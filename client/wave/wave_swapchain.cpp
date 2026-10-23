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
#include <spdlog/spdlog.h>
#include <string>
#include <thread>
#include <vector>

namespace wave
{
namespace
{
// Wave's texture queue images are single-layer RGBA8; the client renders into
// array images (one layer per eye) and the shim copies each layer across.
constexpr VkFormat wave_image_format = VK_FORMAT_R8G8B8A8_UNORM;

VkImage create_image(const device_vulkan & vk, VkDevice device, VkPhysicalDevice physical_device, uint32_t width, uint32_t height, uint32_t layers, VkDeviceMemory * memory)
{
	VkImageCreateInfo image_info{
	        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
	        .pNext = nullptr,
	        .flags = 0,
	        .imageType = VK_IMAGE_TYPE_2D,
	        .format = wave_image_format,
	        .extent = {width, height, 1},
	        .mipLevels = 1,
	        .arrayLayers = layers,
	        .samples = VK_SAMPLE_COUNT_1_BIT,
	        .tiling = VK_IMAGE_TILING_OPTIMAL,
	        .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
	        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
	        .queueFamilyIndexCount = 0,
	        .pQueueFamilyIndices = nullptr,
	        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
	};

	VkImage image = VK_NULL_HANDLE;
	if (vk.CreateImage(device, &image_info, nullptr, &image) != VK_SUCCESS)
	{
		spdlog::error("WaveVR: vkCreateImage failed for a {}x{}x{} swapchain image", width, height, layers);
		return VK_NULL_HANDLE;
	}

	VkMemoryRequirements requirements{};
	vk.GetImageMemoryRequirements(device, image, &requirements);

	auto get_memory_properties = loader().GetPhysicalDeviceMemoryProperties;
	if (get_memory_properties == nullptr)
		return VK_NULL_HANDLE;

	VkPhysicalDeviceMemoryProperties memory_properties{};
	get_memory_properties(physical_device, &memory_properties);

	uint32_t type = 0;
	bool found = false;
	for (; type < memory_properties.memoryTypeCount; ++type)
	{
		if ((requirements.memoryTypeBits & (1u << type)) and (memory_properties.memoryTypes[type].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT))
		{
			found = true;
			break;
		}
	}

	if (not found)
	{
		spdlog::error("WaveVR: no device-local memory type for swapchain images");
		vk.DestroyImage(device, image, nullptr);
		return VK_NULL_HANDLE;
	}

	VkMemoryAllocateInfo allocate_info{
	        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
	        .pNext = nullptr,
	        .allocationSize = requirements.size,
	        .memoryTypeIndex = type,
	};

	if (vk.AllocateMemory(device, &allocate_info, nullptr, memory) != VK_SUCCESS)
	{
		spdlog::error("WaveVR: vkAllocateMemory failed for a swapchain image");
		vk.DestroyImage(device, image, nullptr);
		return VK_NULL_HANDLE;
	}

	if (vk.BindImageMemory(device, image, *memory, 0) != VK_SUCCESS)
	{
		spdlog::error("WaveVR: vkBindImageMemory failed for a swapchain image");
		vk.FreeMemory(device, *memory, nullptr);
		*memory = VK_NULL_HANDLE;
		vk.DestroyImage(device, image, nullptr);
		return VK_NULL_HANDLE;
	}

	return image;
}
} // namespace

swapchain * swapchain_from(XrSwapchain handle)
{
	return object_from_handle<swapchain>(reinterpret_cast<uintptr_t>(handle), swapchain_magic);
}

XrResult enumerate_swapchain_formats(XrSession handle, uint32_t capacity, uint32_t * count, int64_t * formats)
{
	if (not session_from(handle))
		return XR_ERROR_HANDLE_INVALID;

	// Wave owns the presented textures (WVR_ObtainTextureQueue) and creates them
	// as RGBA8. The client's scenes only accept sRGB swapchain formats; an sRGB
	// view over the shim's UNORM array image is a compatible-format view creation.
	static constexpr std::array<int64_t, 3> supported = {
	        VK_FORMAT_R8G8B8A8_SRGB,
	        VK_FORMAT_B8G8R8A8_SRGB,
	        VK_FORMAT_R8G8B8A8_UNORM,
	};

	if (capacity == 0)
	{
		*count = static_cast<uint32_t>(supported.size());
		return XR_SUCCESS;
	}

	*count = std::min(capacity, static_cast<uint32_t>(supported.size()));
	std::copy_n(supported.begin(), *count, formats);
	return XR_SUCCESS;
}

XrResult create_swapchain(XrSession handle, const XrSwapchainCreateInfo * create_info, XrSwapchain * swapchain_handle)
{
	auto * s = session_from(handle);
	if (not s)
		return XR_ERROR_HANDLE_INVALID;

	if (create_info->sampleCount != 1)
		return XR_ERROR_VALIDATION_FAILURE;

	if (create_info->arraySize == 0 or create_info->arraySize > 2)
	{
		spdlog::error("WaveVR: unsupported swapchain arraySize {}", create_info->arraySize);
		return XR_ERROR_VALIDATION_FAILURE;
	}

	if (s->vk_device == VK_NULL_HANDLE)
	{
		spdlog::error("WaveVR: xrCreateSwapchainKHR called before the Vulkan device exists");
		return XR_ERROR_CALL_ORDER_INVALID;
	}

	const device_vulkan & vk = device_entries(s->vk_device);
	if (vk.CreateImage == nullptr)
	{
		spdlog::error("WaveVR: device Vulkan entry points unavailable");
		return XR_ERROR_RUNTIME_FAILURE;
	}

	auto sc = std::make_unique<swapchain>(*s);
	sc->array_size = create_info->arraySize;
	sc->width = create_info->width;
	sc->height = create_info->height;
	sc->format = static_cast<VkFormat>(create_info->format);
	sc->usage_flags = create_info->usageFlags;

	wvr::api * api = wvr::api::get();
	sc->image_count = 0;
	for (uint32_t i = 0; i < sc->array_size; ++i)
	{
		sc->queues[i] = api->ObtainTextureQueue(
		        wvr::TextureTarget_VULKAN,
		        wvr::TextureFormat_RGBA,
		        wvr::TextureType_UnsignedByte,
		        sc->width,
		        sc->height,
		        0);

		if (sc->queues[i] == nullptr)
		{
			spdlog::error("WaveVR: WVR_ObtainTextureQueue failed for {}x{} eye {}", sc->width, sc->height, i);
			return XR_ERROR_RUNTIME_FAILURE;
		}

		uint32_t length = api->GetTextureQueueLength(sc->queues[i]);
		sc->image_count = sc->image_count == 0 ? length : std::min(sc->image_count, length);
	}

	spdlog::info("WaveVR: texture queues for {} eyes are {}x{}, {} images each", sc->array_size, sc->width, sc->height, sc->image_count);
	for (uint32_t i = 0; i < sc->image_count; ++i)
	{
		VkDeviceMemory memory = VK_NULL_HANDLE;
		VkImage image = create_image(vk, s->vk_device, s->vk_physical_device, sc->width, sc->height, sc->array_size, &memory);
		if (image == VK_NULL_HANDLE)
			return XR_ERROR_RUNTIME_FAILURE;
		sc->images.push_back(image);
		sc->memory.push_back(memory);
	}

	uint32_t family = graphics_queue_family(s->vk_physical_device);
	VkCommandPoolCreateInfo pool_info{
	        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
	        .pNext = nullptr,
	        .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
	        .queueFamilyIndex = family,
	};
	if (vk.CreateCommandPool(s->vk_device, &pool_info, nullptr, &sc->command_pool) != VK_SUCCESS)
	{
		spdlog::error("WaveVR: vkCreateCommandPool failed for the presentation copies");
		return XR_ERROR_RUNTIME_FAILURE;
	}

	VkCommandBufferAllocateInfo buffer_info{
	        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
	        .pNext = nullptr,
	        .commandPool = sc->command_pool,
	        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
	        .commandBufferCount = 1,
	};
	if (vk.AllocateCommandBuffers(s->vk_device, &buffer_info, &sc->command_buffer) != VK_SUCCESS)
	{
		spdlog::error("WaveVR: vkAllocateCommandBuffers failed for the presentation copies");
		return XR_ERROR_RUNTIME_FAILURE;
	}

	*swapchain_handle = handle_from<XrSwapchain>(sc.get());
	s->swapchains.push_back(std::move(sc));
	return XR_SUCCESS;
}

XrResult destroy_swapchain(XrSwapchain handle)
{
	auto * sc = swapchain_from(handle);
	if (not sc)
		return XR_ERROR_HANDLE_INVALID;

	wvr::api * api = wvr::api::get();
	// Wave 3.1.5's WVR_ReleaseTextureQueue trampoline resolves to a null
	// runtime entry point and faults (pc=0). The queues are owned by Wave
	// and reclaimed when the process exits, so releasing them here is
	// optional; skip it to keep teardown clean.
	(void)api;

	VkDevice device = sc->session.vk_device;
	const device_vulkan & vk = device_entries(device);
	for (VkImage image: sc->images)
		vk.DestroyImage(device, image, nullptr);
	for (VkDeviceMemory memory: sc->memory)
		vk.FreeMemory(device, memory, nullptr);
	if (sc->command_pool != VK_NULL_HANDLE)
		vk.DestroyCommandPool(device, sc->command_pool, nullptr);

	// Erase only this swapchain: erase(first, end()) would destroy every
	// swapchain created after it, invalidating handles the client still uses.
	std::erase_if(sc->session.swapchains, [sc](const std::unique_ptr<swapchain> & s) { return s.get() == sc; });
	return XR_SUCCESS;
}

XrResult enumerate_swapchain_images(XrSwapchain handle, uint32_t capacity, uint32_t * count, XrSwapchainImageBaseHeader * images)
{
	auto * sc = swapchain_from(handle);
	if (not sc)
		return XR_ERROR_HANDLE_INVALID;

	if (capacity == 0)
	{
		*count = static_cast<uint32_t>(sc->images.size());
		return XR_SUCCESS;
	}

	*count = std::min(capacity, static_cast<uint32_t>(sc->images.size()));
	auto * vulkan_images = reinterpret_cast<XrSwapchainImageVulkanKHR *>(images);
	for (uint32_t i = 0; i < *count; ++i)
	{
		vulkan_images[i].type = XR_TYPE_SWAPCHAIN_IMAGE_VULKAN_KHR;
		vulkan_images[i].next = nullptr;
		vulkan_images[i].image = sc->images[i];
	}

	if (capacity < sc->images.size())
		return XR_ERROR_SIZE_INSUFFICIENT;

	return XR_SUCCESS;
}

XrResult acquire_swapchain_image(XrSwapchain handle, const XrSwapchainImageAcquireInfo *, uint32_t * index)
{
	auto * sc = swapchain_from(handle);
	if (not sc)
		return XR_ERROR_HANDLE_INVALID;

	if (sc->acquired_index >= 0)
		return XR_ERROR_CALL_ORDER_INVALID;

	// WVR_GetAvailableTextureIndex is non-blocking: it returns -1 when Wave has
	// not handed a texture back yet. Poll (with a short sleep) until one frees;
	// this is what paces the frame loop to Wave's composition rate.
	//
	// Wave recycles textures only for the queues it composites, and only a queue
	// that xrEndFrame submits to Wave can be composited. A queue whose layers Wave
	// cannot present (quad layers) stops offering textures after the first one, so
	// render that swapchain locally: stalling on it would pace the frame loop to
	// nothing for a layer xrEndFrame drops anyway. A queue Wave composites always
	// has a texture ready, so it needs no grace poll; its long poll is the frame
	// pacing and survives the runtime taking the display away.
	wvr::api * api = wvr::api::get();
	const int poll_limit = sc->submitted_to_wave ? 10000 : 0;
	int32_t available = api->GetAvailableTextureIndex(sc->queues[0]);
	int polls = 0;
	for (; available < 0 && polls < poll_limit; ++polls)
	{
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
		available = api->GetAvailableTextureIndex(sc->queues[0]);
		if (available < 0 && polls % 500 == 499)
		{
			std::string state;
			for (uint32_t q = 0; q < sc->array_size; ++q)
				state += fmt::format(" queue{}={}/{}", q, api->GetAvailableTextureIndex(sc->queues[q]), api->GetTextureQueueLength(sc->queues[q]));
			spdlog::warn("WaveVR: no texture available after {}ms on {}x{} arraySize={} presented={},{}", polls + 1, sc->width, sc->height, sc->array_size, sc->presented_index, state);
		}
	}

	if (available < 0)
	{
		if (sc->submitted_to_wave)
		{
			spdlog::error("WaveVR: Wave never returned a texture to the queue; the runtime is not compositing this app");
			return XR_ERROR_RUNTIME_FAILURE;
		}

		if (not sc->rendered_locally)
		{
			spdlog::warn("WaveVR: presenting {}x{} arraySize={} swapchain locally; Wave never composites its queue", sc->width, sc->height, sc->array_size);
			sc->rendered_locally = true;
		}
		available = int32_t(sc->next_local_index);
		sc->next_local_index = (sc->next_local_index + 1) % sc->image_count;
	}
	else if (sc->rendered_locally)
	{
		// The runtime took the display away and handed it back: resume Wave
		// presentation for a swapchain that was emulated locally.
		spdlog::info("WaveVR: queue for {}x{} yields textures again; presenting through Wave", sc->width, sc->height);
		sc->rendered_locally = false;
	}

	// Wave-composited eyes must offer the same slot; a locally rendered
	// swapchain hands out its own round-robin index.
	if (not sc->rendered_locally)
	{
		for (uint32_t i = 1; i < sc->array_size; ++i)
		{
			int32_t other = api->GetAvailableTextureIndex(sc->queues[i]);
			if (other != available)
				spdlog::warn("WaveVR: eye {} offers texture {}, expected {}", i, other, available);
		}
	}

	sc->acquired_index = available;
	*index = static_cast<uint32_t>(available);
	return XR_SUCCESS;
}

XrResult wait_swapchain_image(XrSwapchain handle, const XrSwapchainImageWaitInfo *)
{
	auto * sc = swapchain_from(handle);
	if (not sc)
		return XR_ERROR_HANDLE_INVALID;

	// Pacing happens in acquire_swapchain_image (WVR_GetAvailableTextureIndex blocks).
	if (sc->acquired_index < 0)
		return XR_ERROR_CALL_ORDER_INVALID;

	return XR_SUCCESS;
}

XrResult release_swapchain_image(XrSwapchain handle, const XrSwapchainImageReleaseInfo *)
{
	auto * sc = swapchain_from(handle);
	if (not sc)
		return XR_ERROR_HANDLE_INVALID;

	if (sc->acquired_index < 0)
		return XR_ERROR_CALL_ORDER_INVALID;

	const int32_t index = sc->acquired_index;
	sc->acquired_index = -1;

	// Locally rendered swapchain: its Wave queue is never composited, so there is
	// nothing to copy into and nothing to submit. The client keeps rendering into
	// sc->images[index] and the layer is dropped in xrEndFrame.
	if (sc->rendered_locally)
		return XR_SUCCESS;

	// A second release without an intervening end_frame: submit the previous
	// copy before BeginCommandBuffer resets the one-time-submit buffer.
	if (sc->copy_pending)
		flush_pending_copies(sc->session);

	VkDevice device = sc->session.vk_device;
	const device_vulkan & vk = device_entries(device);
	if (vk.CmdCopyImage == nullptr or vk.QueueSubmit == nullptr or vk.GetDeviceQueue == nullptr)
		return XR_ERROR_RUNTIME_FAILURE;

	VkQueue queue = VK_NULL_HANDLE;
	vk.GetDeviceQueue(device, graphics_queue_family(sc->session.vk_physical_device), 0, &queue);
	if (queue == VK_NULL_HANDLE)
		return XR_ERROR_RUNTIME_FAILURE;

	VkCommandBufferBeginInfo begin_info{
	        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
	        .pNext = nullptr,
	        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
	        .pInheritanceInfo = nullptr,
	};
	if (vk.BeginCommandBuffer(sc->command_buffer, &begin_info) != VK_SUCCESS)
		return XR_ERROR_RUNTIME_FAILURE;

	VkImageSubresourceRange full{
	        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
	        .baseMipLevel = 0,
	        .levelCount = 1,
	        .baseArrayLayer = 0,
	        .layerCount = 1,
	};

	wvr::api * api = wvr::api::get();
	for (uint32_t layer = 0; layer < sc->array_size; ++layer)
	{
		VkImage destination = api->GetVkImage(sc->queues[layer], index);
		if (destination == VK_NULL_HANDLE)
		{
			spdlog::error("WaveVR: WVR_GetVkImage returned no image for eye {} index {}", layer, index);
			vk.EndCommandBuffer(sc->command_buffer);
			return XR_ERROR_RUNTIME_FAILURE;
		}

		// Source: the image the client rendered into; destination: Wave's texture.
		VkImageMemoryBarrier barriers[4] = {
		        VkImageMemoryBarrier{
		                .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
		                .pNext = nullptr,
		                .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
		                .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
		                .oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
		                .newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
		                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		                .image = sc->images[index],
		                .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, layer, 1},
		        },
		        VkImageMemoryBarrier{
		                .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
		                .pNext = nullptr,
		                .srcAccessMask = 0,
		                .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
		                // Wave owns this image and its layout is unknown to us: UNDEFINED
		                // discards it, GENERAL is a legal vkCmdCopyImage destination.
		                .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
		                .newLayout = VK_IMAGE_LAYOUT_GENERAL,
		                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		                .image = destination,
		                .subresourceRange = full,
		        },
		};

		vk.CmdPipelineBarrier(sc->command_buffer, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 2, barriers);

	VkImageCopy region{
	        // Fields are aspectMask, mipLevel, baseArrayLayer, layerCount: the eye
	        // index selects the array layer, the mip level is always 0.
	        .srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, layer, 1},
	        .srcOffset = {0, 0, 0},
	        .dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
	        .dstOffset = {0, 0, 0},
	        .extent = {sc->width, sc->height, 1},
	};
		vk.CmdCopyImage(sc->command_buffer, sc->images[index], VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, destination, VK_IMAGE_LAYOUT_GENERAL, 1, &region);

