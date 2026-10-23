/*
 * WiVRn VR streaming
 * Copyright (C) 2024  Guillaume Meunier <guillaume.meunier@centraliens.net>
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
#pragma once

#include "wave_api.h"
#include <array>
#include <condition_variable>
#include <ctime>
#include <deque>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <jni.h>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <vector>
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

namespace wave
{
// Handles are the address of the object; the magic word guards against
// use-after-free and against handles produced by another runtime.
constexpr uint64_t instance_magic = 0x5761766521212121ull;   // "Wave!!!"
constexpr uint64_t session_magic = 0x53657373696f6e21ull;    // "Session!"
constexpr uint64_t swapchain_magic = 0x5377617063682121ull;  // "Swapp!!"
constexpr uint64_t space_magic = 0x5370616365212121ull;      // "Space!!!"
constexpr uint64_t action_set_magic = 0x416374696f6e5321ull; // "ActionS!"
constexpr uint64_t action_magic = 0x416374696f6e2121ull;     // "Action!!"

template <typename T>
T * object_from_handle(uint64_t handle, uint64_t magic)
{
	auto * object = reinterpret_cast<T *>(handle);
	if (object and object->magic == magic)
		return object;
	return nullptr;
}

template <typename Handle, typename T>
Handle handle_from(T * object)
{
	return reinterpret_cast<Handle>(reinterpret_cast<uintptr_t>(object));
}

struct instance;
struct session;
struct space;
struct swapchain;
struct action;
struct action_set;
instance * instance_from(XrInstance);
session * session_from(XrSession);
space * space_from(XrSpace);
swapchain * swapchain_from(XrSwapchain);
action * action_from(XrAction);
action_set * action_set_from(XrActionSet);

// XrPath interning: XrPath is an opaque 64-bit value, index + 1 in the registry.
struct path_registry
{
	std::vector<std::string> strings;
	std::unordered_map<std::string, XrPath> paths;

	static path_registry & instance();

	XrPath intern(std::string_view path);
	std::string_view get(XrPath path) const;
};

// Wave matrices are column-major (the SDK hands them to glUniformMatrix4fv
// with transpose=GL_FALSE), same convention as glm.
glm::mat4 pose_to_matrix(const wvr::PoseState &);
glm::mat4 pose_to_matrix(const wvr::Pose &);
glm::mat4 matrix_from_wave(const wvr::Matrix4f &);
glm::quat matrix_to_quaternion(const glm::mat4 &);
XrVector3f matrix_translation(const glm::mat4 &);

// Derive the OpenXR (left-handed) field of view triangle from a Wave
// (OpenGL) projection matrix.
XrFovf fov_from_projection(const wvr::Matrix4f &, float & near, float & far);

struct instance
{
	uint64_t magic = instance_magic;
	XrVersion api_version = XR_CURRENT_API_VERSION;
	std::vector<std::string> enabled_extensions;
	std::string application_name;

	// Wave render target size, queried through the view configuration.
	uint32_t render_width = 1440;
	uint32_t render_height = 1440;

	// Vulkan state (XR_KHR_vulkan_enable2). The application creates the VkInstance
	// before the session exists, so it lives on the instance.
	VkInstance vk_instance = VK_NULL_HANDLE;
	// Vulkan device handed to xrCreateSession (XrGraphicsBindingVulkan2KHR). The
	// client creates the VkDevice before the session exists, so the shim cannot
	// bind it at session creation; xrCreateSession adopts these when set.
	VkDevice pending_vk_device = VK_NULL_HANDLE;
	VkPhysicalDevice pending_vk_physical_device = VK_NULL_HANDLE;
	bool wave_render_initialized = false;

	std::unique_ptr<struct session> the_session;
};

struct swapchain
{
	uint64_t magic = swapchain_magic;
	struct session & session;

	swapchain(struct session & s) :
	        session(s)
	{
	}
	// One Wave texture queue per eye; array layer i of the OpenXR swapchain
	// corresponds to queues[i].
	wvr::TextureQueueHandle queues[2] = {nullptr, nullptr};
	uint32_t array_size = 1;

	// Images the client renders into: allocated in the client's VkDevice with
	// array_size layers, then copied into the Wave queues on release.
	std::vector<VkImage> images;
	std::vector<VkDeviceMemory> memory;
	VkCommandPool command_pool = VK_NULL_HANDLE;
	VkCommandBuffer command_buffer = VK_NULL_HANDLE;

	uint32_t width = 0;
	uint32_t height = 0;
	VkFormat format = VK_FORMAT_R8G8B8A8_UNORM;
	XrSwapchainUsageFlags usage_flags = 0;
	uint32_t image_count = 0;

	// Index handed out by the last xrAcquireSwapchainImage() that has not
	// been released yet, -1 when no image is acquired.
	int32_t acquired_index = -1;
	// Index whose copy was last submitted to the Wave queues.
	int32_t presented_index = -1;
	// The release-time copy into the Wave textures is recorded in
	// command_buffer but submitted by end_frame, after the client has
	// submitted every command buffer that writes the image (the WiVRn stream
	// scene releases its swapchain before drawing the GUI overlay).
	bool copy_pending = false;
	// Set when a projection layer referencing this swapchain has been submitted to
	// Wave. Wave recycles queue textures only for those, so a swapchain used by
	// layer types Wave cannot present (quad layers) never gets a texture back and
	// must be rendered locally instead of stalling the frame loop on it.
	bool submitted_to_wave = false;
	bool rendered_locally = false;
	// Round-robin index handed out while rendered_locally is set.
	uint32_t next_local_index = 0;
};

struct space
{
	uint64_t magic = space_magic;
	struct session & session;

	space(struct session & s) :
	        session(s)
	{
	}
	XrReferenceSpaceType type = XR_REFERENCE_SPACE_TYPE_LOCAL;
	// Transform from this space to the Wave "OriginOnHead" world.
	glm::mat4 transform = glm::mat4(1.0f);
	// For action spaces: the device the space follows, Invalid for static spaces.
	wvr::DeviceType device = wvr::DeviceType_Invalid;
	// Local offset of the device inside the space (grip vs aim).
	glm::mat4 offset = glm::mat4(1.0f);
};

struct action_set;

struct action
{
	uint64_t magic = action_magic;
	action_set & set;

	action(action_set & s) :
	        set(s)
	{
	}
	std::string name;
	XrPath path = 0;
	std::vector<XrPath> subaction_paths;

	// Current state, refreshed by xrSyncActions()
	XrActionStateBoolean boolean{};
	XrActionStateFloat float_state{};
	XrActionStateVector2f vector2{};
	XrActionStatePose pose{};

	// Wave binding for this action
	wvr::DeviceType device = wvr::DeviceType_Invalid;
	wvr::InputId input = wvr::InputId_Max;
	int kind = 0; // 0 = boolean, 1 = float, 2 = vector2, 3 = pose
	// For vibration output actions: the device to vibrate.
	wvr::DeviceType haptic_device = wvr::DeviceType_Invalid;
};

struct action_set
{
	uint64_t magic = action_set_magic;
	struct session & session;

	action_set(struct session & s) :
	        session(s)
	{
	}
	std::string name;
	std::string localized_name;
	uint32_t priority = 0;
	std::vector<std::unique_ptr<action>> actions;
};

struct session
{
	uint64_t magic = session_magic;
	struct instance & instance;

	session(struct instance & i) :
	        instance(i)
	{
	}

	enum class state
	{
		ready,
		standby,
		stopping,
		exiting,
	};

	state state_value = state::ready;
	XrViewConfigurationType view_configuration = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
	XrEnvironmentBlendMode environment_blend_mode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
	uint32_t view_count = 2;

	std::vector<std::unique_ptr<swapchain>> swapchains;
	std::vector<std::unique_ptr<space>> spaces;
	std::vector<std::unique_ptr<action_set>> action_sets;
	bool actionsets_attached = false;

	// Interaction profile bindings: profile path -> (action path -> subaction paths)
	std::map<XrPath, std::map<XrPath, std::vector<XrPath>>> bindings;
	XrPath current_interaction_profile = 0;

	// Events queued for xrPollEvent (session state changes)
	std::deque<XrEventDataBuffer> pending_events;

	// Views located during the current frame, used to submit frames
	std::array<XrView, 2> last_views{};
	bool last_views_valid = false;
	XrTime last_frame_time = 0;

	// Frame state
	bool frame_active = false;
	XrTime predicted_display_time = 0;
	uint64_t frame_number = 0;

	// Layer types already reported as unsupported, so the warning is logged once
	// per type instead of once per frame.
	std::set<XrStructureType> reported_layer_types;

	// Vulkan state (XR_KHR_vulkan_enable2)
	VkPhysicalDevice vk_physical_device = VK_NULL_HANDLE;
	VkDevice vk_device = VK_NULL_HANDLE;
	bool wave_render_initialized = false;

	// Wave input state, polled once per xrSyncActions call
	uint32_t input_buttons[4]{};
	uint32_t input_touches[4]{};
	std::array<wvr::AnalogState, 16> input_analog[4]{};
	uint32_t input_analog_count[4]{};

	// Cached device properties
	float refresh_rate = 72.0f;
	// Achieved xrEndFrame rate, EMA-filtered, and the rate last announced to the
	// client through XR_FB_display_refresh_rate. The server paces and encodes at
	// the announced rate; Wave composites only as fast as the app submits, and
	// encoding faster than the headset decodes ages frames past the stream
	// scene's 1s stall guard and tears the connection down.
	float measured_rate = 0.0f;
	float reported_rate = 0.0f;
	XrTime last_end_frame = 0;
	XrTime last_rate_report = 0;
	float ipd = 0.062f;
	uint32_t render_width = 1440;
	uint32_t render_height = 1440;

	// Pipelined presentation: end_frame submits the presentation copies with
	// a fence and hands the WVR_SubmitFrame calls to a dedicated thread, so
	// Wave's multi-millisecond submit IPC overlaps the next frame's
	// recording instead of serializing into the frame loop.
	struct wave_submit
	{
		wvr::Eye eye;
		wvr::TextureParams texture;
	};

	std::thread submit_thread;
	std::mutex submit_mutex;
	std::condition_variable submit_cv;
	std::vector<wave_submit> pending_submits;
	VkFence submit_fence = VK_NULL_HANDLE;
	bool submit_work = false;
	bool submit_shutdown = false;
	// Round-robin fences for the presentation copies: a slot is reused only
	// after three frames, long after the submit thread has waited on it.
	std::array<VkFence, 3> copy_fences{};
	uint32_t copy_fence_next = 0;

	~session();

	wvr::PoseState sync_poses[4]{}; // indexed by wvr::DeviceType
};
bool pop_event(instance &, XrEventDataBuffer *);

// Vulkan entry points the shim calls directly: the client owns the VkDevice,
// the shim only allocates the swapchain images and the presentation copies.
struct vulkan_loader
{
	PFN_vkCreateInstance CreateInstance = nullptr;
	PFN_vkEnumeratePhysicalDevices EnumeratePhysicalDevices = nullptr;
	PFN_vkGetPhysicalDeviceProperties GetPhysicalDeviceProperties = nullptr;
	PFN_vkGetPhysicalDeviceQueueFamilyProperties GetQueueFamilyProperties = nullptr;
	PFN_vkGetPhysicalDeviceMemoryProperties GetPhysicalDeviceMemoryProperties = nullptr;
	PFN_vkGetDeviceProcAddr GetDeviceProcAddr = nullptr;
	PFN_vkGetInstanceProcAddr GetInstanceProcAddr = nullptr;

	vulkan_loader();
};

vulkan_loader & loader();

// Device-level entry points, resolved once per VkDevice.
struct device_vulkan
{
	PFN_vkCreateImage CreateImage = nullptr;
	PFN_vkDestroyImage DestroyImage = nullptr;
	PFN_vkGetImageMemoryRequirements GetImageMemoryRequirements = nullptr;
	PFN_vkAllocateMemory AllocateMemory = nullptr;
	PFN_vkFreeMemory FreeMemory = nullptr;
	PFN_vkBindImageMemory BindImageMemory = nullptr;
	PFN_vkCreateCommandPool CreateCommandPool = nullptr;
	PFN_vkDestroyCommandPool DestroyCommandPool = nullptr;
	PFN_vkAllocateCommandBuffers AllocateCommandBuffers = nullptr;
	PFN_vkFreeCommandBuffers FreeCommandBuffers = nullptr;
	PFN_vkBeginCommandBuffer BeginCommandBuffer = nullptr;
	PFN_vkEndCommandBuffer EndCommandBuffer = nullptr;
	PFN_vkCmdPipelineBarrier CmdPipelineBarrier = nullptr;
	PFN_vkCmdCopyImage CmdCopyImage = nullptr;
	PFN_vkQueueSubmit QueueSubmit = nullptr;
	PFN_vkQueueWaitIdle QueueWaitIdle = nullptr;
	PFN_vkGetDeviceQueue GetDeviceQueue = nullptr;
	PFN_vkCreateFence CreateFence = nullptr;
	PFN_vkResetFences ResetFences = nullptr;
	PFN_vkWaitForFences WaitForFences = nullptr;
	PFN_vkDestroyFence DestroyFence = nullptr;
};

// Resolved entry points for a device, or a struct of nulls when unavailable.
const device_vulkan & device_entries(VkDevice device);
// First queue family with the graphics bit, 0 when unknown.
uint32_t graphics_queue_family(VkPhysicalDevice physical_device);

// Implementation of the OpenXR entry points, split by area.
XrResult create_instance(const XrInstanceCreateInfo *, XrInstance *);
XrResult destroy_instance(XrInstance);
XrResult get_instance_proc_addr(XrInstance, const char *, PFN_xrVoidFunction *);
XrResult get_instance_properties(XrInstance, XrInstanceProperties *);
XrResult enumerate_instance_extension_properties(const char *, uint32_t, uint32_t *, XrExtensionProperties *);
XrResult enumerate_api_layer_properties(uint32_t, uint32_t *, XrApiLayerProperties *);
XrResult poll_event(XrInstance, XrEventDataBuffer *);
XrResult string_to_path(XrInstance, const char *, XrPath *);
XrResult path_to_string(XrInstance, XrPath, uint32_t, uint32_t *, char *);
XrResult convert_timespec_time_to_time_khr(XrInstance, const timespec *, XrTime *);

XrResult create_session(XrInstance, const XrSessionCreateInfo *, XrSession *);
XrResult destroy_session(XrSession);
XrResult begin_session(XrSession, const XrSessionBeginInfo *);
XrResult end_session(XrSession);
XrResult request_exit_session(XrSession);
XrResult get_system(XrInstance, const XrSystemGetInfo *, XrSystemId *);
XrResult get_system_properties(XrInstance, XrSystemId, XrSystemProperties *);
XrResult enumerate_view_configurations(XrInstance, XrSystemId, uint32_t, uint32_t *, XrViewConfigurationType *);
XrResult enumerate_environment_blend_modes(XrInstance, XrSystemId, XrViewConfigurationType, uint32_t, uint32_t *, XrEnvironmentBlendMode *);
XrResult get_view_configuration_properties(XrInstance, XrSystemId, XrViewConfigurationType, XrViewConfigurationProperties *);
XrResult enumerate_view_configuration_views(XrInstance, XrSystemId, XrViewConfigurationType, uint32_t, uint32_t *, XrViewConfigurationView *);
XrResult enumerate_reference_spaces(XrSession, uint32_t, uint32_t *, XrReferenceSpaceType *);
XrResult create_reference_space(XrSession, const XrReferenceSpaceCreateInfo *, XrSpace *);
XrResult destroy_space(XrSpace);
XrResult locate_space(XrSpace, XrSpace, XrTime, XrSpaceLocation *);
XrResult locate_spaces(XrSession, const XrSpacesLocateInfo *, XrSpaceLocations *);
XrResult locate_views(XrSession, const XrViewLocateInfo *, XrViewState *, uint32_t, uint32_t *, XrView *);

XrResult enumerate_swapchain_formats(XrSession, uint32_t, uint32_t *, int64_t *);
XrResult create_swapchain(XrSession, const XrSwapchainCreateInfo *, XrSwapchain *);
XrResult destroy_swapchain(XrSwapchain);
XrResult enumerate_swapchain_images(XrSwapchain, uint32_t, uint32_t *, XrSwapchainImageBaseHeader *);
XrResult acquire_swapchain_image(XrSwapchain, const XrSwapchainImageAcquireInfo *, uint32_t *);
XrResult wait_swapchain_image(XrSwapchain, const XrSwapchainImageWaitInfo *);
XrResult release_swapchain_image(XrSwapchain, const XrSwapchainImageReleaseInfo *);

// Submit the recorded presentation copies with QueueWaitIdle; called from
// release_swapchain_image before the copy command buffer is re-recorded.
void flush_pending_copies(session & s);
// Submit the recorded presentation copies with a fence and return without
// waiting; called from end_frame, the submit thread waits on the fence.
void submit_pending_copies(session & s, VkFence fence);

XrResult wait_frame(XrSession, const XrFrameWaitInfo *, XrFrameState *);
XrResult begin_frame(XrSession, const XrFrameBeginInfo *);
XrResult end_frame(XrSession, const XrFrameEndInfo *);
XrResult enumerate_display_refresh_rates_fb(XrSession, uint32_t, uint32_t *, float *);
XrResult get_display_refresh_rate_fb(XrSession, float *);
XrResult request_display_refresh_rate_fb(XrSession, float);

XrResult get_vulkan_graphics_requirements2_khr(XrInstance, XrSystemId, XrGraphicsRequirementsVulkan2KHR *);
XrResult get_vulkan_graphics_device2_khr(XrInstance, const XrVulkanGraphicsDeviceGetInfoKHR *, VkPhysicalDevice *);
XrResult create_vulkan_instance_khr(XrInstance, const XrVulkanInstanceCreateInfoKHR *, VkInstance *, VkResult *);
XrResult create_vulkan_device_khr(XrInstance, const XrVulkanDeviceCreateInfoKHR *, VkDevice *, VkResult *);

// Wave only delivers input for the ids the app requests; must run after WVR_Init.
void request_wave_inputs();

XrResult attach_actionsets(XrSession, const XrSessionActionSetsAttachInfo *);
XrResult create_action_set(XrInstance, const XrActionSetCreateInfo *, XrActionSet *);
XrResult destroy_action_set(XrActionSet);
XrResult create_action(XrActionSet, const XrActionCreateInfo *, XrAction *);
XrResult suggest_interaction_profile_bindings(XrInstance, const XrInteractionProfileSuggestedBinding *);
XrResult get_current_interaction_profile(XrSession, XrPath, XrInteractionProfileState *);
XrResult create_action_space(XrSession, const XrActionSpaceCreateInfo *, XrSpace *);
XrResult sync_actions(XrSession, const XrActionsSyncInfo *);
XrResult get_action_state_boolean(XrSession, const XrActionStateGetInfo *, XrActionStateBoolean *);
XrResult get_action_state_float(XrSession, const XrActionStateGetInfo *, XrActionStateFloat *);
XrResult get_action_state_vector2f(XrSession, const XrActionStateGetInfo *, XrActionStateVector2f *);
XrResult get_action_state_pose(XrSession, const XrActionStateGetInfo *, XrActionStatePose *);
XrResult enumerate_bound_sources_for_action(XrSession, const XrBoundSourcesForActionEnumerateInfo *, uint32_t, uint32_t *, XrPath *);
XrResult get_input_source_localized_name(XrSession, const XrInputSourceLocalizedNameGetInfo *, uint32_t, uint32_t *, char *);
XrResult apply_haptic_feedback(XrSession, const XrHapticActionInfo *, const XrHapticBaseHeader *);
XrResult stop_haptic_feedback(XrSession, const XrHapticActionInfo *);
XrResult get_visibility_mask_khr(XrSession, XrViewConfigurationType, uint32_t, XrVisibilityMaskTypeKHR, XrVisibilityMaskKHR *);
} // namespace wave
