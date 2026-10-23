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

// Declarations of the HTC WaveVR native API used by the VIVE Focus Plus OpenXR
// runtime. They are transcribed from the Wave VR 3.1.6 SDK headers
// (wvr.h, wvr_types.h, wvr_render.h, wvr_device.h, wvr_events.h,
// wvr_projection.h, wvr_vulkan.h) so that the client does not need the
// proprietary SDK at build time; the library is resolved at runtime with
// dlopen()/dlsym(). Layouts must match the SDK ABI exactly.
//
// Reference: https://developer.vive.com/resources/vive-wave/

#include <cstdint>
#include <vulkan/vulkan.h>

namespace wvr
{
enum DeviceType
{
	DeviceType_Invalid = 0,
	DeviceType_HMD = 1,
	DeviceType_Controller_Right = 2,
	DeviceType_Controller_Left = 3,
};

enum Eye
{
	Eye_Left = 0,
	Eye_Right = 1,
};

enum InputId
{
	InputId_System = 0,
	InputId_Menu = 1,
	InputId_Grip = 2,
	InputId_DPad_Left = 3,
	InputId_DPad_Up = 4,
	InputId_DPad_Right = 5,
	InputId_DPad_Down = 6,
	InputId_Volume_Up = 7,
	InputId_Volume_Down = 8,
	InputId_Digital_Trigger = 9,
	InputId_Back = 14,
	InputId_Enter = 15,
	InputId_Touchpad = 16,
	InputId_Trigger = 17,
	InputId_Thumbstick = 18,
	InputId_Max = 32,
};

enum InputType
{
	InputType_Button = 1 << 0,
	InputType_Touch = 1 << 1,
	InputType_Analog = 1 << 2,
};

enum AnalogType
{
	AnalogType_None = 0,
	AnalogType_2D = 1,
	AnalogType_1D = 2,
};

enum Intensity
{
	Intensity_Weak = 1,
	Intensity_Light = 2,
	Intensity_Normal = 3,
	Intensity_Strong = 4,
	Intensity_Severe = 5,
};

enum PoseOriginModel
{
	PoseOriginModel_OriginOnHead = 0,
	PoseOriginModel_OriginOnGround = 1,
	PoseOriginModel_OriginOnTrackingObserver = 2,
	PoseOriginModel_OriginOnHead_3DoF = 3,
};

enum EventType
{
	EventType_Quit = 1000,
	EventType_TrackingModeChanged = 1003,
	EventType_DeviceConnected = 2000,
	EventType_DeviceDisconnected = 2001,
	EventType_DeviceStatusUpdate = 2002,
	EventType_DeviceSuspend = 2003,
	EventType_DeviceResume = 2004,
	EventType_IpdChanged = 2005,
	EventType_DeviceRoleChanged = 2006,
	EventType_RecenterSuccess = 2011,
	EventType_RecenterFail = 2012,
};

enum NumDoF
{
	NumDoF_3DoF = 0,
	NumDoF_6DoF = 1,
};

enum RecenterType
{
	RecenterType_Disabled = 0,
	RecenterType_YawOnly = 1,
	RecenterType_YawAndPosition = 2,
	RecenterType_RotationAndPosition = 3,
};

enum AppType
{
	AppType_VRContent = 1,
};

enum InitError
{
	InitError_None = 0,
	InitError_Unknown = 1,
	InitError_NotInitialized = 2,
};

enum GraphicsApiType
{
	GraphicsApiType_OpenGL = 1,
};

enum RenderError
{
	RenderError_None = 0,
	RenderError_RuntimeInitFailed = 410,
	RenderError_ContextSetupFailed = 411,
	RenderError_DisplaySetupFailed = 412,
	RenderError_LibNotSupported = 413,
	RenderError_NullPtr = 414,
};

enum SubmitExtend
{
	SubmitExtend_Default = 0x0000,
	SubmitExtend_DisableDistortion = 0x0001,
	SubmitExtend_PartialTexture = 0x0010,
};

enum SubmitError
{
	SubmitError_None = 0,
	SubmitError_InvalidTexture = 400,
	SubmitError_ThreadStop = 401,
	SubmitError_BufferSubmitFailed = 402,
};

enum TextureTarget
{
	TextureTarget_2D = 0,
	TextureTarget_2D_ARRAY = 1,
	TextureTarget_2D_EXTERNAL = 2,
	TextureTarget_2D_DUAL = 3,
	TextureTarget_2D_EXT_DUAL = 4,
	TextureTarget_VULKAN = 5,
};

enum TextureFormat
{
	TextureFormat_RGBA = 0,
};

enum TextureType
{
	TextureType_UnsignedByte = 0,
};

using TextureHandle = void *;
using TextureQueueHandle = void *;

struct Vector2f
{
	float v[2];
};

struct Vector3f
{
	float v[3];
};

struct Axis
{
	float x, y;
};

struct Quatf
{
	float w, x, y, z;
};

struct Matrix4f
{
	float m[4][4];
};

struct Pose
{
	Vector3f position;
	Quatf rotation;
};

struct PoseState
{
	bool isValidPose;
	Matrix4f poseMatrix;
	Vector3f velocity;
	Vector3f angularVelocity;
	bool is6DoFPose;
	int64_t timestamp;
	Vector3f acceleration;
	Vector3f angularAcceleration;
	float predictedMilliSec;
	PoseOriginModel originModel;
	Pose rawPose;
};

struct DevicePosePair
{
	DeviceType type;
	PoseState pose;
};

struct TextureLayout
{
	Vector2f leftLowUVs;
	Vector2f rightUpUVs;
};

struct TextureParams
{
	TextureHandle id;
	TextureTarget target;
	TextureLayout layout;
};

struct RenderInitParams
{
	GraphicsApiType graphicsApi;
	uint64_t renderConfig;
};

struct RenderProps
{
	float refreshRate;
	bool hasExternal;
	float ipdMeter;
};

struct AnalogState
{
	InputId id;
	AnalogType type;
	Axis axis;
};

struct InputAttribute
{
	InputId id;
	int32_t capability;
	AnalogType axis_type;
};

struct InputMappingPair
{
	InputAttribute destination;
	InputAttribute source;
};

struct CommonEvent
{
	EventType type;
	int64_t timestamp;
};

struct DeviceEvent
{
	CommonEvent common;
	DeviceType deviceType;
};

struct InputEvent
{
	DeviceEvent device;
	InputId inputId;
};

union Event
{
	CommonEvent common;
	DeviceEvent device;
	InputEvent input;
};

struct VulkanSystemInfo
{
	VkInstance instance;
	VkPhysicalDevice physicalDevice;
	VkDevice device;
};

// Function pointers resolved from libwvr_api.so.
struct api
{
	void * dl_handle = nullptr;