		barriers[0] = VkImageMemoryBarrier{
		        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
		        .pNext = nullptr,
		        .srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
		        .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
		        .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
		        .newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
		        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		        .image = sc->images[index],
		        .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, layer, 1},
		};
		barriers[1] = VkImageMemoryBarrier{
		        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
		        .pNext = nullptr,
		        .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
		        .dstAccessMask = 0,
		        // Hand the texture to Wave in the layout Wave's own renderer uses for
		        // texture-queue images. newLayout must not be UNDEFINED.
		        .oldLayout = VK_IMAGE_LAYOUT_GENERAL,
		        .newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
		        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		        .image = destination,
		        .subresourceRange = full,
		};

		vk.CmdPipelineBarrier(sc->command_buffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr, 0, nullptr, 2, barriers);
	}

	if (vk.EndCommandBuffer(sc->command_buffer) != VK_SUCCESS)
		return XR_ERROR_RUNTIME_FAILURE;

	// Do not submit yet: the client may record more rendering into
	// sc->images[index] after releasing it (the stream scene draws its GUI
	// overlay after releasing the swapchain). end_frame flushes the copy
	// through flush_pending_copies before WVR_SubmitFrame reads the texture.
	sc->copy_pending = true;

	sc->presented_index = index;
	return XR_SUCCESS;
}

