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

#include "wave_api.h"
#include <dlfcn.h>
#include <spdlog/spdlog.h>

namespace
{
// Function pointers and void* have the same representation on every ABI
// Android uses, but the standard does not allow casting between them.
template <typename T>
bool load_symbol(void * handle, T & destination, const char * name)
{
	void * symbol = dlsym(handle, name);
	if (not symbol)
	{
		spdlog::error("WaveVR: missing symbol {}", name);
		return false;
	}

	*reinterpret_cast<T *>(&destination) = *reinterpret_cast<T *>(&symbol);
	return true;
}
} // namespace

wvr::api * wvr::api::get()
{
	static api instance;
	return &instance;
}

wvr::api * wvr::api::load()
{
	api * instance = get();

	if (instance->dl_handle)
		return instance;

	// The Wave client library speaks a protocol that has to match the Wave service on the
	// device, and the system copy is by construction matched to its own runtime. Prefer it
	// over the copy bundled in the APK; a bare soname resolves from the app's native-lib
	// directory first, so the bundled library stays the fallback (and the only option on
	// Android versions that restrict absolute dlopen to public libraries).
	static constexpr const char * const candidates[] = {
	        "/system/lib64/libwvr_api.so",
	        "/vendor/lib64/libwvr_api.so",
	        "libwvr_api.so",
	};

	void * handle = nullptr;
	const char * loaded_from = nullptr;
	for (const char * const candidate: candidates)
	{
		handle = dlopen(candidate, RTLD_NOW | RTLD_LOCAL);
		if (handle)
		{
			loaded_from = candidate;
			break;
		}
	}

	if (not handle)
	{
		spdlog::error("WaveVR: cannot load libwvr_api.so: {}", dlerror());
		return nullptr;
	}

	spdlog::info("WaveVR: loaded Wave client library from {}", loaded_from);

	// The stub resolves its entry points lazily: every WVR_* function branches
	// through a pointer obtained from dlsym(handle, name), where handle comes
	// from its own dlopen("libwvr_runtime.so"). That library lives in a system
	// app's private directory, which is not on the app's library search path,
	// so the stub's dlopen fails and dlsym(NULL, name) returns a non-null
	// garbage pointer; the trampoline then branches to it and faults. Load the
	// runtime (bundled next to the stub) explicitly so the resolution
	// succeeds, and report a missing runtime cleanly instead of faulting.
	if (not dlopen("libwvr_runtime.so", RTLD_NOW | RTLD_GLOBAL))
	{
		spdlog::error("WaveVR: cannot load libwvr_runtime.so: {}", dlerror());
		dlclose(handle);
		return nullptr;
	}

	api & i = *instance;
	bool ok = true;

#define WVR_FUNCTION(name) ok &= load_symbol(handle, i.name, "WVR_" #name);

	WVR_FUNCTION(RegisterMain)
	WVR_FUNCTION(Init)
	WVR_FUNCTION(Quit)
	WVR_FUNCTION(GetWaveRuntimeVersion)
	WVR_FUNCTION(GetWaveSDKVersion)
	WVR_FUNCTION(GetInitErrorString)
	WVR_FUNCTION(IsDeviceConnected)
	WVR_FUNCTION(GetDegreeOfFreedom)
	WVR_FUNCTION(InAppRecenter)
	WVR_FUNCTION(RenderInit)
	WVR_FUNCTION(GetRenderProps)
	WVR_FUNCTION(GetRenderTargetSize)
	WVR_FUNCTION(ObtainTextureQueue)
	WVR_FUNCTION(GetTextureQueueLength)
	WVR_FUNCTION(GetAvailableTextureIndex)
	WVR_FUNCTION(GetTexture)
	WVR_FUNCTION(ReleaseTextureQueue)
	WVR_FUNCTION(SubmitFrame)
	WVR_FUNCTION(VulkanInit)
	WVR_FUNCTION(GetVkImage)
	WVR_FUNCTION(GetProjection)
	WVR_FUNCTION(GetTransformFromEyeToHead)
	WVR_FUNCTION(GetSyncPose)
	WVR_FUNCTION(GetPoseState)
	WVR_FUNCTION(PollEventQueue)
	WVR_FUNCTION(GetInputDeviceCapability)
	WVR_FUNCTION(GetInputDeviceAnalogType)
	WVR_FUNCTION(GetInputTypeCount)
	WVR_FUNCTION(GetInputDeviceState)
	WVR_FUNCTION(GetInputButtonState)
	WVR_FUNCTION(GetInputTouchState)
	WVR_FUNCTION(GetInputAnalogAxis)
	WVR_FUNCTION(SetInputRequest)
#undef WVR_FUNCTION

	// WVR_TriggerVibration appeared in Wave 4.x; its absence is not fatal.
	if (dlsym(handle, "WVR_TriggerVibration"))
		load_symbol(handle, i.TriggerVibration, "WVR_TriggerVibration");
	else
		spdlog::warn("WaveVR: WVR_TriggerVibration not available, haptics disabled");

	// Key-mapping introspection; absence is not fatal.
	if (dlsym(handle, "WVR_GetInputMappingTable"))
		load_symbol(handle, i.GetInputMappingTable, "WVR_GetInputMappingTable");
	if (dlsym(handle, "WVR_IsInputFocusCapturedBySystem"))
		load_symbol(handle, i.IsInputFocusCapturedBySystem, "WVR_IsInputFocusCapturedBySystem");

	if (not ok)
		return nullptr;

	i.dl_handle = handle;

	if (auto version = i.GetWaveRuntimeVersion())
		spdlog::info("WaveVR: runtime version {}", version);

	return instance;
}

wvr::api::~api()
{
	if (dl_handle)
		dlclose(dl_handle);
}
