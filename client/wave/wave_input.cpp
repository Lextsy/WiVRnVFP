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
#include <chrono>
#include <cstring>
#include <spdlog/spdlog.h>
#include <set>

namespace wave
{
namespace
{
XrTime now_ns()
{
	return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

// /user/hand/left -> Controller_Left, /user/hand/right -> Controller_Right,
// anything else is head-anchored (the HMD owns the input device).
wvr::DeviceType device_from_subaction(std::string_view path)
{
	if (path.find("left") != std::string_view::npos)
		return wvr::DeviceType_Controller_Left;
	if (path.find("right") != std::string_view::npos)
		return wvr::DeviceType_Controller_Right;
	return wvr::DeviceType_HMD;
}

wvr::DeviceType device_from_subaction(XrPath subaction)
{
	return device_from_subaction(path_registry::instance().get(subaction));
}

// Map the OpenXR action name to the Wave input id of the VIVE controller.
// The WiVRn client names actions after the full source path (e.g.
// user_hand_left_input_trigger_value) or after profile-local names (e.g.
// left_trigger), so match the component keyword as a substring.
wvr::InputId input_from_action_name(std::string_view name)
{
	static constexpr std::pair<std::string_view, wvr::InputId> table[] = {
	        {"thumbstick_click", wvr::InputId_Thumbstick},
	        {"thumbstick", wvr::InputId_Thumbstick},
	        {"trigger", wvr::InputId_Trigger},
	        {"squeeze", wvr::InputId_Grip},
	        {"grasp", wvr::InputId_Grip},
	        {"grip", wvr::InputId_Grip},
	        {"select", wvr::InputId_Trigger},
	        {"menu", wvr::InputId_Menu},
	        {"system", wvr::InputId_System},
	        {"volume_up", wvr::InputId_Volume_Up},
	        {"volume_down", wvr::InputId_Volume_Down},
	        {"touchpad", wvr::InputId_Touchpad},
	        {"trackpad", wvr::InputId_Touchpad},
	};

	for (auto [key, value]: table)
		if (name.find(key) != std::string_view::npos)
			return value;

	// Single-letter face buttons must match exactly, else every name
	// containing an "a" would map to Enter.
	static constexpr std::pair<std::string_view, wvr::InputId> exact[] = {
	        {"a", wvr::InputId_Enter},
	        {"b", wvr::InputId_Back},
	        {"x", wvr::InputId_Enter},
	        {"y", wvr::InputId_Back},
	};
	for (auto [key, value]: exact)
		if (name == key)
			return value;

	return wvr::InputId_Max;
}

std::string_view last_path_component(std::string_view path)
{
	size_t slash = path.find_last_of('/');
	return slash == std::string_view::npos ? path : path.substr(slash + 1);
}

// Poll every input device once per frame.
void poll_input(session & s)
{
	wvr::api * api = wvr::api::get();
	static constexpr wvr::DeviceType devices[3] = {
	        wvr::DeviceType_HMD,
	        wvr::DeviceType_Controller_Right,
	        wvr::DeviceType_Controller_Left,
	};

	for (wvr::DeviceType device: devices)
	{
		uint32_t index = unsigned(device);
		s.input_buttons[index] = 0;
		s.input_touches[index] = 0;
		s.input_analog_count[index] = 0;

		uint32_t count = api->GetInputTypeCount(device, wvr::InputType_Analog);
		count = std::min<uint32_t>(count, s.input_analog[index].size());
		if (api->GetInputDeviceState(device,
		                             unsigned(wvr::InputType_Button) | unsigned(wvr::InputType_Touch) | unsigned(wvr::InputType_Analog),
		                             &s.input_buttons[index],
		                             &s.input_touches[index],
		                             s.input_analog[index].data(),
		                             count))
			s.input_analog_count[index] = count;

		static uint32_t prev_buttons[4] = {};
		if (s.input_buttons[index] != prev_buttons[index])
		{
			spdlog::info("input change dev {}: btn {:x} -> {:x} touch {:x}", unsigned(device), prev_buttons[index], s.input_buttons[index], s.input_touches[index]);
			prev_buttons[index] = s.input_buttons[index];
		}
		static bool analog_logged[4] = {};
		if (s.input_analog_count[index] and not analog_logged[index])
		{
			analog_logged[index] = true;
			std::string ids;
			for (uint32_t i = 0; i < s.input_analog_count[index]; ++i)
				ids += std::to_string(unsigned(s.input_analog[index][i].id)) + " ";
			if (api->GetInputMappingTable and device == wvr::DeviceType_Controller_Right)
			{
				wvr::InputMappingPair table[32];
				uint32_t n = api->GetInputMappingTable(device, table, 32);
				for (uint32_t i = 0; i < n; ++i)
					spdlog::info("map pair {}: dst {} -> src {}", i, unsigned(table[i].destination.id), unsigned(table[i].source.id));
			}
			if (api->IsInputFocusCapturedBySystem)
				spdlog::info("input focus captured by system: {}", api->IsInputFocusCapturedBySystem());
		}
	}
}

const wvr::AnalogState * find_analog(const session & s, wvr::DeviceType device, wvr::InputId input)
{
	uint32_t index = unsigned(device);
	for (uint32_t i = 0; i < s.input_analog_count[index]; ++i)
		if (s.input_analog[index][i].id == input)
			return &s.input_analog[index][i];
	return nullptr;
}

// Resolve the Wave device/input binding of an action from the suggested
// interaction profile bindings.
void resolve_binding(session & s, action & a)
{
	a.device = wvr::DeviceType_Invalid;
	a.input = wvr::InputId_Max;
	a.haptic_device = wvr::DeviceType_Invalid;

	auto profile_it = s.bindings.find(s.current_interaction_profile);
	if (profile_it == s.bindings.end())
		return;

	auto binding_it = profile_it->second.find(a.path);
	if (binding_it == profile_it->second.end() or binding_it->second.empty())
		return;

	a.device = device_from_subaction(binding_it->second.front());
	if (a.kind == 4)
	{
		a.haptic_device = a.device;
		a.device = wvr::DeviceType_Invalid;
		return;
	}
	// Derive the Wave input from the bound source path (e.g.
	// /user/hand/left/input/menu/click), not the action name: the VFP lobby
	// binds squeeze/value to menu/click, and naming alone would map squeeze
	// to the (nonexistent) grip button.
	a.input = input_from_action_name(path_registry::instance().get(binding_it->second.front()));
	static std::set<void *> logged;
	if (logged.insert(&a).second)
		spdlog::info("resolve_binding: name='{}' kind={} dev={} input={}", path_registry::instance().get(a.path), int(a.kind), unsigned(a.device), unsigned(a.input));
}
} // namespace

// Wave documents that WVR_GetInputDeviceState and the input event queue stay empty unless
// the app declares the input ids it wants, after WVR_Init. The ids below are the Wave
// "destination" ids the key-mapping policy fills from the connected controller's keys, i.e.
// exactly the ids input_from_action_name() maps OpenXR actions to.
void request_wave_inputs()
{
	static constexpr wvr::InputAttribute controller_requests[] = {
	        {wvr::InputId_Trigger, wvr::InputType_Button | wvr::InputType_Analog, wvr::AnalogType_1D},
	        {wvr::InputId_Thumbstick, wvr::InputType_Button | wvr::InputType_Touch | wvr::InputType_Analog, wvr::AnalogType_2D},
	        {wvr::InputId_Touchpad, wvr::InputType_Button | wvr::InputType_Touch | wvr::InputType_Analog, wvr::AnalogType_2D},
	        {wvr::InputId_Grip, wvr::InputType_Button | wvr::InputType_Touch, wvr::AnalogType_None},
	        {wvr::InputId_Menu, wvr::InputType_Button | wvr::InputType_Touch, wvr::AnalogType_None},
	        {wvr::InputId_Enter, wvr::InputType_Button | wvr::InputType_Touch, wvr::AnalogType_None},
	        {wvr::InputId_Back, wvr::InputType_Button | wvr::InputType_Touch, wvr::AnalogType_None},
	};
	static constexpr wvr::InputAttribute hmd_requests[] = {
	        {wvr::InputId_Touchpad, wvr::InputType_Button | wvr::InputType_Touch | wvr::InputType_Analog, wvr::AnalogType_2D},
	        {wvr::InputId_Enter, wvr::InputType_Button | wvr::InputType_Touch, wvr::AnalogType_None},
	        {wvr::InputId_Back, wvr::InputType_Button | wvr::InputType_Touch, wvr::AnalogType_None},
	};

	wvr::api * api = wvr::api::get();

	if (not api->SetInputRequest(wvr::DeviceType_HMD, hmd_requests, uint32_t(std::size(hmd_requests))))
		spdlog::warn("WaveVR: WVR_SetInputRequest(HMD) rejected");

	for (wvr::DeviceType device: {wvr::DeviceType_Controller_Left, wvr::DeviceType_Controller_Right})
		if (not api->SetInputRequest(device, controller_requests, uint32_t(std::size(controller_requests))))
			spdlog::warn("WaveVR: WVR_SetInputRequest(controller {}) rejected", int(device));
}

action * action_from(XrAction handle)
{
	return object_from_handle<action>(reinterpret_cast<uintptr_t>(handle), action_magic);
}

action_set * action_set_from(XrActionSet handle)
{
	return object_from_handle<action_set>(reinterpret_cast<uintptr_t>(handle), action_set_magic);
}

XrResult create_action_set(XrInstance handle, const XrActionSetCreateInfo * create_info, XrActionSet * actionset_handle)
{
	if (not instance_from(handle))
		return XR_ERROR_HANDLE_INVALID;

	auto * inst = instance_from(handle);
	if (not inst->the_session)
		return XR_ERROR_HANDLE_INVALID;

	auto set = std::make_unique<action_set>(*inst->the_session);
	set->name = create_info->actionSetName;
	set->localized_name = create_info->localizedActionSetName;
	set->priority = create_info->priority;

	*actionset_handle = handle_from<XrActionSet>(set.get());
	inst->the_session->action_sets.push_back(std::move(set));
	return XR_SUCCESS;
}

XrResult destroy_action_set(XrActionSet handle)
{
	auto * set = action_set_from(handle);
	if (not set)
		return XR_ERROR_HANDLE_INVALID;
	// Erase only this set: erase(first, end()) would destroy every action set
	// created after it, invalidating handles the client still uses.
	std::erase_if(set->session.action_sets, [set](const std::unique_ptr<action_set> & s) { return s.get() == set; });
	return XR_SUCCESS;
}

XrResult create_action(XrActionSet set_handle, const XrActionCreateInfo * create_info, XrAction * action_handle)
{
	auto * set = action_set_from(set_handle);
	if (not set)
		return XR_ERROR_HANDLE_INVALID;

	auto a = std::make_unique<action>(*set);
	a->name = create_info->actionName;
	a->path = path_registry::instance().intern(create_info->actionName);
	a->subaction_paths.assign(create_info->subactionPaths, create_info->subactionPaths + create_info->countSubactionPaths);

	switch (create_info->actionType)
	{
		case XR_ACTION_TYPE_BOOLEAN_INPUT:
			a->kind = 0;
			break;
		case XR_ACTION_TYPE_FLOAT_INPUT:
			a->kind = 1;
			break;
		case XR_ACTION_TYPE_VECTOR2F_INPUT:
			a->kind = 2;
			break;
		case XR_ACTION_TYPE_POSE_INPUT:
			a->kind = 3;
			break;
		case XR_ACTION_TYPE_VIBRATION_OUTPUT:
			// apply_haptic_feedback resolves the device from the binding;
			// no state to track.
			a->kind = 4;
			break;
		default:
			return XR_ERROR_ACTION_TYPE_MISMATCH;
	}

	*action_handle = handle_from<XrAction>(a.get());
	set->actions.push_back(std::move(a));
	return XR_SUCCESS;
}

XrResult attach_actionsets(XrSession handle, const XrSessionActionSetsAttachInfo *)
{
	auto * s = session_from(handle);
	if (not s)
		return XR_ERROR_HANDLE_INVALID;
	s->actionsets_attached = true;
	return XR_SUCCESS;
}

XrResult suggest_interaction_profile_bindings(XrInstance instance_handle, const XrInteractionProfileSuggestedBinding * suggested)
{
	auto * inst = instance_from(instance_handle);
	if (not inst)
		return XR_ERROR_HANDLE_INVALID;
	auto * s = inst->the_session.get();
	if (not s)
		return XR_ERROR_HANDLE_INVALID;

	XrPath profile = suggested->interactionProfile;
	for (uint32_t i = 0; i < suggested->countSuggestedBindings; ++i)
	{
		const auto & binding = suggested->suggestedBindings[i];
		auto * a = action_from(binding.action);
		if (not a)
			return XR_ERROR_HANDLE_INVALID;
		auto & targets = s->bindings[profile][a->path];
		targets.push_back(binding.binding);
	}

	if (s->current_interaction_profile == 0)
		s->current_interaction_profile = profile;

	return XR_SUCCESS;
}

XrResult get_current_interaction_profile(XrSession handle, XrPath, XrInteractionProfileState * state)
{
	auto * s = session_from(handle);
	if (not s)
		return XR_ERROR_HANDLE_INVALID;

	state->type = XR_TYPE_INTERACTION_PROFILE_STATE;
	state->interactionProfile = s->current_interaction_profile;
	return XR_SUCCESS;
}

XrResult create_action_space(XrSession handle, const XrActionSpaceCreateInfo * create_info, XrSpace * space_handle)
{
	auto * s = session_from(handle);
	if (not s)
		return XR_ERROR_HANDLE_INVALID;

	auto * a = action_from(create_info->action);
	if (not a)
		return XR_ERROR_HANDLE_INVALID;

	auto sp = std::make_unique<space>(*s);
	// The WiVRn client encodes the hand in the action name (e.g.
	// user_hand_left_input_grip_pose) and passes a null subaction path;
	// derive the Wave device from whichever carries the hand, else every
	// controller space falls back to the HMD pose and the controllers are
	// rendered inside the head.
	std::string_view subaction = path_registry::instance().get(create_info->subactionPath);
	std::string_view action_name = last_path_component(path_registry::instance().get(a->path));
	sp->device = device_from_subaction(create_info->subactionPath == XR_NULL_PATH ? action_name : subaction);
	spdlog::info("create_action_space: sub='{}' name='{}' dev={}", subaction, action_name, unsigned(sp->device));
	// The Wave pose is the aim pose; the grip pose is offset a bit below the
	// controller. Refine on-device (Phase 5).
	if (action_name.find("input_grip_pose") != std::string_view::npos or action_name.find("squeeze") != std::string_view::npos or action_name.find("grasp") != std::string_view::npos)
		sp->offset = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, -0.05f, 0.0f));
	sp->transform = glm::mat4(1.0f);