void flush_pending_copies(session & s)
{
	for (auto & sc: s.swapchains)
	{
		if (not sc->copy_pending)
			continue;
		sc->copy_pending = false;

		VkDevice device = sc->session.vk_device;
		const device_vulkan & vk = device_entries(device);
		if (vk.QueueSubmit == nullptr or vk.GetDeviceQueue == nullptr)
			continue;

		VkQueue queue = VK_NULL_HANDLE;
		vk.GetDeviceQueue(device, graphics_queue_family(sc->session.vk_physical_device), 0, &queue);
		if (queue == VK_NULL_HANDLE)
			continue;

		VkSubmitInfo submit_info{
		        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
		        .pNext = nullptr,
		        .waitSemaphoreCount = 0,
		        .pWaitSemaphores = nullptr,
		        .pWaitDstStageMask = nullptr,
		        .commandBufferCount = 1,
		        .pCommandBuffers = &sc->command_buffer,
		        .signalSemaphoreCount = 0,
		        .pSignalSemaphores = nullptr,
		};
		if (vk.QueueSubmit(queue, 1, &submit_info, VK_NULL_HANDLE) != VK_SUCCESS)
		{
			spdlog::error("WaveVR: vkQueueSubmit failed for the presentation copy");
			continue;
		}

		// Wave reads the texture as soon as xrEndFrame submits it, so the copy
		// has to be complete before SubmitFrame runs.
		vk.QueueWaitIdle(queue);
	}
}

