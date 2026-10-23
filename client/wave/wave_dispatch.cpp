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

// Generated dispatch table: the OpenXR entry points exported by the WaveVR shim.
// Functions the VIVE Focus Plus does not support return XR_ERROR_FUNCTION_UNSUPPORTED;
// they are exported so the client links against the shim in place of the OpenXR loader.
// Entry points that the pinned OpenXR headers do not declare (vendor extensions the
// client guards with #ifdef) are skipped entirely.

#include "wave_runtime.h"
#include <string_view>
#include <unordered_map>
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

extern "C"
{
	XrResult XRAPI_PTR xrAcquireSwapchainImage(XrSwapchain swapchain, const XrSwapchainImageAcquireInfo * acquireInfo, uint32_t * index)
	{
		return wave::acquire_swapchain_image(swapchain, acquireInfo, index);
	}
	XrResult XRAPI_PTR xrApplyHapticFeedback(XrSession session, const XrHapticActionInfo * hapticActionInfo, const XrHapticBaseHeader * hapticFeedback)
	{
		return wave::apply_haptic_feedback(session, hapticActionInfo, hapticFeedback);
	}
	XrResult XRAPI_PTR xrAttachSessionActionSets(XrSession session, const XrSessionActionSetsAttachInfo * attachInfo)
	{
		return wave::attach_actionsets(session, attachInfo);
	}
	XrResult XRAPI_PTR xrBeginFrame(XrSession session, const XrFrameBeginInfo * frameBeginInfo)
	{
		return wave::begin_frame(session, frameBeginInfo);
	}
	XrResult XRAPI_PTR xrBeginSession(XrSession session, const XrSessionBeginInfo * beginInfo)
	{
		return wave::begin_session(session, beginInfo);
	}
	XrResult XRAPI_PTR xrConvertTimespecTimeToTimeKHR(XrInstance instance, const struct timespec * timespecTime, XrTime * time)
	{
		return wave::convert_timespec_time_to_time_khr(instance, timespecTime, time);
	}
	XrResult XRAPI_PTR xrCreateAction(XrActionSet actionSet, const XrActionCreateInfo * createInfo, XrAction * action)
	{
		return wave::create_action(actionSet, createInfo, action);
	}
	XrResult XRAPI_PTR xrCreateActionSet(XrInstance instance, const XrActionSetCreateInfo * createInfo, XrActionSet * actionSet)
	{
		return wave::create_action_set(instance, createInfo, actionSet);
	}
	XrResult XRAPI_PTR xrCreateActionSpace(XrSession session, const XrActionSpaceCreateInfo * createInfo, XrSpace * space)
	{
		return wave::create_action_space(session, createInfo, space);
	}
	XrResult XRAPI_PTR xrCreateBodyTrackerBD(XrSession session, const XrBodyTrackerCreateInfoBD * createInfo, XrBodyTrackerBD * bodyTracker)
	{
		(void)session;
		(void)createInfo;
		(void)bodyTracker;
		return XR_ERROR_FUNCTION_UNSUPPORTED;
	}
	XrResult XRAPI_PTR xrCreateBodyTrackerFB(XrSession session, const XrBodyTrackerCreateInfoFB * createInfo, XrBodyTrackerFB * bodyTracker)
	{
		(void)session;
		(void)createInfo;
		(void)bodyTracker;
		return XR_ERROR_FUNCTION_UNSUPPORTED;
	}
	XrResult XRAPI_PTR xrCreateDebugUtilsMessengerEXT(XrInstance instance, const XrDebugUtilsMessengerCreateInfoEXT * createInfo, XrDebugUtilsMessengerEXT * messenger)
	{
		(void)instance;
		(void)createInfo;
		(void)messenger;
		return XR_ERROR_FUNCTION_UNSUPPORTED;
	}
	XrResult XRAPI_PTR xrCreateFaceTracker2FB(XrSession session, const XrFaceTrackerCreateInfo2FB * createInfo, XrFaceTracker2FB * faceTracker)
	{
		(void)session;
		(void)createInfo;
		(void)faceTracker;
		return XR_ERROR_FUNCTION_UNSUPPORTED;
	}
	XrResult XRAPI_PTR xrCreateFaceTrackerANDROID(XrSession session, const XrFaceTrackerCreateInfoANDROID * createInfo, XrFaceTrackerANDROID * faceTracker)
	{
		(void)session;
		(void)createInfo;
		(void)faceTracker;
		return XR_ERROR_FUNCTION_UNSUPPORTED;
	}
	XrResult XRAPI_PTR xrCreateFacialTrackerHTC(XrSession session, const XrFacialTrackerCreateInfoHTC * createInfo, XrFacialTrackerHTC * facialTracker)
	{
		(void)session;
		(void)createInfo;
		(void)facialTracker;
		return XR_ERROR_FUNCTION_UNSUPPORTED;
	}
	XrResult XRAPI_PTR xrCreateHandTrackerEXT(XrSession session, const XrHandTrackerCreateInfoEXT * createInfo, XrHandTrackerEXT * handTracker)
	{
		(void)session;
		(void)createInfo;
		(void)handTracker;
		return XR_ERROR_FUNCTION_UNSUPPORTED;
	}
	XrResult XRAPI_PTR xrCreateInstance(const XrInstanceCreateInfo * createInfo, XrInstance * instance)
	{
		return wave::create_instance(createInfo, instance);
	}
	XrResult XRAPI_PTR xrCreatePassthroughFB(XrSession session, const XrPassthroughCreateInfoFB * createInfo, XrPassthroughFB * outPassthrough)
	{
		(void)session;
		(void)createInfo;
		(void)outPassthrough;
		return XR_ERROR_FUNCTION_UNSUPPORTED;
	}
	XrResult XRAPI_PTR xrCreatePassthroughHTC(XrSession session, const XrPassthroughCreateInfoHTC * createInfo, XrPassthroughHTC * passthrough)
	{
		(void)session;
		(void)createInfo;
		(void)passthrough;
		return XR_ERROR_FUNCTION_UNSUPPORTED;
	}
	XrResult XRAPI_PTR xrCreatePassthroughLayerFB(XrSession session, const XrPassthroughLayerCreateInfoFB * createInfo, XrPassthroughLayerFB * outLayer)
	{
		(void)session;
		(void)createInfo;
		(void)outLayer;
		return XR_ERROR_FUNCTION_UNSUPPORTED;
	}
	XrResult XRAPI_PTR xrCreateReferenceSpace(XrSession session, const XrReferenceSpaceCreateInfo * createInfo, XrSpace * space)
	{
		return wave::create_reference_space(session, createInfo, space);
	}
	XrResult XRAPI_PTR xrCreateSession(XrInstance instance, const XrSessionCreateInfo * createInfo, XrSession * session)
	{
		return wave::create_session(instance, createInfo, session);
	}
	XrResult XRAPI_PTR xrCreateSwapchain(XrSession session, const XrSwapchainCreateInfo * createInfo, XrSwapchain * swapchain)
	{
		return wave::create_swapchain(session, createInfo, swapchain);
	}
	XrResult XRAPI_PTR xrCreateVulkanDeviceKHR(XrInstance instance, const XrVulkanDeviceCreateInfoKHR * createInfo, VkDevice * vulkanDevice, VkResult * vulkanResult)
	{
		return wave::create_vulkan_device_khr(instance, createInfo, vulkanDevice, vulkanResult);
	}
	XrResult XRAPI_PTR xrCreateVulkanInstanceKHR(XrInstance instance, const XrVulkanInstanceCreateInfoKHR * createInfo, VkInstance * vulkanInstance, VkResult * vulkanResult)
	{
		return wave::create_vulkan_instance_khr(instance, createInfo, vulkanInstance, vulkanResult);
	}
	XrResult XRAPI_PTR xrDestroyActionSet(XrActionSet actionSet)
	{
		return wave::destroy_action_set(actionSet);
	}
	XrResult XRAPI_PTR xrDestroyBodyTrackerBD(XrBodyTrackerBD bodyTracker)
	{
		(void)bodyTracker;
		return XR_ERROR_FUNCTION_UNSUPPORTED;
	}
	XrResult XRAPI_PTR xrDestroyBodyTrackerFB(XrBodyTrackerFB bodyTracker)
	{
		(void)bodyTracker;
		return XR_ERROR_FUNCTION_UNSUPPORTED;
	}
	XrResult XRAPI_PTR xrDestroyFaceTracker2FB(XrFaceTracker2FB faceTracker)
	{
		(void)faceTracker;
		return XR_ERROR_FUNCTION_UNSUPPORTED;
	}
	XrResult XRAPI_PTR xrDestroyFaceTrackerANDROID(XrFaceTrackerANDROID faceTracker)
	{
		(void)faceTracker;
		return XR_ERROR_FUNCTION_UNSUPPORTED;
	}
	XrResult XRAPI_PTR xrDestroyHandTrackerEXT(XrHandTrackerEXT handTracker)
	{
		(void)handTracker;
		return XR_ERROR_FUNCTION_UNSUPPORTED;
	}
	XrResult XRAPI_PTR xrDestroyInstance(XrInstance instance)
	{
		return wave::destroy_instance(instance);
	}
	XrResult XRAPI_PTR xrDestroyPassthroughFB(XrPassthroughFB passthrough)
	{
		(void)passthrough;
		return XR_ERROR_FUNCTION_UNSUPPORTED;
	}
	XrResult XRAPI_PTR xrDestroyPassthroughHTC(XrPassthroughHTC passthrough)
	{
		(void)passthrough;
		return XR_ERROR_FUNCTION_UNSUPPORTED;
	}
	XrResult XRAPI_PTR xrDestroyPassthroughLayerFB(XrPassthroughLayerFB layer)
	{
		(void)layer;
		return XR_ERROR_FUNCTION_UNSUPPORTED;
	}
	XrResult XRAPI_PTR xrDestroySession(XrSession session)
	{
		return wave::destroy_session(session);
	}
	XrResult XRAPI_PTR xrDestroySpace(XrSpace space)
	{
		return wave::destroy_space(space);
	}
	XrResult XRAPI_PTR xrDestroySwapchain(XrSwapchain swapchain)
	{
		return wave::destroy_swapchain(swapchain);
	}
	XrResult XRAPI_PTR xrEndFrame(XrSession session, const XrFrameEndInfo * frameEndInfo)
	{
		return wave::end_frame(session, frameEndInfo);
	}
	XrResult XRAPI_PTR xrEndSession(XrSession session)
	{
		return wave::end_session(session);
	}
	XrResult XRAPI_PTR xrEnumerateApiLayerProperties(uint32_t propertyCapacityInput, uint32_t * propertyCountOutput, XrApiLayerProperties * properties)
	{
		return wave::enumerate_api_layer_properties(propertyCapacityInput, propertyCountOutput, properties);
	}
	XrResult XRAPI_PTR xrEnumerateBoundSourcesForAction(XrSession session, const XrBoundSourcesForActionEnumerateInfo * enumerateInfo, uint32_t sourceCapacityInput, uint32_t * sourceCountOutput, XrPath * sources)
	{
		return wave::enumerate_bound_sources_for_action(session, enumerateInfo, sourceCapacityInput, sourceCountOutput, sources);
	}
	XrResult XRAPI_PTR xrEnumerateDisplayRefreshRatesFB(XrSession session, uint32_t displayRefreshRateCapacityInput, uint32_t * displayRefreshRateCountOutput, float * displayRefreshRates)
	{
		return wave::enumerate_display_refresh_rates_fb(session, displayRefreshRateCapacityInput, displayRefreshRateCountOutput, displayRefreshRates);
	}
	XrResult XRAPI_PTR xrEnumerateEnvironmentBlendModes(XrInstance instance, XrSystemId systemId, XrViewConfigurationType viewConfigurationType, uint32_t environmentBlendModeCapacityInput, uint32_t * environmentBlendModeCountOutput, XrEnvironmentBlendMode * environmentBlendModes)
	{
		return wave::enumerate_environment_blend_modes(instance, systemId, viewConfigurationType, environmentBlendModeCapacityInput, environmentBlendModeCountOutput, environmentBlendModes);
	}
	XrResult XRAPI_PTR xrEnumerateInstanceExtensionProperties(const char * layerName, uint32_t propertyCapacityInput, uint32_t * propertyCountOutput, XrExtensionProperties * properties)
	{
		return wave::enumerate_instance_extension_properties(layerName, propertyCapacityInput, propertyCountOutput, properties);
	}
	XrResult XRAPI_PTR xrEnumerateReferenceSpaces(XrSession session, uint32_t spaceCapacityInput, uint32_t * spaceCountOutput, XrReferenceSpaceType * spaces)
	{
		return wave::enumerate_reference_spaces(session, spaceCapacityInput, spaceCountOutput, spaces);
	}
	XrResult XRAPI_PTR xrEnumerateSwapchainFormats(XrSession session, uint32_t formatCapacityInput, uint32_t * formatCountOutput, int64_t * formats)
	{
		return wave::enumerate_swapchain_formats(session, formatCapacityInput, formatCountOutput, formats);
	}
	XrResult XRAPI_PTR xrEnumerateSwapchainImages(XrSwapchain swapchain, uint32_t imageCapacityInput, uint32_t * imageCountOutput, XrSwapchainImageBaseHeader * images)
	{
		return wave::enumerate_swapchain_images(swapchain, imageCapacityInput, imageCountOutput, images);
	}
	XrResult XRAPI_PTR xrEnumerateViewConfigurationViews(XrInstance instance, XrSystemId systemId, XrViewConfigurationType viewConfigurationType, uint32_t viewCapacityInput, uint32_t * viewCountOutput, XrViewConfigurationView * views)
	{
		return wave::enumerate_view_configuration_views(instance, systemId, viewConfigurationType, viewCapacityInput, viewCountOutput, views);
	}
	XrResult XRAPI_PTR xrEnumerateViewConfigurations(XrInstance instance, XrSystemId systemId, uint32_t viewConfigurationTypeCapacityInput, uint32_t * viewConfigurationTypeCountOutput, XrViewConfigurationType * viewConfigurationTypes)
	{
		return wave::enumerate_view_configurations(instance, systemId, viewConfigurationTypeCapacityInput, viewConfigurationTypeCountOutput, viewConfigurationTypes);
	}
	XrResult XRAPI_PTR xrGetActionStateBoolean(XrSession session, const XrActionStateGetInfo * getInfo, XrActionStateBoolean * state)
	{
		return wave::get_action_state_boolean(session, getInfo, state);
	}
	XrResult XRAPI_PTR xrGetActionStateFloat(XrSession session, const XrActionStateGetInfo * getInfo, XrActionStateFloat * state)
	{
		return wave::get_action_state_float(session, getInfo, state);
	}
	XrResult XRAPI_PTR xrGetActionStateVector2f(XrSession session, const XrActionStateGetInfo * getInfo, XrActionStateVector2f * state)
	{
		return wave::get_action_state_vector2f(session, getInfo, state);
	}
	XrResult XRAPI_PTR xrGetActionStatePose(XrSession session, const XrActionStateGetInfo * getInfo, XrActionStatePose * state)
	{
		return wave::get_action_state_pose(session, getInfo, state);
	}
	XrResult XRAPI_PTR xrGetBodySkeletonFB(XrBodyTrackerFB bodyTracker, XrBodySkeletonFB * skeleton)
	{
		(void)bodyTracker;
		(void)skeleton;
		return XR_ERROR_FUNCTION_UNSUPPORTED;
	}
	XrResult XRAPI_PTR xrGetCurrentInteractionProfile(XrSession session, XrPath topLevelUserPath, XrInteractionProfileState * interactionProfile)
	{
		return wave::get_current_interaction_profile(session, topLevelUserPath, interactionProfile);
	}
	XrResult XRAPI_PTR xrGetDisplayRefreshRateFB(XrSession session, float * displayRefreshRate)
	{
		return wave::get_display_refresh_rate_fb(session, displayRefreshRate);
	}
	XrResult XRAPI_PTR xrGetFaceCalibrationStateANDROID(XrFaceTrackerANDROID faceTracker, XrBool32 * faceIsCalibratedOutput)
	{
		(void)faceTracker;
		(void)faceIsCalibratedOutput;
		return XR_ERROR_FUNCTION_UNSUPPORTED;
	}
	XrResult XRAPI_PTR xrGetFaceExpressionWeights2FB(XrFaceTracker2FB faceTracker, const XrFaceExpressionInfo2FB * expressionInfo, XrFaceExpressionWeights2FB * expressionWeights)
	{
		(void)faceTracker;
		(void)expressionInfo;
		(void)expressionWeights;
		return XR_ERROR_FUNCTION_UNSUPPORTED;
	}
	XrResult XRAPI_PTR xrGetFaceStateANDROID(XrFaceTrackerANDROID faceTracker, const XrFaceStateGetInfoANDROID * getInfo, XrFaceStateANDROID * faceStateOutput)
	{
		(void)faceTracker;
		(void)getInfo;
		(void)faceStateOutput;
		return XR_ERROR_FUNCTION_UNSUPPORTED;
	}
	XrResult XRAPI_PTR xrGetFacialExpressionsHTC(XrFacialTrackerHTC facialTracker, XrFacialExpressionsHTC * facialExpressions)
	{
		(void)facialTracker;
		(void)facialExpressions;
		return XR_ERROR_FUNCTION_UNSUPPORTED;
	}
	XrResult XRAPI_PTR xrGetHandMeshFB(XrHandTrackerEXT handTracker, XrHandTrackingMeshFB * mesh)
	{
		(void)handTracker;
		(void)mesh;
		return XR_ERROR_FUNCTION_UNSUPPORTED;
	}
	XrResult XRAPI_PTR xrGetInputSourceLocalizedName(XrSession session, const XrInputSourceLocalizedNameGetInfo * getInfo, uint32_t bufferCapacityInput, uint32_t * bufferCountOutput, char * buffer)
	{
		return wave::get_input_source_localized_name(session, getInfo, bufferCapacityInput, bufferCountOutput, buffer);
	}
	XrResult XRAPI_PTR xrGetInstanceProperties(XrInstance instance, XrInstanceProperties * instanceProperties)
	{
		return wave::get_instance_properties(instance, instanceProperties);
	}
	XrResult XRAPI_PTR xrGetSystem(XrInstance instance, const XrSystemGetInfo * getInfo, XrSystemId * systemId)
	{
		return wave::get_system(instance, getInfo, systemId);
	}
	XrResult XRAPI_PTR xrGetSystemProperties(XrInstance instance, XrSystemId systemId, XrSystemProperties * properties)
	{
		return wave::get_system_properties(instance, systemId, properties);
	}
	XrResult XRAPI_PTR xrGetViewConfigurationProperties(XrInstance instance, XrSystemId systemId, XrViewConfigurationType viewConfigurationType, XrViewConfigurationProperties * configurationProperties)
	{
		return wave::get_view_configuration_properties(instance, systemId, viewConfigurationType, configurationProperties);
	}
	XrResult XRAPI_PTR xrGetVisibilityMaskKHR(XrSession session, XrViewConfigurationType viewConfigurationType, uint32_t viewIndex, XrVisibilityMaskTypeKHR visibilityMaskType, XrVisibilityMaskKHR * visibilityMask)
	{
		return wave::get_visibility_mask_khr(session, viewConfigurationType, viewIndex, visibilityMaskType, visibilityMask);
	}
	XrResult XRAPI_PTR xrGetVulkanGraphicsDevice2KHR(XrInstance instance, const XrVulkanGraphicsDeviceGetInfoKHR * getInfo, VkPhysicalDevice * vulkanPhysicalDevice)
	{
		return wave::get_vulkan_graphics_device2_khr(instance, getInfo, vulkanPhysicalDevice);
	}
	XrResult XRAPI_PTR xrGetVulkanGraphicsRequirements2KHR(XrInstance instance, XrSystemId systemId, XrGraphicsRequirementsVulkanKHR * graphicsRequirements)
	{
		return wave::get_vulkan_graphics_requirements2_khr(instance, systemId, graphicsRequirements);
	}
	XrResult XRAPI_PTR xrLocateBodyJointsBD(XrBodyTrackerBD bodyTracker, const XrBodyJointsLocateInfoBD * locateInfo, XrBodyJointLocationsBD * locations)
	{
		(void)bodyTracker;
		(void)locateInfo;
		(void)locations;
		return XR_ERROR_FUNCTION_UNSUPPORTED;
	}
	XrResult XRAPI_PTR xrLocateBodyJointsFB(XrBodyTrackerFB bodyTracker, const XrBodyJointsLocateInfoFB * locateInfo, XrBodyJointLocationsFB * locations)
	{
		(void)bodyTracker;
		(void)locateInfo;
		(void)locations;
		return XR_ERROR_FUNCTION_UNSUPPORTED;
	}
	XrResult XRAPI_PTR xrLocateHandJointsEXT(XrHandTrackerEXT handTracker, const XrHandJointsLocateInfoEXT * locateInfo, XrHandJointLocationsEXT * locations)
	{
		(void)handTracker;
		(void)locateInfo;
		(void)locations;
		return XR_ERROR_FUNCTION_UNSUPPORTED;
	}
	XrResult XRAPI_PTR xrLocateSpace(XrSpace space, XrSpace baseSpace, XrTime time, XrSpaceLocation * location)
	{
		return wave::locate_space(space, baseSpace, time, location);
	}
	XrResult XRAPI_PTR xrLocateSpaces(XrSession session, const XrSpacesLocateInfo * locateInfo, XrSpaceLocations * spaceLocations)
	{
		return wave::locate_spaces(session, locateInfo, spaceLocations);
	}
	XrResult XRAPI_PTR xrLocateSpacesKHR(XrSession session, const XrSpacesLocateInfo * locateInfo, XrSpaceLocations * spaceLocations)
	{
		return wave::locate_spaces(session, locateInfo, spaceLocations);
	}
	XrResult XRAPI_PTR xrLocateViews(XrSession session, const XrViewLocateInfo * viewLocateInfo, XrViewState * viewState, uint32_t viewCapacityInput, uint32_t * viewCountOutput, XrView * views)
	{
		return wave::locate_views(session, viewLocateInfo, viewState, viewCapacityInput, viewCountOutput, views);
	}
	XrResult XRAPI_PTR xrPassthroughLayerPauseFB(XrPassthroughLayerFB layer)
	{
		(void)layer;
		return XR_ERROR_FUNCTION_UNSUPPORTED;
	}
	XrResult XRAPI_PTR xrPassthroughLayerResumeFB(XrPassthroughLayerFB layer)
	{
		(void)layer;
		return XR_ERROR_FUNCTION_UNSUPPORTED;
	}
	XrResult XRAPI_PTR xrPassthroughPauseFB(XrPassthroughFB passthrough)
	{
		(void)passthrough;
		return XR_ERROR_FUNCTION_UNSUPPORTED;
	}
	XrResult XRAPI_PTR xrPassthroughStartFB(XrPassthroughFB passthrough)
	{
		(void)passthrough;
		return XR_ERROR_FUNCTION_UNSUPPORTED;
	}
	XrResult XRAPI_PTR xrPathToString(XrInstance instance, XrPath path, uint32_t bufferCapacityInput, uint32_t * bufferCountOutput, char * buffer)
	{
		return wave::path_to_string(instance, path, bufferCapacityInput, bufferCountOutput, buffer);
	}
	XrResult XRAPI_PTR xrPerfSettingsSetPerformanceLevelEXT(XrSession session, XrPerfSettingsDomainEXT domain, XrPerfSettingsLevelEXT level)
	{
		(void)session;
		(void)domain;
		(void)level;
		return XR_ERROR_FUNCTION_UNSUPPORTED;
	}
	XrResult XRAPI_PTR xrPollEvent(XrInstance instance, XrEventDataBuffer * eventData)
	{
		return wave::poll_event(instance, eventData);
	}
	XrResult XRAPI_PTR xrReleaseSwapchainImage(XrSwapchain swapchain, const XrSwapchainImageReleaseInfo * releaseInfo)
	{
		return wave::release_swapchain_image(swapchain, releaseInfo);
	}
	XrResult XRAPI_PTR xrRequestBodyTrackingFidelityMETA(XrBodyTrackerFB bodyTracker, const XrBodyTrackingFidelityMETA fidelity)
	{
		(void)bodyTracker;
		(void)fidelity;
		return XR_ERROR_FUNCTION_UNSUPPORTED;
	}
	XrResult XRAPI_PTR xrRequestBoundaryVisibilityMETA(XrSession session, XrBoundaryVisibilityMETA boundaryVisibility)
	{
		(void)session;
		(void)boundaryVisibility;
		return XR_ERROR_FUNCTION_UNSUPPORTED;
	}
	XrResult XRAPI_PTR xrRequestDisplayRefreshRateFB(XrSession session, float displayRefreshRate)
	{
		return wave::request_display_refresh_rate_fb(session, displayRefreshRate);
	}
	XrResult XRAPI_PTR xrStopHapticFeedback(XrSession session, const XrHapticActionInfo * hapticActionInfo)
	{
		return wave::stop_haptic_feedback(session, hapticActionInfo);
	}
	XrResult XRAPI_PTR xrStringToPath(XrInstance instance, const char * pathString, XrPath * path)
	{
		return wave::string_to_path(instance, pathString, path);
	}
	XrResult XRAPI_PTR xrSuggestInteractionProfileBindings(XrInstance instance, const XrInteractionProfileSuggestedBinding * suggestedBindings)
	{
		return wave::suggest_interaction_profile_bindings(instance, suggestedBindings);
	}
	XrResult XRAPI_PTR xrSyncActions(XrSession session, const XrActionsSyncInfo * syncInfo)
	{
		return wave::sync_actions(session, syncInfo);
	}
	XrResult XRAPI_PTR xrWaitFrame(XrSession session, const XrFrameWaitInfo * frameWaitInfo, XrFrameState * frameState)
	{
		return wave::wait_frame(session, frameWaitInfo, frameState);
	}
	XrResult XRAPI_PTR xrWaitSwapchainImage(XrSwapchain swapchain, const XrSwapchainImageWaitInfo * waitInfo)
	{
		return wave::wait_swapchain_image(swapchain, waitInfo);
	}

	void XRAPI_PTR xrVoidFunction(void)
	{
	}

	XrResult XRAPI_PTR xrGetInstanceProcAddr(XrInstance instance, const char * name, PFN_xrVoidFunction * function)
	{
		(void)instance;
		static const std::unordered_map<std::string_view, PFN_xrVoidFunction> table = {
		        {"xrAcquireSwapchainImage", reinterpret_cast<PFN_xrVoidFunction>(xrAcquireSwapchainImage)},
		        {"xrApplyHapticFeedback", reinterpret_cast<PFN_xrVoidFunction>(xrApplyHapticFeedback)},
		        {"xrAttachSessionActionSets", reinterpret_cast<PFN_xrVoidFunction>(xrAttachSessionActionSets)},
		        {"xrBeginFrame", reinterpret_cast<PFN_xrVoidFunction>(xrBeginFrame)},
		        {"xrBeginSession", reinterpret_cast<PFN_xrVoidFunction>(xrBeginSession)},
		        {"xrConvertTimespecTimeToTimeKHR", reinterpret_cast<PFN_xrVoidFunction>(xrConvertTimespecTimeToTimeKHR)},
		        {"xrCreateAction", reinterpret_cast<PFN_xrVoidFunction>(xrCreateAction)},
		        {"xrCreateActionSet", reinterpret_cast<PFN_xrVoidFunction>(xrCreateActionSet)},
		        {"xrCreateActionSpace", reinterpret_cast<PFN_xrVoidFunction>(xrCreateActionSpace)},
		        {"xrCreateBodyTrackerBD", reinterpret_cast<PFN_xrVoidFunction>(xrCreateBodyTrackerBD)},
		        {"xrCreateBodyTrackerFB", reinterpret_cast<PFN_xrVoidFunction>(xrCreateBodyTrackerFB)},
		        {"xrCreateDebugUtilsMessengerEXT", reinterpret_cast<PFN_xrVoidFunction>(xrCreateDebugUtilsMessengerEXT)},
		        {"xrCreateFaceTracker2FB", reinterpret_cast<PFN_xrVoidFunction>(xrCreateFaceTracker2FB)},
		        {"xrCreateFaceTrackerANDROID", reinterpret_cast<PFN_xrVoidFunction>(xrCreateFaceTrackerANDROID)},
		        {"xrCreateFacialTrackerHTC", reinterpret_cast<PFN_xrVoidFunction>(xrCreateFacialTrackerHTC)},
		        {"xrCreateHandTrackerEXT", reinterpret_cast<PFN_xrVoidFunction>(xrCreateHandTrackerEXT)},
		        {"xrCreateInstance", reinterpret_cast<PFN_xrVoidFunction>(xrCreateInstance)},
		        {"xrCreatePassthroughFB", reinterpret_cast<PFN_xrVoidFunction>(xrCreatePassthroughFB)},
		        {"xrCreatePassthroughHTC", reinterpret_cast<PFN_xrVoidFunction>(xrCreatePassthroughHTC)},
		        {"xrCreatePassthroughLayerFB", reinterpret_cast<PFN_xrVoidFunction>(xrCreatePassthroughLayerFB)},
		        {"xrCreateReferenceSpace", reinterpret_cast<PFN_xrVoidFunction>(xrCreateReferenceSpace)},
		        {"xrCreateSession", reinterpret_cast<PFN_xrVoidFunction>(xrCreateSession)},
		        {"xrCreateSwapchain", reinterpret_cast<PFN_xrVoidFunction>(xrCreateSwapchain)},
		        {"xrCreateVulkanDeviceKHR", reinterpret_cast<PFN_xrVoidFunction>(xrCreateVulkanDeviceKHR)},
		        {"xrCreateVulkanInstanceKHR", reinterpret_cast<PFN_xrVoidFunction>(xrCreateVulkanInstanceKHR)},
		        {"xrDestroyActionSet", reinterpret_cast<PFN_xrVoidFunction>(xrDestroyActionSet)},
		        {"xrDestroyBodyTrackerBD", reinterpret_cast<PFN_xrVoidFunction>(xrDestroyBodyTrackerBD)},
		        {"xrDestroyBodyTrackerFB", reinterpret_cast<PFN_xrVoidFunction>(xrDestroyBodyTrackerFB)},
		        {"xrDestroyFaceTracker2FB", reinterpret_cast<PFN_xrVoidFunction>(xrDestroyFaceTracker2FB)},
		        {"xrDestroyFaceTrackerANDROID", reinterpret_cast<PFN_xrVoidFunction>(xrDestroyFaceTrackerANDROID)},
		        {"xrDestroyHandTrackerEXT", reinterpret_cast<PFN_xrVoidFunction>(xrDestroyHandTrackerEXT)},
		        {"xrDestroyInstance", reinterpret_cast<PFN_xrVoidFunction>(xrDestroyInstance)},
		        {"xrDestroyPassthroughFB", reinterpret_cast<PFN_xrVoidFunction>(xrDestroyPassthroughFB)},
		        {"xrDestroyPassthroughHTC", reinterpret_cast<PFN_xrVoidFunction>(xrDestroyPassthroughHTC)},
		        {"xrDestroyPassthroughLayerFB", reinterpret_cast<PFN_xrVoidFunction>(xrDestroyPassthroughLayerFB)},
		        {"xrDestroySession", reinterpret_cast<PFN_xrVoidFunction>(xrDestroySession)},
		        {"xrDestroySpace", reinterpret_cast<PFN_xrVoidFunction>(xrDestroySpace)},
		        {"xrDestroySwapchain", reinterpret_cast<PFN_xrVoidFunction>(xrDestroySwapchain)},
		        {"xrEndFrame", reinterpret_cast<PFN_xrVoidFunction>(xrEndFrame)},
		        {"xrEndSession", reinterpret_cast<PFN_xrVoidFunction>(xrEndSession)},
		        {"xrEnumerateApiLayerProperties", reinterpret_cast<PFN_xrVoidFunction>(xrEnumerateApiLayerProperties)},
		        {"xrEnumerateBoundSourcesForAction", reinterpret_cast<PFN_xrVoidFunction>(xrEnumerateBoundSourcesForAction)},
		        {"xrEnumerateDisplayRefreshRatesFB", reinterpret_cast<PFN_xrVoidFunction>(xrEnumerateDisplayRefreshRatesFB)},
		        {"xrEnumerateEnvironmentBlendModes", reinterpret_cast<PFN_xrVoidFunction>(xrEnumerateEnvironmentBlendModes)},
		        {"xrEnumerateInstanceExtensionProperties", reinterpret_cast<PFN_xrVoidFunction>(xrEnumerateInstanceExtensionProperties)},
		        {"xrEnumerateReferenceSpaces", reinterpret_cast<PFN_xrVoidFunction>(xrEnumerateReferenceSpaces)},
		        {"xrEnumerateSwapchainFormats", reinterpret_cast<PFN_xrVoidFunction>(xrEnumerateSwapchainFormats)},
		        {"xrEnumerateSwapchainImages", reinterpret_cast<PFN_xrVoidFunction>(xrEnumerateSwapchainImages)},
		        {"xrEnumerateViewConfigurationViews", reinterpret_cast<PFN_xrVoidFunction>(xrEnumerateViewConfigurationViews)},
		        {"xrEnumerateViewConfigurations", reinterpret_cast<PFN_xrVoidFunction>(xrEnumerateViewConfigurations)},
		        {"xrGetActionStateBoolean", reinterpret_cast<PFN_xrVoidFunction>(xrGetActionStateBoolean)},
		        {"xrGetActionStateFloat", reinterpret_cast<PFN_xrVoidFunction>(xrGetActionStateFloat)},
		        {"xrGetActionStatePose", reinterpret_cast<PFN_xrVoidFunction>(xrGetActionStatePose)},
		        {"xrGetActionStateVector2f", reinterpret_cast<PFN_xrVoidFunction>(xrGetActionStateVector2f)},
		        {"xrGetBodySkeletonFB", reinterpret_cast<PFN_xrVoidFunction>(xrGetBodySkeletonFB)},
		        {"xrGetCurrentInteractionProfile", reinterpret_cast<PFN_xrVoidFunction>(xrGetCurrentInteractionProfile)},
		        {"xrGetDisplayRefreshRateFB", reinterpret_cast<PFN_xrVoidFunction>(xrGetDisplayRefreshRateFB)},
		        {"xrGetFaceCalibrationStateANDROID", reinterpret_cast<PFN_xrVoidFunction>(xrGetFaceCalibrationStateANDROID)},
		        {"xrGetFaceExpressionWeights2FB", reinterpret_cast<PFN_xrVoidFunction>(xrGetFaceExpressionWeights2FB)},
		        {"xrGetFaceStateANDROID", reinterpret_cast<PFN_xrVoidFunction>(xrGetFaceStateANDROID)},
		        {"xrGetFacialExpressionsHTC", reinterpret_cast<PFN_xrVoidFunction>(xrGetFacialExpressionsHTC)},
		        {"xrGetHandMeshFB", reinterpret_cast<PFN_xrVoidFunction>(xrGetHandMeshFB)},
		        {"xrGetInputSourceLocalizedName", reinterpret_cast<PFN_xrVoidFunction>(xrGetInputSourceLocalizedName)},
		        {"xrGetInstanceProperties", reinterpret_cast<PFN_xrVoidFunction>(xrGetInstanceProperties)},
		        {"xrGetSystem", reinterpret_cast<PFN_xrVoidFunction>(xrGetSystem)},
		        {"xrGetSystemProperties", reinterpret_cast<PFN_xrVoidFunction>(xrGetSystemProperties)},
		        {"xrGetViewConfigurationProperties", reinterpret_cast<PFN_xrVoidFunction>(xrGetViewConfigurationProperties)},
		        {"xrGetVisibilityMaskKHR", reinterpret_cast<PFN_xrVoidFunction>(xrGetVisibilityMaskKHR)},
		        {"xrGetVulkanGraphicsDevice2KHR", reinterpret_cast<PFN_xrVoidFunction>(xrGetVulkanGraphicsDevice2KHR)},
		        {"xrGetVulkanGraphicsRequirements2KHR", reinterpret_cast<PFN_xrVoidFunction>(xrGetVulkanGraphicsRequirements2KHR)},
		        {"xrLocateBodyJointsBD", reinterpret_cast<PFN_xrVoidFunction>(xrLocateBodyJointsBD)},
		        {"xrLocateBodyJointsFB", reinterpret_cast<PFN_xrVoidFunction>(xrLocateBodyJointsFB)},
		        {"xrLocateHandJointsEXT", reinterpret_cast<PFN_xrVoidFunction>(xrLocateHandJointsEXT)},
		        {"xrLocateSpace", reinterpret_cast<PFN_xrVoidFunction>(xrLocateSpace)},
		        {"xrLocateSpaces", reinterpret_cast<PFN_xrVoidFunction>(xrLocateSpaces)},
		        {"xrLocateSpacesKHR", reinterpret_cast<PFN_xrVoidFunction>(xrLocateSpacesKHR)},
		        {"xrLocateViews", reinterpret_cast<PFN_xrVoidFunction>(xrLocateViews)},
		        {"xrPassthroughLayerPauseFB", reinterpret_cast<PFN_xrVoidFunction>(xrPassthroughLayerPauseFB)},
		        {"xrPassthroughLayerResumeFB", reinterpret_cast<PFN_xrVoidFunction>(xrPassthroughLayerResumeFB)},
		        {"xrPassthroughPauseFB", reinterpret_cast<PFN_xrVoidFunction>(xrPassthroughPauseFB)},
		        {"xrPassthroughStartFB", reinterpret_cast<PFN_xrVoidFunction>(xrPassthroughStartFB)},
		        {"xrPathToString", reinterpret_cast<PFN_xrVoidFunction>(xrPathToString)},
		        {"xrPerfSettingsSetPerformanceLevelEXT", reinterpret_cast<PFN_xrVoidFunction>(xrPerfSettingsSetPerformanceLevelEXT)},
		        {"xrPollEvent", reinterpret_cast<PFN_xrVoidFunction>(xrPollEvent)},
		        {"xrReleaseSwapchainImage", reinterpret_cast<PFN_xrVoidFunction>(xrReleaseSwapchainImage)},
		        {"xrRequestBodyTrackingFidelityMETA", reinterpret_cast<PFN_xrVoidFunction>(xrRequestBodyTrackingFidelityMETA)},
		        {"xrRequestBoundaryVisibilityMETA", reinterpret_cast<PFN_xrVoidFunction>(xrRequestBoundaryVisibilityMETA)},
		        {"xrRequestDisplayRefreshRateFB", reinterpret_cast<PFN_xrVoidFunction>(xrRequestDisplayRefreshRateFB)},
		        {"xrStopHapticFeedback", reinterpret_cast<PFN_xrVoidFunction>(xrStopHapticFeedback)},
		        {"xrStringToPath", reinterpret_cast<PFN_xrVoidFunction>(xrStringToPath)},
		        {"xrSuggestInteractionProfileBindings", reinterpret_cast<PFN_xrVoidFunction>(xrSuggestInteractionProfileBindings)},
		        {"xrSyncActions", reinterpret_cast<PFN_xrVoidFunction>(xrSyncActions)},
		        {"xrVoidFunction", reinterpret_cast<PFN_xrVoidFunction>(xrVoidFunction)},
		        {"xrWaitFrame", reinterpret_cast<PFN_xrVoidFunction>(xrWaitFrame)},
		        {"xrWaitSwapchainImage", reinterpret_cast<PFN_xrVoidFunction>(xrWaitSwapchainImage)},
		};

		auto it = table.find(name);
		if (it == table.end())
		{
			*function = nullptr;
			return XR_ERROR_FUNCTION_UNSUPPORTED;
		}
		*function = it->second;
		return XR_SUCCESS;
	}
}
