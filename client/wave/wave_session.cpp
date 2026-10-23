/*
 * WiVRn VR streaming
 * Copyright (C) 2024  Patrick Nicolas <patricknicolas@laposte.net>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "wave_runtime.h"
#include <sched.h>
#include <sys/resource.h>
#include <algorithm>
#include <chrono>
#include <thread>
#include <cstring>
#include <glm/gtc/matrix_inverse.hpp>
#include <spdlog/spdlog.h>

namespace wave
{
namespace
{
XrTime monotonic_now()
{
	return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

void queue_state_event(session & s, XrSessionState state)
{
	XrEventDataBuffer buffer{};
	auto & event = *reinterpret_cast<XrEventDataSessionStateChanged *>(&buffer);
	event.type = XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED;
	event.session = handle_from<XrSession>(&s);
	event.state = state;
	event.time = monotonic_now();
	s.pending_events.push_back(buffer);
}

// Announce a display refresh rate change. The client forwards it as
// from_headset::refresh_rate_changed, which sets the server's encode pacing.
void queue_refresh_rate_event(session & s, float from, float to)
{
	XrEventDataBuffer buffer{};
	auto & event = *reinterpret_cast<XrEventDataDisplayRefreshRateChangedFB *>(&buffer);
	event.type = XR_TYPE_EVENT_DATA_DISPLAY_REFRESH_RATE_CHANGED_FB;
	event.fromDisplayRefreshRate = from;
	event.toDisplayRefreshRate = to;
	s.pending_events.push_back(buffer);
}

// Rate to advertise as the session's current rate: the last announced
// measured rate, falling back to Wave's panel rate before the first frame
// has been measured. Only for xrGetCurrentDisplayRefreshRateFB; the
// enumerate entry point must stay deterministic (see below).
float advertised_refresh_rate(session & s)
{
	if (s.reported_rate > 0.0f)
		return s.reported_rate;
	if (s.measured_rate > 0.0f)
		return s.measured_rate;
	return s.refresh_rate;
}

// Refresh the cached device poses from Wave.
void refresh_poses(session & s)
{
	wvr::api * api = wvr::api::get();
	wvr::DevicePosePair pairs[3] = {
	        {wvr::DeviceType_HMD, {}},
	        {wvr::DeviceType_Controller_Right, {}},
	        {wvr::DeviceType_Controller_Left, {}},
	};
	api->GetSyncPose(wvr::PoseOriginModel_OriginOnHead, pairs, 3);
	s.sync_poses[wvr::DeviceType_HMD] = pairs[0].pose;
	s.sync_poses[wvr::DeviceType_Controller_Right] = pairs[1].pose;
	s.sync_poses[wvr::DeviceType_Controller_Left] = pairs[2].pose;
}

// Wave's SubmitFrame costs several milliseconds of IPC per eye; running it
// on a dedicated thread overlaps that cost with the next frame's recording
// instead of serializing it into the frame loop.
void submit_loop(session & s)
{
	wvr::api * api = wvr::api::get();
	// Run on Wave's RT band (below the render loop's 3) so the submit IPC is
	// not starved by Wave's SCHED_FIFO threads; CFS nice is preempted by them.
	sched_param rt{2};
	if (sched_setscheduler(0, SCHED_FIFO, &rt) != 0)
		setpriority(PRIO_PROCESS, 0, 10);
	while (true)
	{
		std::vector<session::wave_submit> submits;
		VkFence fence = VK_NULL_HANDLE;
		{
			std::unique_lock lock(s.submit_mutex);
			s.submit_cv.wait(lock, [&] { return s.submit_work or s.submit_shutdown; });
			if (s.submit_shutdown)
				return;
			submits = std::move(s.pending_submits);
			s.pending_submits.clear();
			fence = s.submit_fence;
			s.submit_work = false;
		}

		const device_vulkan & vk = device_entries(s.vk_device);
		if (fence != VK_NULL_HANDLE and vk.WaitForFences != nullptr)
		{
			// The copies sit behind the scene command buffer on the queue,
			// so the fence signals once every Wave texture holds the frame.
			if (vk.WaitForFences(s.vk_device, 1, &fence, true, 200'000'000) != VK_SUCCESS)
				spdlog::warn("WaveVR: presentation copy fence wait failed");
		}

		// Wave applies the eye transform itself, so it wants the HMD pose it
		// handed us from WVR_GetSyncPose, read as fresh as possible.
		refresh_poses(s);
		const wvr::PoseState & pose = s.sync_poses[wvr::DeviceType_HMD];
		for (auto & submit: submits)
		{
			wvr::SubmitError submit_error = api->SubmitFrame(submit.eye, &submit.texture, &pose, wvr::SubmitExtend_Default);
			if (submit_error != wvr::SubmitError_None)
				spdlog::warn("WaveVR: WVR_SubmitFrame failed with {}", int(submit_error));
		}
	}
}
} // namespace

session::~session()
{
	{
		std::lock_guard lock(submit_mutex);
		submit_shutdown = true;
	}
	submit_cv.notify_one();
	if (submit_thread.joinable())
		submit_thread.join();

	if (vk_device != VK_NULL_HANDLE)
	{
		const device_vulkan & vk = device_entries(vk_device);
		if (vk.DestroyFence != nullptr)
			for (VkFence fence: copy_fences)
				if (fence != VK_NULL_HANDLE)
					vk.DestroyFence(vk_device, fence, nullptr);
	}
}

session * session_from(XrSession handle)
{
	return object_from_handle<session>(reinterpret_cast<uintptr_t>(handle), session_magic);
}

space * space_from(XrSpace handle)
{
	return object_from_handle<space>(reinterpret_cast<uintptr_t>(handle), space_magic);
}

bool pop_event(instance & inst, XrEventDataBuffer * event)
{
	if (not inst.the_session)
		return false;
	auto & queue = inst.the_session->pending_events;
	if (queue.empty())
		return false;
	*event = queue.front();
	queue.pop_front();
	return true;
}

XrResult get_system(XrInstance handle, const XrSystemGetInfo * info, XrSystemId * system_id)
{
	if (not instance_from(handle))
		return XR_ERROR_HANDLE_INVALID;

	if (info->formFactor != XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY)
		return XR_ERROR_FORM_FACTOR_UNAVAILABLE;

	*system_id = 1;
	return XR_SUCCESS;
}

XrResult get_system_properties(XrInstance handle, XrSystemId system_id, XrSystemProperties * properties)
{
	if (not instance_from(handle))
		return XR_ERROR_HANDLE_INVALID;
	if (system_id != 1)
		return XR_ERROR_HANDLE_INVALID;

	properties->type = XR_TYPE_SYSTEM_PROPERTIES;
	properties->graphicsProperties.maxSwapchainImageHeight = 1440;
	properties->graphicsProperties.maxSwapchainImageWidth = 1440;
	properties->graphicsProperties.maxLayerCount = 8;
	properties->vendorId = 0x0bb4; // HTC
	std::strncpy(properties->systemName, "VIVE Focus Plus", XR_MAX_SYSTEM_NAME_SIZE - 1);
	properties->systemName[XR_MAX_SYSTEM_NAME_SIZE - 1] = '\0';
	properties->systemId = 1;

	properties->trackingProperties.orientationTracking = true;
	properties->trackingProperties.positionTracking = true;

	return XR_SUCCESS;
}

XrResult enumerate_view_configurations(XrInstance instance_handle, XrSystemId system_id, uint32_t capacity, uint32_t * count, XrViewConfigurationType * configurations)
{
	if (not instance_from(instance_handle) or system_id != 1)
		return XR_ERROR_HANDLE_INVALID;

	if (capacity == 0)
	{
		*count = 1;
		return XR_SUCCESS;
	}

	*count = 1;
	configurations[0] = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
	return XR_SUCCESS;
}

XrResult enumerate_environment_blend_modes(XrInstance instance_handle, XrSystemId system_id, XrViewConfigurationType configuration, uint32_t capacity, uint32_t * count, XrEnvironmentBlendMode * modes)
{
	if (not instance_from(instance_handle) or system_id != 1)
		return XR_ERROR_HANDLE_INVALID;
	if (configuration != XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO)
		return XR_ERROR_VIEW_CONFIGURATION_TYPE_UNSUPPORTED;

	// The VIVE Focus Plus is opaque.
	if (capacity == 0)
	{
		*count = 1;
		return XR_SUCCESS;
	}

	*count = 1;
	modes[0] = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
	return XR_SUCCESS;
}

XrResult get_view_configuration_properties(XrInstance instance_handle, XrSystemId system_id, XrViewConfigurationType configuration, XrViewConfigurationProperties * properties)
{
	auto * inst = instance_from(instance_handle);
	if (not inst or system_id != 1)
		return XR_ERROR_HANDLE_INVALID;
	if (configuration != XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO)
		return XR_ERROR_VIEW_CONFIGURATION_TYPE_UNSUPPORTED;

	properties->type = XR_TYPE_VIEW_CONFIGURATION_PROPERTIES;
	properties->viewConfigurationType = configuration;
	properties->fovMutable = false;
	if (inst->the_session)
		inst->the_session->view_configuration = configuration;
	return XR_SUCCESS;
}

XrResult enumerate_view_configuration_views(XrInstance instance_handle, XrSystemId system_id, XrViewConfigurationType configuration, uint32_t capacity, uint32_t * count, XrViewConfigurationView * views)
{
	auto * inst = instance_from(instance_handle);
	if (not inst or system_id != 1)
		return XR_ERROR_HANDLE_INVALID;
	if (configuration != XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO)
		return XR_ERROR_VIEW_CONFIGURATION_TYPE_UNSUPPORTED;

	if (capacity == 0)
	{
		*count = 2;
		return XR_SUCCESS;
	}

	uint32_t width = 0, height = 0;
	wvr::api::get()->GetRenderTargetSize(&width, &height);
	if (width == 0 or height == 0)
	{
		width = 1440;
		height = 1440;
	}
	inst->render_width = width;
	inst->render_height = height;

	*count = std::min(capacity, 2u);
	for (uint32_t i = 0; i < *count; ++i)
	{
		views[i].type = XR_TYPE_VIEW_CONFIGURATION_VIEW;
		views[i].recommendedImageRectWidth = width;
		views[i].maxImageRectWidth = width;
		views[i].recommendedImageRectHeight = height;
		views[i].maxImageRectHeight = height;
		views[i].recommendedSwapchainSampleCount = 1;
		views[i].maxSwapchainSampleCount = 1;
	}

	if (capacity < 2)
		return XR_ERROR_SIZE_INSUFFICIENT;

	return XR_SUCCESS;
}

XrResult create_session(XrInstance instance_handle, const XrSessionCreateInfo * create_info, XrSession * handle)
{
	auto * inst = instance_from(instance_handle);
	if (not inst)
		return XR_ERROR_HANDLE_INVALID;
	if (create_info->systemId != 1)
		return XR_ERROR_HANDLE_INVALID;

	auto s = std::make_unique<session>(*inst);
	queue_state_event(*s, XR_SESSION_STATE_READY);

	wvr::RenderProps render_props{};
	if (wvr::api::get()->GetRenderProps(&render_props) and render_props.refreshRate > 0)
		s->refresh_rate = render_props.refreshRate;
	if (render_props.ipdMeter > 0)
		s->ipd = render_props.ipdMeter;
	spdlog::info("WaveVR: render props refreshRate={} ipd={} externalDisplay={}",
	             render_props.refreshRate,
	             render_props.ipdMeter,
	             render_props.hasExternal);

	// The client creates the VkDevice (xrCreateVulkanDeviceKHR) before the
	// session exists, so create_vulkan_device_khr cannot bind it to a session.
	// Adopt it here so xrCreateSwapchain sees a live Vulkan device.
	if (inst->pending_vk_device != VK_NULL_HANDLE)
	{
		s->vk_device = inst->pending_vk_device;
		s->vk_physical_device = inst->pending_vk_physical_device;
		s->wave_render_initialized = inst->wave_render_initialized;
	}

	request_wave_inputs();

	*handle = handle_from<XrSession>(s.get());
	inst->the_session = std::move(s);
	inst->the_session->submit_thread = std::thread([sess = inst->the_session.get()] { submit_loop(*sess); });

	return XR_SUCCESS;
}

XrResult destroy_session(XrSession handle)
{
	auto * s = session_from(handle);
	if (not s)
		return XR_ERROR_HANDLE_INVALID;
	s->instance.the_session.reset();
	return XR_SUCCESS;
}

XrResult begin_session(XrSession handle, const XrSessionBeginInfo *)
{
	auto * s = session_from(handle);
	if (not s)
		return XR_ERROR_HANDLE_INVALID;
	if (s->state_value != session::state::ready)
		return XR_ERROR_SESSION_NOT_RUNNING;

	s->state_value = session::state::standby;
	queue_state_event(*s, XR_SESSION_STATE_IDLE);
	queue_state_event(*s, XR_SESSION_STATE_VISIBLE);
	queue_state_event(*s, XR_SESSION_STATE_FOCUSED);
	return XR_SUCCESS;
}

XrResult request_exit_session(XrSession handle)
{
	auto * s = session_from(handle);
	if (not s)
		return XR_ERROR_HANDLE_INVALID;
	if (s->state_value != session::state::standby)
		return XR_ERROR_SESSION_NOT_RUNNING;

	s->state_value = session::state::exiting;
	queue_state_event(*s, XR_SESSION_STATE_EXITING);
	return XR_SUCCESS;
}

XrResult end_session(XrSession handle)
{
	auto * s = session_from(handle);
	if (not s)
		return XR_ERROR_HANDLE_INVALID;

	s->state_value = session::state::ready;
	return XR_SUCCESS;
}

XrResult enumerate_reference_spaces(XrSession handle, uint32_t capacity, uint32_t * count, XrReferenceSpaceType * spaces)
{
	if (not session_from(handle))
		return XR_ERROR_HANDLE_INVALID;

	static constexpr std::array<XrReferenceSpaceType, 3> supported = {
	        XR_REFERENCE_SPACE_TYPE_LOCAL,
	        XR_REFERENCE_SPACE_TYPE_VIEW,
	        XR_REFERENCE_SPACE_TYPE_STAGE,
	};

	if (capacity == 0)
	{
		*count = static_cast<uint32_t>(supported.size());
		return XR_SUCCESS;
	}

	*count = std::min(capacity, static_cast<uint32_t>(supported.size()));
	std::copy_n(supported.begin(), *count, spaces);

	if (capacity < supported.size())
		return XR_ERROR_SIZE_INSUFFICIENT;

	return XR_SUCCESS;
}

XrResult create_reference_space(XrSession handle, const XrReferenceSpaceCreateInfo * create_info, XrSpace * space_handle)
{
	auto * s = session_from(handle);
	if (not s)
		return XR_ERROR_HANDLE_INVALID;

	switch (create_info->referenceSpaceType)
	{
		case XR_REFERENCE_SPACE_TYPE_LOCAL:
		case XR_REFERENCE_SPACE_TYPE_VIEW:
		case XR_REFERENCE_SPACE_TYPE_STAGE:
			break;
		default:
			return XR_ERROR_REFERENCE_SPACE_UNSUPPORTED;
	}

	auto sp = std::make_unique<space>(*s);
	sp->type = create_info->referenceSpaceType;
	// Wave poses are reported in the head-centered (OriginOnHead) world, which
	// is what LOCAL/VIEW reference spaces use. STAGE is head-anchored on this
	// device (3-DoF tracking), so it shares the same world.
	sp->transform = glm::mat4(1.0f);

	*space_handle = handle_from<XrSpace>(sp.get());
	s->spaces.push_back(std::move(sp));
	return XR_SUCCESS;
}

XrResult destroy_space(XrSpace handle)
{
	auto * sp = space_from(handle);
	if (not sp)
		return XR_ERROR_HANDLE_INVALID;
	// Erase only this space: erase(first, end()) would destroy every space
	// created after it, invalidating handles the client still uses.
	std::erase_if(sp->session.spaces, [sp](const std::unique_ptr<space> & s) { return s.get() == sp; });
	return XR_SUCCESS;
}

namespace
{
// Wave has no velocity tracking, so only the pose part of a location is filled.
XrResult locate_space_impl(XrSpace space_handle, XrSpace base_handle, XrSpaceLocationFlags & flags, XrPosef & pose)
{
	auto * a = space_from(space_handle);
	auto * b = space_from(base_handle);
	if (not a or not b)
		return XR_ERROR_HANDLE_INVALID;

	glm::mat4 world;
	if (a->device != wvr::DeviceType_Invalid)
	{
		// A disconnected device has no pose: report it invalid instead of
		// returning the stale/identity sync pose, which would render the
		// controller and its ray at the user's head.
		wvr::api * api = wvr::api::get();
		if (api->IsDeviceConnected and not api->IsDeviceConnected(a->device))
		{
			flags = 0;
			pose = {};
			return XR_SUCCESS;
		}
		refresh_poses(a->session);
		world = pose_to_matrix(a->session.sync_poses[a->device]) * a->offset;
	}
	else
	{
		world = a->transform;
	}

	glm::mat4 relative = glm::affineInverse(b->transform) * world;

	glm::quat q = matrix_to_quaternion(relative);
	// Same rotation-convention conversion as locate_views.
	pose.orientation = XrQuaternionf{-q.x, -q.y, -q.z, q.w};
	pose.position = matrix_translation(relative);
	flags = XR_SPACE_LOCATION_POSITION_VALID_BIT | XR_SPACE_LOCATION_ORIENTATION_VALID_BIT;
	return XR_SUCCESS;
}
} // namespace

XrResult locate_space(XrSpace space_handle, XrSpace base_handle, XrTime, XrSpaceLocation * location)
{
	location->type = XR_TYPE_SPACE_LOCATION;
	return locate_space_impl(space_handle, base_handle, location->locationFlags, location->pose);
}

XrResult locate_spaces(XrSession handle, const XrSpacesLocateInfo * info, XrSpaceLocations * locations)
{
	if (not session_from(handle))
		return XR_ERROR_HANDLE_INVALID;

	if (locations->locationCount < info->spaceCount)
		return XR_ERROR_SIZE_INSUFFICIENT;

	// One Wave pose refresh serves every device space in the batch.
	XrResult result = XR_SUCCESS;
	for (uint32_t i = 0; i < info->spaceCount; ++i)
	{
		locations->locations[i].locationFlags = 0;
		XrResult r = locate_space_impl(info->spaces[i], info->baseSpace, locations->locations[i].locationFlags, locations->locations[i].pose);
		if (r != XR_SUCCESS)
			result = r;
	}
	locations->locationCount = info->spaceCount;
	return result;
}

XrResult locate_views(XrSession handle, const XrViewLocateInfo * info, XrViewState * view_state, uint32_t capacity, uint32_t * count, XrView * views)
{
	auto * s = session_from(handle);
	if (not s)
		return XR_ERROR_HANDLE_INVALID;
	if (info->viewConfigurationType != XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO)
		return XR_ERROR_VIEW_CONFIGURATION_TYPE_UNSUPPORTED;

	if (capacity == 0)
	{
		*count = 2;
		return XR_SUCCESS;
	}

	auto * base_space = space_from(info->space);
	if (not base_space)
		return XR_ERROR_HANDLE_INVALID;

	wvr::api * api = wvr::api::get();
	refresh_poses(*s);

	glm::mat4 head = pose_to_matrix(s->sync_poses[wvr::DeviceType_HMD]);
	static uint64_t h_log = 0;
	if ((h_log++ % 120) == 0)
	{
		const wvr::Pose & rp = s->sync_poses[wvr::DeviceType_HMD].rawPose;
		glm::quat rq(rp.rotation.w, rp.rotation.x, rp.rotation.y, rp.rotation.z);
		glm::quat mq = matrix_to_quaternion(head);
		wvr::Matrix4f eth = wvr::api::get()->GetTransformFromEyeToHead(wvr::Eye_Left, wvr::NumDoF_6DoF);
		spdlog::info("head cmp: mat q {:.3f} {:.3f} {:.3f} {:.3f} raw q {:.3f} {:.3f} {:.3f} {:.3f} rawp {:.3f} {:.3f} {:.3f} eth m3 {} {} {} m03 {} {} {}",
		        mq.w, mq.x, mq.y, mq.z, rq.w, rq.x, rq.y, rq.z, rp.position.v[0], rp.position.v[1], rp.position.v[2],
		        eth.m[3][0], eth.m[3][1], eth.m[3][2], eth.m[0][3], eth.m[1][3], eth.m[2][3]);
	}
	glm::mat4 from_base = glm::affineInverse(base_space->transform);

	*count = std::min(capacity, 2u);
	for (uint32_t i = 0; i < *count; ++i)
	{
		wvr::Eye eye = i == 0 ? wvr::Eye_Left : wvr::Eye_Right;
		glm::mat4 eye_to_head = matrix_from_wave(api->GetTransformFromEyeToHead(eye, wvr::NumDoF_6DoF));
		glm::mat4 view_in_space = from_base * head * eye_to_head;

		views[i].type = XR_TYPE_VIEW;
		glm::quat q = matrix_to_quaternion(view_in_space);
		// Wave's poseMatrix rotation block is the transpose of the OpenXR
		// convention on this runtime (all head rotations appeared mirrored
		// on-device while translations were correct); conjugate the
		// orientation, keep the position.
		views[i].pose.orientation = XrQuaternionf{-q.x, -q.y, -q.z, q.w};
		views[i].pose.position = matrix_translation(view_in_space);
		// XrView::pose carries no validity flags in OpenXR 1.0.

		float near, far;
		views[i].fov = fov_from_projection(api->GetProjection(eye, 0.01f, 1000.0f), near, far);
	}

	if (capacity < 2)
		return XR_ERROR_SIZE_INSUFFICIENT;

	if (*count == 2)
	{
		s->last_views[0] = views[0];
		s->last_views[1] = views[1];
		s->last_views_valid = true;
	}

	view_state->type = XR_TYPE_VIEW_STATE;
	view_state->viewStateFlags = XR_SPACE_LOCATION_POSITION_TRACKED_BIT | XR_SPACE_LOCATION_ORIENTATION_TRACKED_BIT;

	return XR_SUCCESS;
}

XrResult wait_frame(XrSession handle, const XrFrameWaitInfo *, XrFrameState * frame_state)
{
	auto * s = session_from(handle);
	if (not s)
		return XR_ERROR_HANDLE_INVALID;

	frame_state->type = XR_TYPE_FRAME_STATE;

	if (s->state_value != session::state::standby)
	{
		frame_state->shouldRender = false;
		frame_state->predictedDisplayTime = monotonic_now();
		frame_state->predictedDisplayPeriod = 0;
		return XR_SUCCESS;
	}

	// xrWaitFrame must block until the frame is ready to be rendered. Returning
	// instantly makes application::loop() spin at full speed: stream::render()
	// then re-blits the same decoded frame hundreds of times per second,
	// predictedDisplayTime races ahead of wall clock (so common_frame keeps
	// matching the same frame), and the stream scene's 1s stall guard measures
	// the age of a frame being redisplayed instead of the decoder's arrival rate.
	XrTime interval = XrTime(1e9 / s->refresh_rate);
	XrTime pace_entry = monotonic_now();
	XrTime prev_frame_time = s->last_frame_time;
	XrTime target = std::max(pace_entry, prev_frame_time + interval);

	// Busy-wait: the VRThread runs as CFS nice 12 (RT promotion is denied for
	// Wave-created threads), and its sleep_for wakes hundreds of ms late when
	// Wave's SCHED_FIFO threads saturate the cores — the frame rate collapses
	// to the wake-up latency. Spinning keeps the thread runnable so it
	// converges on the target at CFS timeslice granularity.
	while (monotonic_now() < target)
	{
	}

	frame_state->shouldRender = true;
	frame_state->predictedDisplayTime = target + interval;
	frame_state->predictedDisplayPeriod = interval;

	s->predicted_display_time = frame_state->predictedDisplayTime;
	s->last_frame_time = target;


	return XR_SUCCESS;
}

XrResult begin_frame(XrSession handle, const XrFrameBeginInfo *)
{
	auto * s = session_from(handle);
	if (not s)
		return XR_ERROR_HANDLE_INVALID;
	s->frame_active = true;
	s->last_views_valid = false;
	return XR_SUCCESS;
}

XrResult end_frame(XrSession handle, const XrFrameEndInfo * info)
{
	auto * s = session_from(handle);
	if (not s)
		return XR_ERROR_HANDLE_INVALID;

	s->frame_active = false;

	wvr::api * api = wvr::api::get();
	spdlog::debug("WaveVR: xrEndFrame layerCount={} swapchains={}", info->layerCount, s->swapchains.size());

	XrTime t_end0 = monotonic_now();

	// Capture this frame's Wave submits; the submit thread performs them so
	// their multi-millisecond IPC overlaps the next frame's recording.
	std::vector<session::wave_submit> submits;
	for (uint32_t i = 0; i < info->layerCount; ++i)
	{
		const XrCompositionLayerBaseHeader * layer = info->layers[i];
		if (layer->type != XR_TYPE_COMPOSITION_LAYER_PROJECTION)
		{
			// Wave composites eye textures only: quad layers (the WiVRn GUI) and
			// every other layer type have no Wave presentation path.
			if (s->reported_layer_types.insert(layer->type).second)
				spdlog::warn("WaveVR: dropping composition layer of type {}; Wave presents projection layers only", int(layer->type));
			continue;
		}

		const auto & projection = *reinterpret_cast<const XrCompositionLayerProjection *>(layer);

		if (s->swapchains.empty())
		{
			spdlog::error("WaveVR: xrEndFrame with no swapchain");
			continue;
		}

		// Each projection view names the swapchain it rendered into; resolve it
		// from the layer instead of assuming swapchains[0] (the imgui overlay
		// swapchain is created first and has a single eye).
		for (uint32_t view_index = 0; view_index < projection.viewCount; ++view_index)
		{
			auto * sc = swapchain_from(projection.views[view_index].subImage.swapchain);
			if (not sc)
			{
				spdlog::error("WaveVR: projection view {} has no swapchain", view_index);
				continue;
			}

			if (view_index >= sc->array_size)
			{
				spdlog::error("WaveVR: swapchain has no layer for view {}", view_index);
				continue;
			}

			if (sc->presented_index < 0)
			{
				spdlog::warn("WaveVR: no image presented for view {}", view_index);
				continue;
			}

			wvr::TextureParams texture = api->GetTexture(sc->queues[view_index], sc->presented_index);
			static uint64_t t_log = 0;
			if ((t_log++ % 120) == 0)
				spdlog::info("present uv {}: low {} {} up {} {} handle {}", view_index,
				        texture.layout.leftLowUVs.v[0], texture.layout.leftLowUVs.v[1],
				        texture.layout.rightUpUVs.v[0], texture.layout.rightUpUVs.v[1],
				        texture.id);
			sc->submitted_to_wave = true;
			submits.push_back({wvr::Eye(view_index), texture});
		}
	}

	// Submit the presentation copies recorded at release behind the scene
	// command buffer (queue FIFO) with a fence, without waiting: the fence
	// signals once every Wave texture holds the finished frame, and the
	// submit thread waits on it. The frame loop never blocks on the GPU.
	VkFence fence = VK_NULL_HANDLE;
	const device_vulkan & vk = device_entries(s->vk_device);
	if (vk.CreateFence != nullptr and s->copy_fences[0] == VK_NULL_HANDLE)
	{
		const VkFenceCreateInfo fence_info{
		        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
		};
		for (VkFence & f: s->copy_fences)
			vk.CreateFence(s->vk_device, &fence_info, nullptr, &f);
	}
	if (s->copy_fences[0] != VK_NULL_HANDLE)
	{
		fence = s->copy_fences[s->copy_fence_next];
		s->copy_fence_next = (s->copy_fence_next + 1) % uint32_t(s->copy_fences.size());
		if (vk.ResetFences != nullptr)
			vk.ResetFences(s->vk_device, 1, &fence);
	}
	submit_pending_copies(*s, fence);

	{
		std::lock_guard lock(s->submit_mutex);
		s->pending_submits = std::move(submits);
		s->submit_fence = fence;
		s->submit_work = true;
	}
	s->submit_cv.notify_one();

	// Measure the achieved frame rate and announce it. Wave composites only as
	// fast as the app submits frames, and the decode+render loop sustains far
	// less than the panel's 75Hz. Encoding at the panel rate makes decoded frames
	// age past the stream scene's 1s stall guard, which pops the scene and closes
	// the connection; reporting the sustained rate keeps the server encoding at a
	// rate the headset can consume.
	XrTime now = monotonic_now();
	if (s->last_end_frame)
	{
		XrTime dt = now - s->last_end_frame;
		if (dt > 5'000'000 and dt < 500'000'000)
		{
			float instant = 1e9f / float(dt);
			s->measured_rate = s->measured_rate > 0.0f ? 0.75f * s->measured_rate + 0.25f * instant : instant;
		}
	}
	s->last_end_frame = now;

	if (s->measured_rate > 0.0f)
	{
		float target = std::clamp(s->measured_rate, 10.0f, s->refresh_rate);
		bool first = s->reported_rate == 0.0f;
		bool drifted = std::abs(target - s->reported_rate) > 0.1f * s->reported_rate;
		if ((first or drifted) and now - s->last_rate_report > 1'000'000'000)
		{
			spdlog::info("WaveVR: announcing display refresh rate {:.1f}Hz (measured {:.1f}Hz)", target, s->measured_rate);
			queue_refresh_rate_event(*s, s->reported_rate > 0.0f ? s->reported_rate : s->refresh_rate, target);
			s->reported_rate = target;
			s->last_rate_report = now;
		}
	}
	return XR_SUCCESS;
}

XrResult enumerate_display_refresh_rates_fb(XrSession handle, uint32_t capacity, uint32_t * count, float * rates)
{
	auto * s = session_from(handle);
	if (not s)
		return XR_ERROR_HANDLE_INVALID;

	*count = 1;
	if (capacity == 0)
		return XR_SUCCESS;

	// The client puts this list in the headset info packet, and the server
	// refuses a reconnecting headset whose list differs from the live
	// session's (validate_headset_info). A measured rate here varies per
	// run and gets the second connection rejected, which closes the stream
	// socket and kills the client's network thread. Advertise only Wave's
	// stable panel rate; encode pacing follows the announced
	// XR_FB_display_refresh_rate events instead.
	rates[0] = s->refresh_rate;
	return XR_SUCCESS;
}

XrResult get_display_refresh_rate_fb(XrSession handle, float * rate)
{
	auto * s = session_from(handle);
	if (not s or not rate)
		return XR_ERROR_HANDLE_INVALID;

	*rate = advertised_refresh_rate(*s);
	return XR_SUCCESS;
}

XrResult request_display_refresh_rate_fb(XrSession handle, float rate)
{
	auto * s = session_from(handle);
	if (not s)
		return XR_ERROR_HANDLE_INVALID;

	// Wave owns the panel rate and the shim's frame pacing; the request only
	// steers the server's encode pacing, which the measured-rate event controls.
	spdlog::info("WaveVR: xrRequestDisplayRefreshRateFB({:.1f}Hz) accepted, panel stays at {:.1f}Hz", rate, s->refresh_rate);
	return XR_SUCCESS;
}

XrResult get_visibility_mask_khr(XrSession handle, XrViewConfigurationType, uint32_t, XrVisibilityMaskTypeKHR mask_type, XrVisibilityMaskKHR * mask)
{
	if (not session_from(handle))
		return XR_ERROR_HANDLE_INVALID;

	static constexpr std::array<XrVector2f, 4> vertices = {
	        XrVector2f{0.0f, 0.0f},
	        XrVector2f{1.0f, 0.0f},
	        XrVector2f{1.0f, 1.0f},
	        XrVector2f{0.0f, 1.0f},
	};
	static constexpr std::array<uint32_t, 6> indices = {0, 1, 2, 0, 2, 3};

	if (mask_type == XR_VISIBILITY_MASK_TYPE_HIDDEN_TRIANGLE_MESH_KHR)
	{
		mask->vertexCountOutput = 0;
		mask->indexCountOutput = 0;
		return XR_SUCCESS;
	}

	if (mask->vertexCapacityInput == 0)
	{
		mask->vertexCountOutput = static_cast<uint32_t>(vertices.size());
		mask->indexCountOutput = static_cast<uint32_t>(indices.size());
		return XR_SUCCESS;
	}

	if (mask->vertexCapacityInput < vertices.size() or mask->indexCapacityInput < indices.size())
		return XR_ERROR_SIZE_INSUFFICIENT;

	mask->vertexCountOutput = static_cast<uint32_t>(vertices.size());
	mask->indexCountOutput = static_cast<uint32_t>(indices.size());
	std::copy(vertices.begin(), vertices.end(), mask->vertices);
	std::copy(indices.begin(), indices.end(), mask->indices);
	return XR_SUCCESS;
}
} // namespace wave