	*space_handle = handle_from<XrSpace>(sp.get());
	s->spaces.push_back(std::move(sp));
	return XR_SUCCESS;
}

XrResult sync_actions(XrSession handle, const XrActionsSyncInfo * info)
{
	auto * s = session_from(handle);
	if (not s)
		return XR_ERROR_HANDLE_INVALID;

	poll_input(*s);

	for (uint32_t i = 0; i < info->countActiveActionSets; ++i)
	{
		auto * set = action_set_from(info->activeActionSets[i].actionSet);
		if (not set)
			return XR_ERROR_HANDLE_INVALID;

		for (auto & a: set->actions)
		{
			resolve_binding(*s, *a);
			if (a->kind == 3)
			{
				// The controller pose comes from xrLocateSpace on the action
				// space; Wave has no per-action pose state. Only claim an
				// active pose when the device is actually connected, else the
				// client renders the controller model at the invalid identity
				// pose (inside the head).
				a->pose.isActive = a->device != wvr::DeviceType_Invalid &&
				                   (not wvr::api::get()->IsDeviceConnected ||
				                    wvr::api::get()->IsDeviceConnected(a->device));
				continue;
			}
			if (a->device == wvr::DeviceType_Invalid or a->input == wvr::InputId_Max)
				continue;

			uint32_t device_index = unsigned(a->device);
			uint32_t bit = 1u << unsigned(a->input);

			switch (a->kind)
			{
				case 0: {
					bool value = (s->input_buttons[device_index] & bit) != 0;
					a->boolean.changedSinceLastSync = value != a->boolean.currentState;
					a->boolean.currentState = value;
					a->boolean.isActive = true;
					if (a->boolean.changedSinceLastSync)
						a->boolean.lastChangeTime = now_ns();
				}
				break;

				case 1: {
					const wvr::AnalogState * analog = find_analog(*s, a->device, a->input);
					// simple_controller binds float actions to button paths
					// (select/click, menu/click); Wave reports those as button
					// bits only, so synthesise a 0/1 float from the bit.
					float value = analog ? analog->axis.x : ((s->input_buttons[device_index] & bit) ? 1.0f : 0.0f);
					a->float_state.changedSinceLastSync = value != a->float_state.currentState;
					a->float_state.currentState = value;
					a->float_state.isActive = true;
					if (a->float_state.changedSinceLastSync)
						a->float_state.lastChangeTime = now_ns();
				}
				break;

				case 2: {
					const wvr::AnalogState * analog = find_analog(*s, a->device, a->input);
					XrVector2f value = analog ? XrVector2f{analog->axis.x, analog->axis.y} : XrVector2f{0, 0};
					a->vector2.changedSinceLastSync = value.x != a->vector2.currentState.x || value.y != a->vector2.currentState.y;
					a->vector2.currentState = value;
					a->vector2.isActive = analog != nullptr;
					if (a->vector2.changedSinceLastSync)
						a->vector2.lastChangeTime = now_ns();
				}
				break;

				default:
					break;
			}
		}
	}

	return XR_SUCCESS;
}

