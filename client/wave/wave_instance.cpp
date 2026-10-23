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

#include "wave_runtime.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <mutex>
#include <spdlog/spdlog.h>

namespace wave
{
path_registry & path_registry::instance()
{
	static path_registry registry;
	return registry;
}

XrPath path_registry::intern(std::string_view path)
{
	std::string key(path);
	auto [it, inserted] = paths.try_emplace(key, 0);
	if (inserted)
	{
		it->second = static_cast<XrPath>(strings.size() + 1);
		strings.push_back(std::move(key));
	}
	return it->second;
}

std::string_view path_registry::get(XrPath path) const
{
	if (path == 0)
		return "XR_PATH_NULL";
	if (path > strings.size())
		return "XR_PATH_INVALID";
	return strings[path - 1];
}

namespace
{
// Extensions implemented by this runtime.
// XR_KHR_android_create_instance: the client always requests it on Android, and
// xrCreateInstance fails with XR_ERROR_EXTENSION_NOT_PRESENT if it is not advertised.
// XR_HTC_vive_focus3_controller_interaction: WiVRn gates its
// /interaction_profiles/htc/vive_focus3_controller profile on it; without it every
// controller profile is unavailable and the app runs with no input.
constexpr std::array<const char *, 6> supported_extensions = {
        XR_FB_DISPLAY_REFRESH_RATE_EXTENSION_NAME,
        XR_KHR_ANDROID_CREATE_INSTANCE_EXTENSION_NAME,
        XR_KHR_CONVERT_TIMESPEC_TIME_EXTENSION_NAME,
        XR_KHR_VULKAN_ENABLE2_EXTENSION_NAME,
        XR_KHR_VISIBILITY_MASK_EXTENSION_NAME,
        XR_HTC_VIVE_FOCUS3_CONTROLLER_INTERACTION_EXTENSION_NAME,
};

struct instance_registry
{
	std::mutex mutex;
	std::vector<std::unique_ptr<instance>> instances;
};

instance_registry & registry()
{
	static instance_registry r;
	return r;
}

bool supports_extension(std::string_view name)
{
	return std::find(supported_extensions.begin(), supported_extensions.end(), name) != supported_extensions.end();
}
} // namespace

instance * instance_from(XrInstance handle)
{
	return object_from_handle<instance>(reinterpret_cast<uintptr_t>(handle), instance_magic);
}

XrResult enumerate_api_layer_properties(uint32_t, uint32_t * count, XrApiLayerProperties *)
{
	*count = 0;
	return XR_SUCCESS;
}

XrResult enumerate_instance_extension_properties(const char * layer_name, uint32_t capacity, uint32_t * count, XrExtensionProperties * properties)
{
	if (layer_name and *layer_name)
		return XR_ERROR_API_LAYER_NOT_PRESENT;

	if (capacity == 0 or properties == nullptr)
	{
		*count = static_cast<uint32_t>(supported_extensions.size());
		return XR_SUCCESS;
	}

	*count = std::min<uint32_t>(capacity, static_cast<uint32_t>(supported_extensions.size()));
	for (uint32_t i = 0; i < *count; ++i)
	{
		properties[i].type = XR_TYPE_EXTENSION_PROPERTIES;
		std::strncpy(properties[i].extensionName, supported_extensions[i], XR_MAX_EXTENSION_NAME_SIZE - 1);
		properties[i].extensionName[XR_MAX_EXTENSION_NAME_SIZE - 1] = '\0';
		properties[i].extensionVersion = 1;
	}

	if (capacity < supported_extensions.size())
		return XR_ERROR_SIZE_INSUFFICIENT;

	return XR_SUCCESS;
}

XrResult create_instance(const XrInstanceCreateInfo * create_info, XrInstance * handle)
{
	if (create_info->applicationInfo.apiVersion > XR_API_VERSION_1_1)
		return XR_ERROR_API_VERSION_UNSUPPORTED;

	for (uint32_t i = 0; i < create_info->enabledExtensionCount; ++i)
	{
		if (not supports_extension(create_info->enabledExtensionNames[i]))
		{
			spdlog::error("WaveVR: instance extension {} is not supported", create_info->enabledExtensionNames[i]);
			return XR_ERROR_RUNTIME_UNAVAILABLE;
		}
	}

	if (not wvr::api::load())
		return XR_ERROR_RUNTIME_UNAVAILABLE;

	// WVR_Init opens the connection to the Wave service; every other WVR_* entry point
	// requires it, and Wave reports InitError_NotInitialized otherwise.
	if (wvr::InitError init_error = wvr::api::get()->Init(wvr::AppType_VRContent); init_error != wvr::InitError_None)
	{
		spdlog::error("WaveVR: WVR_Init failed: {}", wvr::api::get()->GetInitErrorString(init_error));
		return XR_ERROR_RUNTIME_UNAVAILABLE;
	}

	auto inst = std::make_unique<instance>();
	inst->api_version = create_info->applicationInfo.apiVersion;
	inst->application_name = create_info->applicationInfo.applicationName;
	for (uint32_t i = 0; i < create_info->enabledExtensionCount; ++i)
		inst->enabled_extensions.push_back(create_info->enabledExtensionNames[i]);

	spdlog::info("WaveVR: instance created for \"{}\", API version {}.{}",
	             inst->application_name,
	             XR_VERSION_MAJOR(inst->api_version),
	             XR_VERSION_MINOR(inst->api_version));

	*handle = handle_from<XrInstance>(inst.get());
	{
		std::scoped_lock lock(registry().mutex);
		registry().instances.push_back(std::move(inst));
	}

	return XR_SUCCESS;
}

XrResult destroy_instance(XrInstance handle)
{
	{
		std::scoped_lock lock(registry().mutex);
		auto & v = registry().instances;
		auto it = std::ranges::find_if(v, [handle](const std::unique_ptr<instance> & i) {
			return handle_from<XrInstance>(i.get()) == handle;
		});
		if (it != v.end())
			v.erase(it);
	}
	wvr::api::get()->Quit();
	return XR_SUCCESS;
}

XrResult get_instance_properties(XrInstance handle, XrInstanceProperties * properties)
{
	if (not instance_from(handle))
		return XR_ERROR_HANDLE_INVALID;

	properties->type = XR_TYPE_INSTANCE_PROPERTIES;
	properties->runtimeVersion = 0;
	std::strncpy(properties->runtimeName, "WiVRn WaveVR", XR_MAX_RUNTIME_NAME_SIZE - 1);
	properties->runtimeName[XR_MAX_RUNTIME_NAME_SIZE - 1] = '\0';
	return XR_SUCCESS;
}

XrResult string_to_path(XrInstance handle, const char * path, XrPath * result)
{
	if (not instance_from(handle))
		return XR_ERROR_HANDLE_INVALID;
	*result = path_registry::instance().intern(path);
	return XR_SUCCESS;
}

XrResult path_to_string(XrInstance handle, XrPath path, uint32_t capacity, uint32_t * count, char * buffer)
{
	if (not instance_from(handle))
		return XR_ERROR_HANDLE_INVALID;

	std::string_view value = path_registry::instance().get(path);
	*count = static_cast<uint32_t>(value.size());

	if (capacity == 0 or buffer == nullptr)
		return XR_SUCCESS;

	if (capacity <= value.size())
		return XR_ERROR_SIZE_INSUFFICIENT;

	std::memcpy(buffer, value.data(), value.size());
	buffer[value.size()] = '\0';
	return XR_SUCCESS;
}

XrResult convert_timespec_time_to_time_khr(XrInstance handle, const timespec * time, XrTime * result)
{
	if (not instance_from(handle))
		return XR_ERROR_HANDLE_INVALID;

	// Wave pose timestamps use the same clock as clock_gettime(CLOCK_MONOTONIC).
	*result = static_cast<XrTime>(time->tv_sec) * 1000000000ll + time->tv_nsec;
	return XR_SUCCESS;
}

XrResult poll_event(XrInstance handle, XrEventDataBuffer * event)
{
	auto * inst = instance_from(handle);
	if (not inst)
		return XR_ERROR_HANDLE_INVALID;

	wvr::api * api = wvr::api::get();
	wvr::Event wave_event;

	event->type = XR_TYPE_EVENT_DATA_BUFFER;

	if (pop_event(*inst, event))
		return XR_SUCCESS;

	while (api->PollEventQueue(&wave_event))
	{
		switch (wave_event.common.type)
		{
			case wvr::EventType_Quit:
				event->type = XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING;
				return XR_SUCCESS;

			case wvr::EventType_RecenterSuccess:
			case wvr::EventType_RecenterFail: {
				auto & pending = *reinterpret_cast<XrEventDataReferenceSpaceChangePending *>(event);
				pending.type = XR_TYPE_EVENT_DATA_REFERENCE_SPACE_CHANGE_PENDING;
				pending.session = inst->the_session ? handle_from<XrSession>(inst->the_session.get()) : XR_NULL_HANDLE;
				pending.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
				pending.changeTime = 0;
				pending.poseValid = true;
				pending.poseInPreviousSpace = XrPosef{
				        .orientation = XrQuaternionf{0, 0, 0, 1},
				        .position = XrVector3f{0, 0, 0},
				};
			}
				return XR_SUCCESS;

			case wvr::EventType_DeviceSuspend:
			case wvr::EventType_DeviceResume:
				if (inst->the_session)
				{
					auto & state = *reinterpret_cast<XrEventDataSessionStateChanged *>(event);
					state.type = XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED;
					state.session = handle_from<XrSession>(inst->the_session.get());
					// STOPPING (not IDLE): the client calls xrEndSession() on STOPPING, which
					// returns the shim session to the "ready" state so the DeviceResume ->
					// READY -> xrBeginSession() cycle succeeds.
					state.state = wave_event.common.type == wvr::EventType_DeviceSuspend
					                      ? XR_SESSION_STATE_STOPPING
					                      : XR_SESSION_STATE_READY;
					return XR_SUCCESS;
				}
				break;

			default:
				break;
		}
	}

	return XR_EVENT_UNAVAILABLE;
}
} // namespace wave