// Submit the recorded presentation copies with a fence and return without
// waiting. The graphics queue is FIFO behind the scene command buffer the
// client submitted at render_end, so the fence signals once every copy has
// landed in its Wave texture; the submit thread waits on it, keeping the
// frame loop off the GPU's critical path.
void submit_pending_copies(session & s, VkFence fence)
{
	for (auto & sc: s.swapchains)
	{
		if (not sc->copy_pending)
			continue;
		sc->copy_pending = false;

		VkDevice device = sc->session.vk_device;
		const device_vulkan & vk = device_entries(device);
		if (vk.QueueSubmit == nullptr or vk.GetDeviceQueue == nullptr)
			continue;

		VkQueue queue = VK_NULL_HANDLE;
		vk.GetDeviceQueue(device, graphics_queue_family(sc->session.vk_physical_device), 0, &queue);
		if (queue == VK_NULL_HANDLE)
			continue;

		VkSubmitInfo submit_info{
		        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
		        .pNext = nullptr,
		        .waitSemaphoreCount = 0,
		        .pWaitSemaphores = nullptr,
		        .pWaitDstStageMask = nullptr,
		        .commandBufferCount = 1,
		        .pCommandBuffers = &sc->command_buffer,
		        .signalSemaphoreCount = 0,
		        .pSignalSemaphores = nullptr,
		};
		if (vk.QueueSubmit(queue, 1, &submit_info, fence) != VK_SUCCESS)
		{
			spdlog::error("WaveVR: vkQueueSubmit failed for the presentation copy");
			continue;
		}
	}
}
} // namespace wave