XrResult get_action_state_boolean(XrSession handle, const XrActionStateGetInfo * info, XrActionStateBoolean * state)
{
	auto * s = session_from(handle);
	auto * a = action_from(info->action);
	if (not s or not a)
		return XR_ERROR_HANDLE_INVALID;
	if (a->kind != 0)
		return XR_ERROR_ACTION_TYPE_MISMATCH;

	*state = a->boolean;
	state->type = XR_TYPE_ACTION_STATE_BOOLEAN;
	return XR_SUCCESS;
}

XrResult get_action_state_float(XrSession handle, const XrActionStateGetInfo * info, XrActionStateFloat * state)
{
	auto * s = session_from(handle);
	auto * a = action_from(info->action);
	if (not s or not a)
		return XR_ERROR_HANDLE_INVALID;
	if (a->kind != 1)
		return XR_ERROR_ACTION_TYPE_MISMATCH;

	*state = a->float_state;
	state->type = XR_TYPE_ACTION_STATE_FLOAT;
	return XR_SUCCESS;
}

XrResult get_action_state_vector2f(XrSession handle, const XrActionStateGetInfo * info, XrActionStateVector2f * state)
{
	auto * s = session_from(handle);
	auto * a = action_from(info->action);
	if (not s or not a)
		return XR_ERROR_HANDLE_INVALID;
	if (a->kind != 2)
		return XR_ERROR_ACTION_TYPE_MISMATCH;

	*state = a->vector2;
	state->type = XR_TYPE_ACTION_STATE_VECTOR2F;
	return XR_SUCCESS;
}

