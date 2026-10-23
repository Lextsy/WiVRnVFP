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
#include <cstring>
#include <dlfcn.h>
#include <spdlog/spdlog.h>
#include <unordered_map>
#include <vector>

namespace wave
{
vulkan_loader::vulkan_loader()
{
	void * handle = dlopen("libvulkan.so", RTLD_NOW | RTLD_LOCAL);
	if (handle == nullptr)
		return;
	CreateInstance = reinterpret_cast<PFN_vkCreateInstance>(dlsym(handle, "vkCreateInstance"));
	EnumeratePhysicalDevices = reinterpret_cast<PFN_vkEnumeratePhysicalDevices>(dlsym(handle, "vkEnumeratePhysicalDevices"));
	GetPhysicalDeviceProperties = reinterpret_cast<PFN_vkGetPhysicalDeviceProperties>(dlsym(handle, "vkGetPhysicalDeviceProperties"));
	GetQueueFamilyProperties = reinterpret_cast<PFN_vkGetPhysicalDeviceQueueFamilyProperties>(dlsym(handle, "vkGetPhysicalDeviceQueueFamilyProperties"));
	GetPhysicalDeviceMemoryProperties = reinterpret_cast<PFN_vkGetPhysicalDeviceMemoryProperties>(dlsym(handle, "vkGetPhysicalDeviceMemoryProperties"));
	GetDeviceProcAddr = reinterpret_cast<PFN_vkGetDeviceProcAddr>(dlsym(handle, "vkGetDeviceProcAddr"));
	GetInstanceProcAddr = reinterpret_cast<PFN_vkGetInstanceProcAddr>(dlsym(handle, "vkGetInstanceProcAddr"));
}

vulkan_loader & loader()
{
	static vulkan_loader l;
	return l;
}

const device_vulkan & device_entries(VkDevice device)
{
	static std::unordered_map<VkDevice, device_vulkan> cache;
	auto it = cache.find(device);
	if (it != cache.end())
		return it->second;

	device_vulkan entries;
	if (device != VK_NULL_HANDLE and loader().GetDeviceProcAddr != nullptr)
	{
		auto get = [&](const char * name) {
			return reinterpret_cast<void *>(loader().GetDeviceProcAddr(device, name));
		};
		entries.CreateImage = reinterpret_cast<PFN_vkCreateImage>(get("vkCreateImage"));
		entries.DestroyImage = reinterpret_cast<PFN_vkDestroyImage>(get("vkDestroyImage"));
		entries.GetImageMemoryRequirements = reinterpret_cast<PFN_vkGetImageMemoryRequirements>(get("vkGetImageMemoryRequirements"));
		entries.AllocateMemory = reinterpret_cast<PFN_vkAllocateMemory>(get("vkAllocateMemory"));
		entries.FreeMemory = reinterpret_cast<PFN_vkFreeMemory>(get("vkFreeMemory"));
		entries.BindImageMemory = reinterpret_cast<PFN_vkBindImageMemory>(get("vkBindImageMemory"));
		entries.CreateCommandPool = reinterpret_cast<PFN_vkCreateCommandPool>(get("vkCreateCommandPool"));
		entries.DestroyCommandPool = reinterpret_cast<PFN_vkDestroyCommandPool>(get("vkDestroyCommandPool"));
		entries.AllocateCommandBuffers = reinterpret_cast<PFN_vkAllocateCommandBuffers>(get("vkAllocateCommandBuffers"));
		entries.FreeCommandBuffers = reinterpret_cast<PFN_vkFreeCommandBuffers>(get("vkFreeCommandBuffers"));
		entries.BeginCommandBuffer = reinterpret_cast<PFN_vkBeginCommandBuffer>(get("vkBeginCommandBuffer"));
		entries.EndCommandBuffer = reinterpret_cast<PFN_vkEndCommandBuffer>(get("vkEndCommandBuffer"));
		entries.CmdPipelineBarrier = reinterpret_cast<PFN_vkCmdPipelineBarrier>(get("vkCmdPipelineBarrier"));
		entries.CmdCopyImage = reinterpret_cast<PFN_vkCmdCopyImage>(get("vkCmdCopyImage"));
		entries.QueueSubmit = reinterpret_cast<PFN_vkQueueSubmit>(get("vkQueueSubmit"));
		entries.QueueWaitIdle = reinterpret_cast<PFN_vkQueueWaitIdle>(get("vkQueueWaitIdle"));
		entries.GetDeviceQueue = reinterpret_cast<PFN_vkGetDeviceQueue>(get("vkGetDeviceQueue"));
		entries.CreateFence = reinterpret_cast<PFN_vkCreateFence>(get("vkCreateFence"));
		entries.ResetFences = reinterpret_cast<PFN_vkResetFences>(get("vkResetFences"));
		entries.WaitForFences = reinterpret_cast<PFN_vkWaitForFences>(get("vkWaitForFences"));
		entries.DestroyFence = reinterpret_cast<PFN_vkDestroyFence>(get("vkDestroyFence"));
	}
	return cache.emplace(device, entries).first->second;
}

uint32_t graphics_queue_family(VkPhysicalDevice physical_device)
{
	auto enumerate = loader().GetQueueFamilyProperties;
	if (physical_device == VK_NULL_HANDLE or enumerate == nullptr)
		return 0;

	uint32_t count = 0;
	enumerate(physical_device, &count, nullptr);
	std::vector<VkQueueFamilyProperties> families(count);
	enumerate(physical_device, &count, families.data());

	for (uint32_t i = 0; i < count; ++i)
	{
		if (families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)
			return i;
	}
	return 0;
}

XrResult get_vulkan_graphics_requirements2_khr(XrInstance handle, [[maybe_unused]] XrSystemId system_id, XrGraphicsRequirementsVulkan2KHR * requirements)
{
	if (not instance_from(handle))
		return XR_ERROR_HANDLE_INVALID;

	// The Adreno 540 in the VIVE Focus Plus exposes Vulkan 1.0.
	requirements->type = XR_TYPE_GRAPHICS_REQUIREMENTS_VULKAN2_KHR;
	requirements->minApiVersionSupported = VK_API_VERSION_1_0;
	requirements->maxApiVersionSupported = VK_API_VERSION_1_1;
	return XR_SUCCESS;
}
XrResult get_vulkan_graphics_device2_khr(XrInstance handle, const XrVulkanGraphicsDeviceGetInfoKHR * info, VkPhysicalDevice * physical_device)
{
	if (not instance_from(handle) or info->systemId != 1)
		return XR_ERROR_HANDLE_INVALID;

	auto enumerate = loader().EnumeratePhysicalDevices;
	if (enumerate == nullptr)
		return XR_ERROR_GRAPHICS_DEVICE_INVALID;

	// Prefer the instance the application is building on; fall back to a probe
	// instance of our own (physical devices stay valid while it lives).
	VkInstance instance = info->vulkanInstance;
	static VkInstance probe = VK_NULL_HANDLE;
	if (instance == VK_NULL_HANDLE)
	{
		if (probe == VK_NULL_HANDLE)
		{
			VkInstanceCreateInfo create_info{};
			create_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
			if (loader().CreateInstance(&create_info, nullptr, &probe) != VK_SUCCESS)
			{
				probe = VK_NULL_HANDLE;
				return XR_ERROR_GRAPHICS_DEVICE_INVALID;
			}
		}
		instance = probe;
	}

	uint32_t count = 0;
	if (enumerate(instance, &count, nullptr) != VK_SUCCESS or count == 0)
		return XR_ERROR_GRAPHICS_DEVICE_INVALID;

	std::vector<VkPhysicalDevice> devices(count);
	if (enumerate(instance, &count, devices.data()) != VK_SUCCESS)
		return XR_ERROR_GRAPHICS_DEVICE_INVALID;

	// Prefer the Adreno GPU (the only one on this device).
	VkPhysicalDevice chosen = devices[0];
	for (VkPhysicalDevice device: devices)
	{
		VkPhysicalDeviceProperties properties{};
		auto get_properties = loader().GetPhysicalDeviceProperties;
		if (get_properties == nullptr)
			continue;
		get_properties(device, &properties);
		spdlog::info("WaveVR: Vulkan physical device {} \"{}\"", int(properties.deviceType), properties.deviceName);
		if (std::strstr(properties.deviceName, "Adreno"))
		{
			chosen = device;
			break;
		}
	}

	*physical_device = chosen;
	return XR_SUCCESS;
}

XrResult create_vulkan_instance_khr(XrInstance handle, const XrVulkanInstanceCreateInfoKHR * info, VkInstance * vulkan_instance, VkResult * vulkan_result)
{
	auto * inst = instance_from(handle);
	if (not inst)
		return XR_ERROR_HANDLE_INVALID;

	auto create = reinterpret_cast<PFN_vkCreateInstance>(info->pfnGetInstanceProcAddr(VK_NULL_HANDLE, "vkCreateInstance"));
	if (create == nullptr)
		return XR_ERROR_GRAPHICS_DEVICE_INVALID;

	*vulkan_result = create(info->vulkanCreateInfo, info->vulkanAllocator, vulkan_instance);
	if (*vulkan_result == VK_SUCCESS)
		inst->vk_instance = *vulkan_instance;

	return XR_SUCCESS;
}

XrResult create_vulkan_device_khr(XrInstance handle, const XrVulkanDeviceCreateInfoKHR * info, VkDevice * vulkan_device, VkResult * vulkan_result)
{
	auto * inst = instance_from(handle);
	if (not inst)
		return XR_ERROR_HANDLE_INVALID;

	// xrCreateVulkanInstanceKHR runs before the session exists; the instance
	// owns the VkInstance.
	VkInstance vk_instance = inst->vk_instance;
	auto create = reinterpret_cast<PFN_vkCreateDevice>(
	        info->pfnGetInstanceProcAddr ? info->pfnGetInstanceProcAddr(vk_instance, "vkCreateDevice") : nullptr);
	// The application's loader may not expose vkCreateDevice through its own
	// GetInstanceProcAddr; fall back to the system loader, which always does.
	if (create == nullptr and loader().GetInstanceProcAddr)
		create = reinterpret_cast<PFN_vkCreateDevice>(loader().GetInstanceProcAddr(vk_instance, "vkCreateDevice"));
	if (create == nullptr)
		return XR_ERROR_GRAPHICS_DEVICE_INVALID;

	*vulkan_result = create(info->vulkanPhysicalDevice, info->vulkanCreateInfo, info->vulkanAllocator, vulkan_device);
	if (*vulkan_result != VK_SUCCESS)
		return XR_SUCCESS;

	// The client creates the VkDevice before the session exists (initialize()
	// runs xrCreateVulkanDeviceKHR, then xrCreateSession). Record it on the
	// instance; create_session adopts it. Wave must be told which Vulkan device
	// owns the textures, in this order.
	inst->pending_vk_device = *vulkan_device;
	inst->pending_vk_physical_device = info->vulkanPhysicalDevice;

	wvr::RenderInitParams render_init{
	        .graphicsApi = wvr::GraphicsApiType_OpenGL,
	        .renderConfig = 0,
	};
	wvr::RenderError render_error = wvr::api::get()->RenderInit(&render_init);
	if (render_error != wvr::RenderError_None)
		spdlog::warn("WaveVR: WVR_RenderInit failed with {}", int(render_error));

	wvr::VulkanSystemInfo system_info{
	        .instance = inst->vk_instance,
	        .physicalDevice = info->vulkanPhysicalDevice,
	        .device = *vulkan_device,
	};
	if (not wvr::api::get()->VulkanInit(&system_info))
		spdlog::error("WaveVR: WVR_VulkanInit failed; presenting frames will not work");
	else
		inst->wave_render_initialized = true;

	return XR_SUCCESS;
}
} // namespace wave