	// Wave invokes the function registered here on its own main thread; WVR_Init
	// must run on that thread (wvr.h: "runtime expect the entry function of
	// native application registed here via this API").
	void (*RegisterMain)(int (*)(int, char **)) = nullptr;

	InitError (*Init)(AppType) = nullptr;
	void (*Quit)() = nullptr;
	uint32_t (*GetWaveRuntimeVersion)() = nullptr;
	uint32_t (*GetWaveSDKVersion)() = nullptr;
	const char * (*GetInitErrorString)(InitError) = nullptr;

	bool (*IsDeviceConnected)(DeviceType) = nullptr;
	NumDoF (*GetDegreeOfFreedom)(DeviceType) = nullptr;
	void (*InAppRecenter)(RecenterType) = nullptr;

	RenderError (*RenderInit)(const RenderInitParams *) = nullptr;
	bool (*GetRenderProps)(RenderProps *) = nullptr;
	void (*GetRenderTargetSize)(uint32_t *, uint32_t *) = nullptr;
	TextureQueueHandle (*ObtainTextureQueue)(TextureTarget, TextureFormat, TextureType, uint32_t, uint32_t, int32_t) = nullptr;
	uint32_t (*GetTextureQueueLength)(TextureQueueHandle) = nullptr;
	int32_t (*GetAvailableTextureIndex)(TextureQueueHandle) = nullptr;
	TextureParams (*GetTexture)(TextureQueueHandle, int32_t) = nullptr;
	void (*ReleaseTextureQueue)(TextureQueueHandle) = nullptr;
	SubmitError (*SubmitFrame)(Eye, const TextureParams *, const PoseState *, SubmitExtend) = nullptr;

	bool (*VulkanInit)(const VulkanSystemInfo *) = nullptr;
	VkImage (*GetVkImage)(TextureQueueHandle, int32_t) = nullptr;


	Matrix4f (*GetProjection)(Eye, float, float) = nullptr;
	Matrix4f (*GetTransformFromEyeToHead)(Eye, NumDoF) = nullptr;
	void (*GetSyncPose)(PoseOriginModel, DevicePosePair *, uint32_t) = nullptr;
	void (*GetPoseState)(DeviceType, PoseOriginModel, uint32_t, PoseState *) = nullptr;

	bool (*PollEventQueue)(Event *) = nullptr;

	int32_t (*GetInputDeviceCapability)(DeviceType, InputType) = nullptr;
	AnalogType (*GetInputDeviceAnalogType)(DeviceType, InputId) = nullptr;
	int32_t (*GetInputTypeCount)(DeviceType, InputType) = nullptr;
	bool (*GetInputDeviceState)(DeviceType, uint32_t, uint32_t *, uint32_t *, AnalogState *, uint32_t) = nullptr;
	bool (*GetInputButtonState)(DeviceType, InputId) = nullptr;
	bool (*GetInputTouchState)(DeviceType, InputId) = nullptr;
	Axis (*GetInputAnalogAxis)(DeviceType, InputId) = nullptr;
	bool (*SetInputRequest)(DeviceType, const InputAttribute *, uint32_t) = nullptr;

	uint32_t (*GetInputMappingTable)(DeviceType, InputMappingPair *, uint32_t) = nullptr;
	bool (*IsInputFocusCapturedBySystem)() = nullptr;
	// Wave 4.x only; nullptr on Wave 3.x runtimes.
	void (*TriggerVibration)(DeviceType, InputId, uint32_t, uint32_t, Intensity) = nullptr;

	api(const api &) = delete;
	api & operator=(const api &) = delete;

	// Load libwvr_api.so; returns nullptr when the WaveVR runtime is not
	// available (not installed, or not a VIVE Focus family device).
	static api * load();
	// Instance created by load(), or nullptr.
	static api * get();

private:
	api() = default;
	~api();
};
} // namespace wvr