XrResult get_action_state_pose(XrSession handle, const XrActionStateGetInfo * info, XrActionStatePose * state)
{
	auto * s = session_from(handle);
	auto * a = action_from(info->action);
	if (not s or not a)
		return XR_ERROR_HANDLE_INVALID;
	if (a->kind != 3)
		return XR_ERROR_ACTION_TYPE_MISMATCH;

	*state = a->pose;
	state->type = XR_TYPE_ACTION_STATE_POSE;
	return XR_SUCCESS;
}

XrResult enumerate_bound_sources_for_action(XrSession handle, const XrBoundSourcesForActionEnumerateInfo * info, uint32_t capacity, uint32_t * count, XrPath * sources)
{
	auto * s = session_from(handle);
	auto * a = action_from(info->action);
	if (not s or not a)
		return XR_ERROR_HANDLE_INVALID;

	if (capacity == 0)
	{
		*count = static_cast<uint32_t>(a->subaction_paths.size());
		return XR_SUCCESS;
	}

	*count = std::min<uint32_t>(capacity, static_cast<uint32_t>(a->subaction_paths.size()));
	std::copy_n(a->subaction_paths.begin(), *count, sources);
	return XR_SUCCESS;
}

XrResult get_input_source_localized_name(XrSession handle, const XrInputSourceLocalizedNameGetInfo * info, uint32_t capacity, uint32_t * count, char * buffer)
{
	if (not session_from(handle))
		return XR_ERROR_HANDLE_INVALID;

	std::string_view path = path_registry::instance().get(info->sourcePath);
	std::string_view name = "Controller";
	if (path.find("left") != std::string_view::npos)
		name = "Left controller";
	else if (path.find("right") != std::string_view::npos)
		name = "Right controller";

	*count = static_cast<uint32_t>(name.size());
	if (capacity == 0)
		return XR_SUCCESS;
	if (capacity <= name.size())
		return XR_ERROR_SIZE_INSUFFICIENT;
	std::memcpy(buffer, name.data(), name.size());
	buffer[name.size()] = '\0';
	return XR_SUCCESS;
}

XrResult apply_haptic_feedback(XrSession handle, const XrHapticActionInfo * info, const XrHapticBaseHeader * haptic)
{
	if (not session_from(handle))
		return XR_ERROR_HANDLE_INVALID;

	auto * a = action_from(info->action);
	if (not a)
		return XR_ERROR_HANDLE_INVALID;

	if (haptic->type != XR_TYPE_HAPTIC_VIBRATION)
		return XR_ERROR_FEATURE_UNSUPPORTED;

	// Wave 3.2.0 has no vibration API; WVR_TriggerVibration appeared in Wave 4.x.
	if (not wvr::api::get()->TriggerVibration)
		return XR_ERROR_FEATURE_UNSUPPORTED;

	const auto & vibration = *reinterpret_cast<const XrHapticVibration *>(haptic);
	wvr::api::get()->TriggerVibration(
	        a->haptic_device,
	        wvr::InputId_Trigger,
	        vibration.duration > 0 ? uint32_t(vibration.duration / 1000) : 65535,
	        vibration.frequency > 0 ? uint32_t(vibration.frequency) : 1,
	        vibration.amplitude > 0.5f ? wvr::Intensity_Strong : wvr::Intensity_Normal);
	return XR_SUCCESS;
}

XrResult stop_haptic_feedback(XrSession handle, const XrHapticActionInfo * info)
{
	if (not session_from(handle))
		return XR_ERROR_HANDLE_INVALID;

	auto * a = action_from(info->action);
	if (not a)
		return XR_ERROR_HANDLE_INVALID;

	if (not wvr::api::get()->TriggerVibration)
		return XR_ERROR_FEATURE_UNSUPPORTED;

	wvr::api::get()->TriggerVibration(a->haptic_device, wvr::InputId_Trigger, 0, 1, wvr::Intensity_Normal);
	return XR_SUCCESS;
}
} // namespace wave
