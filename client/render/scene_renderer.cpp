/*
 * WiVRn VR streaming
 * Copyright (C) 2023-2024 Guillaume Meunier <guillaume.meunier@centraliens.net>
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

#include "render/scene_renderer.h"

#include "application.h"
#include "hmd_traits.h"
#include "render/image_loader.h"
#include "render/scene_components.h"
#include "render/vertex_layout.h"
#include "utils/alignment.h"
#include "utils/fmt_glm.h"
#include "utils/ranges.h"
#include "vk/allocation.h"
#include "vk/pipeline.h"
#include "vk/shader.h"
#include "vk/specialization_constants.h"
#include <algorithm>
#include <boost/pfr/core.hpp>
#include <entt/entity/entity.hpp>
#include <entt/entt.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/gtc/matrix_access.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/quaternion.hpp>
#include <memory>
#include <numeric>
#include <ranges>
#include <spdlog/spdlog.h>
#include <vk_mem_alloc.h>
#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_enums.hpp>
#include <vulkan/vulkan_handles.hpp>
#include <vulkan/vulkan_structs.hpp>

namespace
{
struct frustum
{
	// Only 5 planes because the far plane is infinitely far
	std::array<glm::vec4, 5> planes;

	frustum() = default;
	frustum(const frustum &) = default;
	frustum(const glm::mat4 & proj)
	{
		/* Let [x',y',z',w'] = proj * [x,y,z,1]
		 * Extract the planes from the projection matrix s.t.
		 *
		 * (dot(planes[0], [x,y,z,1]) > 0 <=> x' < w'  <=> w' - x' > 0) <=> planes[0] = row3-row0
		 * (dot(planes[1], [x,y,z,1]) > 0 <=> x' > -w' <=> x' + w' > 0) <=> planes[1] = row0+row3
		 * (dot(planes[2], [x,y,z,1]) > 0 <=> y' < w'  <=> w' - y' > 0) <=> planes[2] = row3-row1
		 * (dot(planes[3], [x,y,z,1]) > 0 <=> y' > -w' <=> y' + w' > 0) <=> planes[3] = row1+row3
		 * (dot(planes[4], [x,y,z,1]) > 0 <=> z' < w'  <=> w' - z' > 0) <=> planes[4] = row3-row2
		 *
		 * See Gribb & Hartmann
		 */

		planes[0] = glm::row(proj, 3) - glm::row(proj, 0);
		planes[1] = glm::row(proj, 0) + glm::row(proj, 3);
		planes[2] = glm::row(proj, 3) - glm::row(proj, 1);
		planes[3] = glm::row(proj, 1) + glm::row(proj, 3);
		planes[4] = glm::row(proj, 3) - glm::row(proj, 2);
	}
};
} // namespace

// TODO move in lobby?
vk::Format scene_renderer::find_usable_image_format(
        vk::raii::PhysicalDevice physical_device,
        std::span<const vk::Format> formats,
        vk::Extent3D min_extent,
        vk::ImageUsageFlags usage,
        vk::ImageType type,
        vk::ImageTiling tiling,
        vk::ImageCreateFlags flags)
{
	for (vk::Format format: formats)
	{
		try
		{
			vk::ImageFormatProperties prop = physical_device.getImageFormatProperties(format, type, tiling, usage, flags);

			if (prop.maxExtent.width < min_extent.width)
				continue;
			if (prop.maxExtent.height < min_extent.height)
				continue;
			if (prop.maxExtent.depth < min_extent.depth)
				continue;

			return format;
		}
		catch (vk::FormatNotSupportedError &)
		{
			continue;
		}
	}

	return vk::Format::eUndefined;
}

std::shared_ptr<renderer::texture> scene_renderer::create_default_texture(image_loader & loader, std::vector<uint8_t> pixel, const std::string & name)
{
	vk::Format format;

	switch (pixel.size())
	{
		case 1:
			format = vk::Format::eR8Unorm;
			break;
		case 2:
			format = vk::Format::eR8G8Unorm;
			break;
		case 4:
			format = vk::Format::eR8G8B8A8Unorm;
			break;
		default:
			assert(false);
			__builtin_unreachable();
	}

	auto image = std::make_shared<loaded_image>(loader.load(pixel, vk::Extent3D{1, 1, 1}, format, name));
	std::shared_ptr<vk::raii::ImageView> image_view{image, &image->image_view};

	return std::make_shared<renderer::texture>(image_view, renderer::sampler_info{});
}

std::shared_ptr<renderer::material> scene_renderer::create_default_material()
{
	image_loader loader(device, physical_device, queue, queue_family_index);

	auto default_material = std::make_shared<renderer::material>();
	default_material->name = "default";

	default_material->base_color_texture = create_default_texture(loader, {255, 255, 255, 255}, "Default base color");
	default_material->metallic_roughness_texture = create_default_texture(loader, {255, 255}, "Default metallic roughness map");
	default_material->occlusion_texture = create_default_texture(loader, {255}, "Default occlusion map");
	default_material->emissive_texture = create_default_texture(loader, {0, 0, 0, 0}, "Default emissive color");
	default_material->normal_texture = create_default_texture(loader, {128, 128, 255, 255}, "Default normal map");

	default_material->buffer = std::make_shared<buffer_allocation>(
	        device,
	        vk::BufferCreateInfo{
	                .size = sizeof(default_material->staging),
	                .usage = vk::BufferUsageFlagBits::eUniformBuffer},
	        VmaAllocationCreateInfo{
	                .flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT,
	                .usage = VMA_MEMORY_USAGE_AUTO});
	memcpy(default_material->buffer->map(), &default_material->staging, sizeof(default_material->staging));
	default_material->buffer->unmap();

	return default_material;
}

static vk::FrontFace reverse(vk::FrontFace face)
{
	if (face == vk::FrontFace::eCounterClockwise)
		return vk::FrontFace::eClockwise;
	else
		return vk::FrontFace::eCounterClockwise;
}

// Return true if the OBB is outside the frustum
static bool frustum_cull(
        const frustum & fru,
        const glm::mat4 & model,
        const glm::vec3 & obb_min,
        const glm::vec3 & obb_max)
{
	for (const glm::vec4 & plane: fru.planes)
	{
		int out = 0;
		out += glm::dot(plane, model * glm::vec4(obb_min.x, obb_min.y, obb_min.z, 1)) < 0;
		out += glm::dot(plane, model * glm::vec4(obb_min.x, obb_min.y, obb_max.z, 1)) < 0;
		out += glm::dot(plane, model * glm::vec4(obb_min.x, obb_max.y, obb_min.z, 1)) < 0;
		out += glm::dot(plane, model * glm::vec4(obb_min.x, obb_max.y, obb_max.z, 1)) < 0;
		out += glm::dot(plane, model * glm::vec4(obb_max.x, obb_min.y, obb_min.z, 1)) < 0;
		out += glm::dot(plane, model * glm::vec4(obb_max.x, obb_min.y, obb_max.z, 1)) < 0;
		out += glm::dot(plane, model * glm::vec4(obb_max.x, obb_max.y, obb_min.z, 1)) < 0;
		out += glm::dot(plane, model * glm::vec4(obb_max.x, obb_max.y, obb_max.z, 1)) < 0;

		if (out == 8)
			return true;
	}

	return false;
}

static std::array layout_bindings_0{
        vk::DescriptorSetLayoutBinding{
                // scene_ubo: per-frame/view data
                .binding = 0,
                .descriptorType = vk::DescriptorType::eUniformBuffer,
                .descriptorCount = 1,
                .stageFlags = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
        },
        vk::DescriptorSetLayoutBinding{
                // mesh_ubo: per-instance data
                .binding = 1,
                .descriptorType = vk::DescriptorType::eUniformBuffer,
                .descriptorCount = 1,
                .stageFlags = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
        },
        vk::DescriptorSetLayoutBinding{
                // joints_ubo: skeletal animation
                .binding = 2,
                .descriptorType = vk::DescriptorType::eUniformBuffer,
                .descriptorCount = 1,
                .stageFlags = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
        },
        vk::DescriptorSetLayoutBinding{
                // base_color
                .binding = 3,
                .descriptorType = vk::DescriptorType::eCombinedImageSampler,
                .descriptorCount = 1,
                .stageFlags = vk::ShaderStageFlagBits::eFragment,
        },
        vk::DescriptorSetLayoutBinding{
                // metallic_roughness
                .binding = 4,
                .descriptorType = vk::DescriptorType::eCombinedImageSampler,
                .descriptorCount = 1,
                .stageFlags = vk::ShaderStageFlagBits::eFragment,
        },
        vk::DescriptorSetLayoutBinding{
                // occlusion
                .binding = 5,
                .descriptorType = vk::DescriptorType::eCombinedImageSampler,
                .descriptorCount = 1,
                .stageFlags = vk::ShaderStageFlagBits::eFragment,
        },
        vk::DescriptorSetLayoutBinding{
                // emissive
                .binding = 6,
                .descriptorType = vk::DescriptorType::eCombinedImageSampler,
                .descriptorCount = 1,
                .stageFlags = vk::ShaderStageFlagBits::eFragment,
        },
        vk::DescriptorSetLayoutBinding{
                // normal_map
                .binding = 7,
                .descriptorType = vk::DescriptorType::eCombinedImageSampler,
                .descriptorCount = 1,
                .stageFlags = vk::ShaderStageFlagBits::eFragment,
        },
        vk::DescriptorSetLayoutBinding{
                // material_ubo
                .binding = 8,
                .descriptorType = vk::DescriptorType::eUniformBuffer,
                .descriptorCount = 1,
                .stageFlags = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
        },
};

scene_renderer::scene_renderer(
        vk::raii::Device & device,
        vk::raii::PhysicalDevice physical_device,
        thread_safe<vk::raii::Queue> & queue,
        uint32_t queue_family_index,
        int frames_in_flight) :
        physical_device(physical_device),
        device(device),
        physical_device_properties(physical_device.getProperties()),
        queue(queue),
        queue_family_index(queue_family_index),
        cb_pool(device,
                vk::CommandPoolCreateInfo{
                        .flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer,
                        .queueFamilyIndex = queue_family_index,
                }),
        shader_cache(device),
        layout_0(create_descriptor_set_layout(layout_bindings_0,
                                              application::get_hmd_traits().supports_push_descriptors
                                                      ? vk::DescriptorSetLayoutCreateFlagBits::ePushDescriptorKHR
                                                      : vk::DescriptorSetLayoutCreateFlags{})),
        push_descriptors(application::get_hmd_traits().supports_push_descriptors)
{
	// Create the default material
	default_material = create_default_material();

	if (not push_descriptors)
	{
		// DescriptorPoolSize::descriptorCount is a pool-wide total, not a per-set count.
		// Derive the per-set needs from the layout and multiply by the number of sets a frame
		// allocates before the pool is reset.
		static constexpr uint32_t max_sets = 2048;
		std::vector<vk::DescriptorPoolSize> pool_sizes;
		for (vk::DescriptorType type: {vk::DescriptorType::eUniformBuffer, vk::DescriptorType::eCombinedImageSampler, vk::DescriptorType::eStorageBuffer})
		{
			uint32_t per_set = 0;
			for (const auto & binding: layout_bindings_0)
				if (binding.descriptorType == type)
					per_set += binding.descriptorCount;
			if (per_set)
				pool_sizes.push_back(vk::DescriptorPoolSize{.type = type, .descriptorCount = per_set * max_sets});
		}
		descriptor_pool = vk::raii::DescriptorPool(
		        device,
		        vk::DescriptorPoolCreateInfo{
		                .flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet,
		                .maxSets = max_sets,
		                .poolSizeCount = uint32_t(pool_sizes.size()),
		                .pPoolSizes = pool_sizes.data(),
		        });
	}

	// Create Vulkan resources
	frame_resources.resize(frames_in_flight);

	std::vector<vk::raii::CommandBuffer> command_buffers = device.allocateCommandBuffers(
	        {
	                .commandPool = *cb_pool,
	                .level = vk::CommandBufferLevel::ePrimary,
	                .commandBufferCount = (uint32_t)frame_resources.size(),
	        });

	for (auto && [res, cb]: std::views::zip(frame_resources, command_buffers))
	{
		res.fence = device.createFence({.flags = vk::FenceCreateFlagBits::eSignaled});
		res.cb = std::move(cb);
	}
	query_pool = vk::raii::QueryPool(
	        device,
	        vk::QueryPoolCreateInfo{
	                .queryType = vk::QueryType::eTimestamp,
	                .queryCount = uint32_t(2 * frames_in_flight),
	        });

	std::array layouts{*layout_0};
	pipeline_layout = create_pipeline_layout(layouts);
}

scene_renderer::~scene_renderer()
{
	wait_idle();
}

void scene_renderer::wait_idle()
{
	std::vector<vk::Fence> fences(frame_resources.size());
	for (auto && [i, f]: utils::enumerate(frame_resources))
		fences[i] = *f.fence;

	// Every caller is a scene destructor, and ~scene_renderer calls this too:
	// a throwing destructor runs std::terminate and aborts the process. A fence
	// that does not signal while the device is going away deserves a log, not an
	// abort. vk::raii::Device::waitForFences throws on any non-success result.
	try
	{
		vk::Result result = device.waitForFences(fences, true, 1'000'000'000);
		if (result != vk::Result::eSuccess)
			spdlog::error("vkWaitForfences: {}", vk::to_string(result));
	}
	catch (const vk::SystemError & e)
	{
		spdlog::error("vkWaitForfences: {}", e.what());
	}
}

scene_renderer::renderpass & scene_renderer::get_renderpass(const renderpass_info & info)
{
	auto it = renderpasses.find(info);
	if (it != renderpasses.end())
		return it->second;

	return renderpasses.emplace(info, create_renderpass(info)).first->second;
}

scene_renderer::renderpass scene_renderer::create_renderpass(const renderpass_info & info_)
{
	vk::StructureChain<vk::RenderPassCreateInfo, vk::RenderPassMultiviewCreateInfo> info;
	scene_renderer::renderpass rp;

	std::vector<vk::AttachmentDescription> attachments;

	rp.color_attachment = {
	        .attachment = (uint32_t)attachments.size(),
	        .layout = vk::ImageLayout::eColorAttachmentOptimal,
	};
	attachments.push_back(vk::AttachmentDescription{
	        .format = info_.color_format,
	        .samples = info_.msaa_samples,
	        .loadOp = info_.clear_color ? vk::AttachmentLoadOp::eClear : vk::AttachmentLoadOp::eLoad,
	        .storeOp = vk::AttachmentStoreOp::eStore,
	        // When loading, the image must already be in the attachment layout:
	        // compositing passes transition it with an explicit barrier.
	        .initialLayout = info_.clear_color ? vk::ImageLayout::eUndefined : vk::ImageLayout::eColorAttachmentOptimal,
	        .finalLayout = vk::ImageLayout::eColorAttachmentOptimal,
	});

	if (info_.depth_attachment)
	{
		rp.depth_attachment = vk::AttachmentReference{
		        .attachment = (uint32_t)attachments.size(),
		        .layout = vk::ImageLayout::eDepthStencilAttachmentOptimal,
		};
		attachments.push_back(vk::AttachmentDescription{
		        .format = info_.depth_format,
		        .samples = info_.msaa_samples,
		        .loadOp = vk::AttachmentLoadOp::eClear,
		        .storeOp = info_.keep_depth_buffer ? vk::AttachmentStoreOp::eStore : vk::AttachmentStoreOp::eDontCare,
		        .stencilLoadOp = vk::AttachmentLoadOp::eClear,
		        .stencilStoreOp = vk::AttachmentStoreOp::eDontCare,
		        .initialLayout = vk::ImageLayout::eUndefined,
		        .finalLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal,
		});
	}

	// Only used if MSAA is enabled
	if (info_.msaa_samples != vk::SampleCountFlagBits::e1)
	{
		rp.resolve_attachment = {
		        .attachment = (uint32_t)attachments.size(),
		        .layout = vk::ImageLayout::eColorAttachmentOptimal,
		};
		attachments.push_back(vk::AttachmentDescription{
		        .format = info_.color_format,
		        .samples = vk::SampleCountFlagBits::e1,
		        .loadOp = vk::AttachmentLoadOp::eDontCare,
		        .storeOp = vk::AttachmentStoreOp::eStore,
		        .stencilLoadOp = vk::AttachmentLoadOp::eClear,
		        .stencilStoreOp = vk::AttachmentStoreOp::eDontCare,
		        .initialLayout = vk::ImageLayout::eUndefined,
		        .finalLayout = vk::ImageLayout::eColorAttachmentOptimal,
		});
	}

	info.get().setAttachments(attachments);

	vk::SubpassDescription subpasses{
	        .pipelineBindPoint = vk::PipelineBindPoint::eGraphics,
	        .colorAttachmentCount = 1,
	        .pColorAttachments = &rp.color_attachment,
	        .pDepthStencilAttachment = rp.depth_attachment ? &*rp.depth_attachment : nullptr,
	};

	if (info_.msaa_samples != vk::SampleCountFlagBits::e1)
		subpasses.pResolveAttachments = &*rp.resolve_attachment;

	info.get().setSubpasses(subpasses);

	std::array dependencies{
	        vk::SubpassDependency{
	                .srcSubpass = vk::SubpassExternal,
	                .dstSubpass = 0,
	                .srcStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput,
	                .dstStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput,
	                // The load operation must see the color writes of whatever
	                // filled the image before this pass: a previous render pass
	                // in this command buffer (the compositing passes load the
	                // world image) or an already-submitted command buffer. An
	                // empty srcAccessMask orders nothing, and Adreno 540 then
	                // loads stale tiles, wiping the image.
	                .srcAccessMask = vk::AccessFlagBits::eColorAttachmentWrite,
	                .dstAccessMask = vk::AccessFlagBits::eColorAttachmentWrite,
	        },
	        vk::SubpassDependency{
	                .srcSubpass = vk::SubpassExternal,
	                .dstSubpass = 0,
	                .srcStageMask = vk::PipelineStageFlagBits::eEarlyFragmentTests,
	                .dstStageMask = vk::PipelineStageFlagBits::eEarlyFragmentTests,
	                .srcAccessMask = vk::AccessFlagBits::eDepthStencilAttachmentWrite,
	                .dstAccessMask = vk::AccessFlagBits::eDepthStencilAttachmentWrite,
	        }};
	info.get().setDependencies(dependencies);

	if (info_.multiview_count > 1)
	{
		auto & multiview_info = info.get<vk::RenderPassMultiviewCreateInfo>();
		std::array view_mask{uint32_t((1 << info_.multiview_count) - 1)};
		multiview_info.setViewMasks(view_mask);
		std::array correlation_mask{uint32_t((1 << info_.multiview_count) - 1)};
		multiview_info.setCorrelationMasks(correlation_mask);
	}
	else
		info.unlink<vk::RenderPassMultiviewCreateInfo>();

	rp.attachment_count = (uint32_t)attachments.size();
	rp.renderpass = vk::raii::RenderPass(device, info.get());
	return rp;
}

scene_renderer::output_image & scene_renderer::get_output_image_data(const output_image_info & info)
{
	auto it = output_images.find(info);
	if (it != output_images.end())
		return it->second;

	return output_images.emplace(info, create_output_image_data(info)).first->second;
}

scene_renderer::output_image scene_renderer::create_output_image_data(const output_image_info & info)
{
	assert(info.renderpass.multiview_count >= 1);
	assert(info.renderpass.view_count % info.renderpass.multiview_count == 0);

	// Swapchain image array size, views rendered by a single pass, and the resulting
	// number of render passes (one per eye when multiview is unavailable).
	const uint32_t layers = info.renderpass.view_count;
	const uint32_t views_per_pass = info.renderpass.multiview_count;
	const uint32_t pass_count = layers / views_per_pass;

	auto & rp = get_renderpass(info.renderpass);

	output_image out;

	// TODO: use image view from xr::swapchain
	// One view per render pass: the whole image array with multiview, a single layer without.
	for (uint32_t pass = 0; pass < pass_count; ++pass)
		out.image_views.push_back(vk::raii::ImageView(
		        device, vk::ImageViewCreateInfo{
		                        .image = info.color,
		                        .viewType = vk::ImageViewType::e2DArray,
		                        .format = info.renderpass.color_format,
		                        .components{},
		                        .subresourceRange = {
		                                .aspectMask = vk::ImageAspectFlagBits::eColor,
		                                .baseMipLevel = 0,
		                                .levelCount = 1,
		                                .baseArrayLayer = pass * views_per_pass,
		                                .layerCount = views_per_pass,
		                        },
		                }));

	// TODO: check requiredFlags
	if (not info.depth and info.renderpass.depth_attachment)
		out.depth_buffer = image_allocation{
		        device,
		        vk::ImageCreateInfo{
		                .imageType = vk::ImageType::e2D,
		                .format = info.renderpass.depth_format,
		                .extent = {
		                        .width = info.output_size.width,
		                        .height = info.output_size.height,
		                        .depth = 1,
		                },
		                .mipLevels = 1,
		                .arrayLayers = layers,
		                .samples = info.renderpass.msaa_samples,
		                .tiling = vk::ImageTiling::eOptimal,
		                .usage = vk::ImageUsageFlagBits::eDepthStencilAttachment | vk::ImageUsageFlagBits::eTransientAttachment,
		        },
		        VmaAllocationCreateInfo{
		                .flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT,
		                .usage = VMA_MEMORY_USAGE_AUTO,
		                .requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
		        }};

	if (info.renderpass.depth_attachment)
		for (uint32_t pass = 0; pass < pass_count; ++pass)
			out.depth_views.push_back(vk::raii::ImageView(
			        device,
			        vk::ImageViewCreateInfo{
			                .image = info.depth ? info.depth : out.depth_buffer,
			                .viewType = vk::ImageViewType::e2DArray,
			                .format = info.renderpass.depth_format,
			                .components{},
			                .subresourceRange = {
			                        .aspectMask = vk::ImageAspectFlagBits::eDepth,
			                        .baseMipLevel = 0,
			                        .levelCount = 1,
			                        .baseArrayLayer = pass * views_per_pass,
			                        .layerCount = views_per_pass,
			                },
			        }));

	if (info.renderpass.msaa_samples != vk::SampleCountFlagBits::e1)
	{
		out.multisample_image = image_allocation{
		        device,
		        vk::ImageCreateInfo{
		                .imageType = vk::ImageType::e2D,
		                .format = info.renderpass.color_format,
		                .extent = {
		                        .width = info.output_size.width,
		                        .height = info.output_size.height,
		                        .depth = 1,
		                },
		                .mipLevels = 1,
		                .arrayLayers = layers,
		                .samples = info.renderpass.msaa_samples,
		                .tiling = vk::ImageTiling::eOptimal,
		                .usage = vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eTransientAttachment,
		        },
		        VmaAllocationCreateInfo{
		                .flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT,
		                .usage = VMA_MEMORY_USAGE_AUTO,
		                .requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, // TODO: check
		                .preferredFlags = VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT,
		        }};

		for (uint32_t pass = 0; pass < pass_count; ++pass)
			out.multisample_views.push_back(vk::raii::ImageView(
			        device,
			        vk::ImageViewCreateInfo{
			                .image = out.multisample_image,
			                .viewType = vk::ImageViewType::e2DArray,
			                .format = info.renderpass.color_format,
			                .components{},
			                .subresourceRange = {
			                        .aspectMask = vk::ImageAspectFlagBits::eColor,
			                        .baseMipLevel = 0,
			                        .levelCount = 1,
			                        .baseArrayLayer = pass * views_per_pass,
			                        .layerCount = views_per_pass,
			                },
			        }));
	}

	// One framebuffer per render pass, with the layer count the render pass expects
	for (uint32_t pass = 0; pass < pass_count; ++pass)
	{
		vk::FramebufferCreateInfo fb_info{
		        .renderPass = *rp.renderpass,
		        .width = info.output_size.width,
		        .height = info.output_size.height,
		        .layers = views_per_pass,
		};

		std::vector<vk::ImageView> attachments;
		attachments.resize(rp.attachment_count);

		if (rp.resolve_attachment)
		{
			attachments[rp.color_attachment.attachment] = vk::ImageView{*out.multisample_views[pass]};
			attachments[rp.resolve_attachment->attachment] = vk::ImageView{*out.image_views[pass]};
		}
		else
		{
			attachments[rp.color_attachment.attachment] = vk::ImageView{*out.image_views[pass]};
		}

		if (rp.depth_attachment)
			attachments[rp.depth_attachment->attachment] = vk::ImageView{*out.depth_views[pass]};

		fb_info.setAttachments(attachments);
		out.framebuffers.push_back(vk::raii::Framebuffer(device, fb_info));
	}

	return out;
}

vk::raii::Pipeline & scene_renderer::get_pipeline(const pipeline_info & info)
{
	auto it = pipelines.find(info);
	if (it != pipelines.end())
		return it->second;

	return pipelines.emplace(info, create_pipeline(info)).first->second;
}

vk::raii::DescriptorSetLayout scene_renderer::create_descriptor_set_layout(std::span<vk::DescriptorSetLayoutBinding> bindings, vk::DescriptorSetLayoutCreateFlags flags)
{
	vk::DescriptorSetLayoutCreateInfo dsl_info{.flags = flags};
	dsl_info.setBindings(bindings);
	return vk::raii::DescriptorSetLayout{device, dsl_info};
}

vk::raii::PipelineLayout scene_renderer::create_pipeline_layout(std::span<vk::DescriptorSetLayout> layouts)
{
	vk::PipelineLayoutCreateInfo pipeline_layout_info;
	pipeline_layout_info.setSetLayouts(layouts);

	return vk::raii::PipelineLayout{device, pipeline_layout_info};
}

vk::raii::Pipeline scene_renderer::create_pipeline(const pipeline_info & info)
{
	spdlog::debug("Creating pipeline");

	// Non-multiview devices use the shader variant compiled without GL_EXT_multiview
	const std::string shader_suffix = application::get_hmd_traits().supports_multiview ? "" : ".nomv";
	auto vertex_shader = load_shader(device, info.vertex_shader_name + shader_suffix);
	auto fragment_shader = load_shader(device, info.fragment_shader_name + shader_suffix);

	auto specialization = make_specialization_constants(
	        int32_t(info.nb_texcoords),
	        VkBool32(info.dithering),
	        VkBool32(info.alpha_cutout),
	        VkBool32(info.skinning));

	return vk::raii::Pipeline{
	        device,
	        application::get_pipeline_cache(),
	        vk::pipeline_builder{
	                .Stages{
	                        vk::PipelineShaderStageCreateInfo{
	                                .stage = vk::ShaderStageFlagBits::eVertex,
	                                .module = *vertex_shader,
	                                .pName = "main",
	                                .pSpecializationInfo = specialization,

	                        },
	                        vk::PipelineShaderStageCreateInfo{
	                                .stage = vk::ShaderStageFlagBits::eFragment,
	                                .module = *fragment_shader,
	                                .pName = "main",
	                                .pSpecializationInfo = specialization,

	                        },
	                },
	                .VertexBindingDescriptions = info.vertex_layout.bindings,
	                .VertexAttributeDescriptions = info.vertex_layout.attributes,
	                .InputAssemblyState = {{
	                        .topology = info.topology,
	                        .primitiveRestartEnable = false,
	                }},
	                .Viewports = {vk::Viewport{}}, // Dynamic scissor / viewport but the count must be set
	                .Scissors = {vk::Rect2D{}},
	                .RasterizationState = {vk::PipelineRasterizationStateCreateInfo{
	                        .polygonMode = vk::PolygonMode::eFill,
	                        .cullMode = info.cull_mode,
	                        .frontFace = info.front_face,
	                        .lineWidth = 1.0,
	                }},
	                .MultisampleState = {vk::PipelineMultisampleStateCreateInfo{
	                        .rasterizationSamples = info.renderpass.msaa_samples,
	                }},
	                .DepthStencilState = {vk::PipelineDepthStencilStateCreateInfo{
	                        .depthTestEnable = info.depth_test_enable,
	                        .depthWriteEnable = info.depth_write_enable,
	                        .depthCompareOp = vk::CompareOp::eGreater,
	                        .depthBoundsTestEnable = false,
	                        .minDepthBounds = 0.0f,
	                        .maxDepthBounds = 1.0f,
	                }},
	                .ColorBlendState = {vk::PipelineColorBlendStateCreateInfo{}},
	                .ColorBlendAttachments = {vk::PipelineColorBlendAttachmentState{
	                        .blendEnable = info.blend_enable,
	                        .srcColorBlendFactor = vk::BlendFactor::eOne,
	                        .dstColorBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha,
	                        .colorBlendOp = vk::BlendOp::eAdd,
	                        .srcAlphaBlendFactor = vk::BlendFactor::eOne,
	                        .dstAlphaBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha,
	                        .alphaBlendOp = vk::BlendOp::eAdd,
	                        .colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA}},
	                .DynamicStates = {
	                        vk::DynamicState::eViewport,
	                        vk::DynamicState::eScissor,
	                },
	                .layout = *pipeline_layout,
	                .renderPass = *get_renderpass(info.renderpass).renderpass,
	                .subpass = 0,
	        }};
}

vk::Sampler scene_renderer::get_sampler(const renderer::sampler_info & info)
{
	auto it = samplers.find(info);
	if (it != samplers.end())
		return **it->second;

	auto out = std::make_shared<vk::raii::Sampler>(
	        device,
	        vk::SamplerCreateInfo{
	                .magFilter = info.mag_filter,
	                .minFilter = info.min_filter,
	                .mipmapMode = info.min_filter_mipmap,
	                .addressModeU = info.wrapS,
	                .addressModeV = info.wrapT,
	                // .anisotropyEnable = true,
	                // .maxAnisotropy = 4,
	                .minLod = 0,
	                .maxLod = vk::LodClampNone,
	        });

	samplers.emplace(info, out);
	return **out;
}

void scene_renderer::start_frame()
{
	current_frame_index = (current_frame_index + 1) % frame_resources.size();
	++frame_stamp;

	auto & f = current_frame();
	if (auto result = device.waitForFences(*f.fence, true, 1'000'000'000); result != vk::Result::eSuccess)
		throw std::runtime_error("vkWaitForfences: " + vk::to_string(result));
	device.resetFences(*f.fence);

	if (f.query_pool_filled)
	{
		auto [res, timestamps] = query_pool.getResults<uint64_t>(
		        current_frame_index * 2,
		        2,
		        2 * sizeof(uint64_t),
		        sizeof(uint64_t),
		        vk::QueryResultFlagBits::e64 | vk::QueryResultFlagBits::eWait);

		if (res == vk::Result::eSuccess)
		{
			gpu_time_s = (timestamps[1] - timestamps[0]) * application::get_physical_device_properties().limits.timestampPeriod / 1e9;

			static uint64_t gpu_log_counter = 0;
			if ((gpu_log_counter++ % 60) == 0)
				spdlog::info("scene gpu time {:.2f}ms", gpu_time_s);
		}
	}

	f.resources.clear();
	f.cb.begin({.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit});
	f.cb.resetQueryPool(*query_pool, current_frame_index * 2, 2);
	f.cb.writeTimestamp(vk::PipelineStageFlagBits::eTopOfPipe, *query_pool, current_frame_index * 2);

	f.uniform_buffer_offset = 0;

	// Canvas images are transitioned to shader-read-only once per frame
	gui_canvases_read_only.clear();

	if (not push_descriptors)
	{
		// Descriptor sets persist across frames; rewind the per-frame cursor
		f.descriptor_sets_used = 0;
	}
	f.nodes_dirty = true;

	if (!f.uniform_buffer)
	{
		// TODO multiple UBOs if it does not fit
		f.uniform_buffer = buffer_allocation{
		        device,
		        vk::BufferCreateInfo{
		                .size = 1048576,
		                .usage = vk::BufferUsageFlagBits::eUniformBuffer,
		        },
		        VmaAllocationCreateInfo{
		                .flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT,
		                .usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
		        },
		        "scene_renderer::render (UBO)"};
	}

	f.frame_stats = stats{};
}

void scene_renderer::end_frame()
{
	auto & f = current_frame();

	f.cb.writeTimestamp(vk::PipelineStageFlagBits::eTopOfPipe, *query_pool, current_frame_index * 2 + 1);
	f.cb.end();

	queue.lock()->submit(vk::SubmitInfo{
	                             .commandBufferCount = 1,
	                             .pCommandBuffers = &*f.cb,
	                             .signalSemaphoreCount = 0,
	                     },
	        *f.fence);
	f.query_pool_filled = true;
}

scene_renderer::per_frame_resources & scene_renderer::current_frame()
{
	return frame_resources[current_frame_index];
}

[[maybe_unused]] static void print_scene_hierarchy(const entt::registry & scene, entt::entity root = entt::null, int level = 0)
{
	if (level == 0)
		spdlog::info("Node hierarchy:");

	for (auto && [entity, node]: scene.view<components::node>().each())
	{
		if (node.parent != root)
			continue;

		// glm::mat4 M = model_matrices[index];
		// glm::vec4 pos = glm::column(M, 3);
		// spdlog::info("{:{}} {} pos={}, rot={}, pos to root={}", "", level * 2, node.name, node.translation, node.rotation, glm::vec3(pos));

		spdlog::info("{:{}} {} ({}, visible: {}, {})", "", level * 2, node.name, (int)entity, node.visible, node.global_visible);

		print_scene_hierarchy(scene, entity, level + 1);
	}
	if (level == 0)
		spdlog::info("---------------");
}

// Topological sort of all nodes
// static std::vector<entt::entity> sort_nodes(entt::registry & scene)
// {
// 	std::vector<std::pair<entt::entity, entt::entity>> unsorted; // first: child, second: parent
// 	std::unordered_set<entt::entity> processed;
// 	std::vector<entt::entity> sorted;
//
// 	for(auto && [entity, node]: scene.view<components::node>().each())
// 	{
// 		if (scene.valid(node.parent))
// 			unsorted.push_back({entity, node.parent});
// 		else
// 		{
// 			processed.emplace(entity);
// 			sorted.push_back(entity);
// 		}
// 	}
//
// 	while(not unsorted.empty())
// 	{
// 		for(auto it = unsorted.begin(); it != unsorted.end(); )
// 		{
// 			if (processed.contains(it->second))
// 			{
// 				processed.emplace(it->first);
// 				sorted.push_back(it->first);
// 				it = unsorted.erase(it);
// 			}
// 			else
// 				++it;
// 		}
// 	}
//
// 	return sorted;
// }

void scene_renderer::render(
        entt::registry & scene,
        const std::array<float, 4> & clear_color,
        uint32_t layer_mask,
        vk::Extent2D output_size,
        vk::Format color_format,
        vk::Format depth_format,
        vk::Image color_buffer,
        vk::Image depth_buffer,
        std::span<frame_info> frames,
        bool render_debug_draws,
        bool clear_color_attachment)
{
	assert(frames.size() <= instance_gpu_data{}.modelview.size());

	per_frame_resources & resources = current_frame();

	size_t buffer_alignment = std::max<size_t>(sizeof(glm::mat4), physical_device_properties.limits.minUniformBufferOffsetAlignment);
	// size_t buffer_alignment = std::max<size_t>(sizeof(glm::mat4), physical_device_properties.limits.minStorageBufferOffsetAlignment);

	vk::raii::CommandBuffer & cb = resources.cb;

	uint8_t * ubo = resources.uniform_buffer.data();

	std::array<vk::ClearValue, 2> clear_values{
	        vk::ClearColorValue{clear_color},
	        vk::ClearDepthStencilValue{0.0, 0},
	};

	// TODO once per frame, even if render is called several times
	if (resources.nodes_dirty)
	{
		// Recompute only the subtrees whose ancestors changed since this frame's
		// stamp; cached ancestors carry valid global_* values
		std::vector<components::node *> chain;
		for (auto && [entity, node]: scene.view<components::node>().each())
		{
			if (node.transform_stamp == frame_stamp)
				continue;

			chain.clear();
			components::node * cur = &node;
			while (cur != nullptr and cur->transform_stamp != frame_stamp)
			{
				chain.push_back(cur);
				cur = scene.try_get<components::node>(cur->parent);
			}

			glm::mat4 inherited = cur != nullptr ? cur->transform_to_root : glm::mat4(1.0);
			bool visible = cur != nullptr ? cur->global_visible : true;
			bool reverse_side = cur != nullptr ? cur->reverse_side : false;
			uint32_t layers = cur != nullptr ? cur->global_layer_mask : 0xffffffff;

			for (auto it = chain.rbegin(); it != chain.rend(); ++it)
			{
				components::node & n = **it;
				float det = n.scale.x * n.scale.y * n.scale.z;
				glm::mat4 transform_to_parent = glm::translate(glm::mat4(1), n.position) * (glm::mat4)n.orientation * glm::scale(glm::mat4(1), n.scale);
				n.transform_to_root = transform_to_parent * inherited;
				inherited = n.transform_to_root;
				visible = visible and n.visible;
				reverse_side = reverse_side ^ (det < 0);
				layers = layers bitand n.layer_mask;
				n.global_visible = visible;
				n.reverse_side = reverse_side;
				n.global_layer_mask = layers;
				n.transform_stamp = frame_stamp;
			}
		}
		resources.nodes_dirty = false;
	}

	// print_scene_hierarchy(scene);

	// Devices without VK_KHR_multiview render one pass per eye, with the eye's
	// matrices at uniform buffer index 0 (see the wivrn_view shader macro).
	const bool multiview = application::get_hmd_traits().supports_multiview;

	renderpass_info rp_info{
	        .color_format = color_format,
	        .depth_format = depth_format,
	        // The depth attachment is never read after the pass: no pass stores it
	        .keep_depth_buffer = false,
	        .msaa_samples = vk::SampleCountFlagBits::e1,
	        // .msaa_samples = vk::SampleCountFlagBits::e4, // FIXME: MSAA does not work
	        .multiview_count = multiview ? (uint32_t)frames.size() : 1u,
	        .view_count = (uint32_t)frames.size(),
	        .clear_color = clear_color_attachment,
	        .depth_attachment = depth_buffer != vk::Image{},
	};
	vk::raii::RenderPass & renderpass = get_renderpass(rp_info).renderpass;

	// Get the average view position/direction for sorting:
	// The distance from a given view is (view * xform_to_root * vec4(0,0,0,1)).z
	// =>   dot(view * xform_to_root * vec4(0,0,0,1), vec4(0,0,1,0))
	// =>   transpose(vec4(0,0,1,0)) * view * xform_to_root * vec(0,0,0,1)
	// =>   dot(row(view, 2), xform_to_root * vec(0,0,0,1))
	auto avg_view = 1.f / frames.size() * std::accumulate(frames.begin(), frames.end(), glm::vec4(0, 0, 0, 0), [](const glm::vec4 sum_view, const frame_info & frame) { return sum_view + glm::row(frame.view, 2); });

	// Compute the views frustum
	std::vector<frustum> frusta;
	for (const auto & frame: frames)
	{
		frusta.push_back(frustum{frame.projection * frame.view});
	}

	// Accumulate all visible primitives
	std::vector<std::tuple<bool, float, components::node *, renderer::primitive *>> primitives; // TODO keep it between frames
	for (auto && [entity, node]: scene.view<components::node>().each())
	{
		if (not node.global_visible or not node.mesh or (node.global_layer_mask & layer_mask) == 0)
			continue;

		for (renderer::primitive & primitive: node.mesh->primitives)
		{
			size_t nb_triangles;
			size_t nb_vertices = primitive.indexed ? primitive.index_count : primitive.vertex_count;
			switch (primitive.topology)
			{
				case vk::PrimitiveTopology::eTriangleList:
					nb_triangles = nb_vertices / 3;
					break;
				case vk::PrimitiveTopology::eTriangleFan:
					nb_triangles = nb_vertices - 2;
					break;

				case vk::PrimitiveTopology::eTriangleStrip:
					nb_triangles = nb_vertices - 2;
					break;

				default:
					nb_triangles = 0;
					break;
			}
			resources.frame_stats.count_primitives++;
			resources.frame_stats.count_triangles += nb_triangles;

			// Compute the primitive center
			glm::vec3 center = 0.5f * (primitive.obb_min + primitive.obb_max);

			// Position relative to the camera
			float position = glm::dot(avg_view, node.transform_to_root * glm::vec4(center, 1));

			renderer::material & material = primitive.material_ ? *primitive.material_ : *default_material;

			if (node.joints.empty())
			{
				bool visible = false;

				for (const auto & fru: frusta)
					visible = visible or not frustum_cull(fru, node.transform_to_root, primitive.obb_min, primitive.obb_max);

				if (visible)
					primitives.emplace_back(material.blend_enable, position, &node, &primitive);
				else
				{
					resources.frame_stats.count_culled_primitives++;
					resources.frame_stats.count_culled_triangles += nb_triangles;
				}
			}
			else
				primitives.emplace_back(material.blend_enable, position, &node, &primitive);
		}
	}

	// Sort by blending / distance
	std::ranges::stable_sort(primitives, [](const auto & a, const auto & b) -> bool {
		// Put the opaque objects first
		if (std::get<0>(a) < std::get<0>(b))
			return true;
		if (std::get<0>(a) > std::get<0>(b))
			return false;

		// If blending is disabled (std::get<0> == false), put the closest objects first
		// If blending is enabled (std::get<0> == true), put the farthest objects first
		return std::get<0>(a) ^ (std::get<1>(a) > std::get<1>(b));
	});


	scene_renderer::output_image & output = get_output_image_data(output_image_info{
	        .renderpass = rp_info,
	        .output_size = output_size,
	        .color = color_buffer,
	        .depth = depth_buffer,
	});

	std::array<glm::mat4, 2> viewproj;
	for (const auto && [frame_index, frame]: utils::enumerate(frames))
		viewproj[frame_index] = frame.projection * frame.view;

	// One render pass per eye when the device has no multiview support
	const uint32_t pass_count = multiview ? 1u : (uint32_t)frames.size();

	// Per-frame uniform buffer for a pass: every view with multiview, the pass's eye at index 0
	auto write_frame_ubo = [&](size_t view_index) -> vk::DeviceSize {
		vk::DeviceSize frame_ubo_offset = resources.uniform_buffer_offset;
		frame_gpu_data & frame_ubo = *reinterpret_cast<frame_gpu_data *>(ubo + resources.uniform_buffer_offset);
		resources.uniform_buffer_offset += utils::align_up(buffer_alignment, sizeof(frame_gpu_data));

		frame_ubo.ambient_color = glm::vec4(0.1, 0.1, 0.1, 0); // TODO
		frame_ubo.light_color = glm::vec4(0.8, 0.8, 0.8, 0);   // TODO
		frame_ubo.light_position = glm::vec4(1, 1, 1, 0);      // TODO

		if (multiview)
			for (const auto && [frame_index, frame]: utils::enumerate(frames))
				frame_ubo.view[frame_index] = frame.view;
		else
			frame_ubo.view[0] = frames[view_index].view;

		return frame_ubo_offset;
	};

	// Per-instance view matrices for a pass: every view with multiview, index 0 without
	auto write_instance_matrices = [&](instance_gpu_data & object_ubo, size_t view_index, const glm::mat4 & transform) {
		if (multiview)
			for (const auto && [frame_index, frame]: utils::enumerate(frames))
			{
				object_ubo.modelview[frame_index] = frame.view * transform;
				object_ubo.modelviewproj[frame_index] = viewproj[frame_index] * transform;
			}
		else
		{
			object_ubo.modelview[0] = frames[view_index].view * transform;
			object_ubo.modelviewproj[0] = viewproj[view_index] * transform;
		}
	};

	// Bind descriptors for a draw: pushed directly, or allocated from the pool and bound
	auto bind_descriptors = [&](std::span<vk::WriteDescriptorSet> descriptors) {
		if (push_descriptors)
		{
			cb.pushDescriptorSetKHR(vk::PipelineBindPoint::eGraphics, *pipeline_layout, 0, descriptors);
			return;
		}

		if (resources.descriptor_sets_used >= resources.descriptor_sets.size())
		{
			// Grow the set cache in batches; sets persist across frames
			std::vector<vk::DescriptorSetLayout> layouts(64, *layout_0);
			std::vector<vk::raii::DescriptorSet> fresh;
			try
			{
				fresh = device.allocateDescriptorSets(vk::DescriptorSetAllocateInfo{
				        .descriptorPool = *descriptor_pool,
				        .descriptorSetCount = uint32_t(layouts.size()),
				        .pSetLayouts = layouts.data()});
			}
			catch (const vk::OutOfPoolMemoryError & e)
			{
				(void)e;
				spdlog::error("Descriptor pool exhausted ({} sets), skipping draw", resources.descriptor_sets.size());
				return;
			}
			for (auto & s: fresh)
				resources.descriptor_sets.push_back(std::move(s));
		}

		vk::DescriptorSet dst = *resources.descriptor_sets[resources.descriptor_sets_used++];
		for (auto & d: descriptors)
			d.dstSet = dst;
		device.updateDescriptorSets(descriptors, {});
		cb.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *pipeline_layout, 0, dst, {});
	};

	// Draw the whole scene once for a render pass (view_index is ignored with multiview)
	auto draw_pass = [&](size_t view_index, vk::DeviceSize frame_ubo_offset) {
		// TODO try to add a depth pre-pass
		for (auto & [blend_enable, distance, node_ptr, primitive_ptr]: primitives)
		{
			components::node & node = *node_ptr;
			renderer::primitive & primitive = *primitive_ptr;

			glm::mat4 & transform = node.transform_to_root;

			// TODO: reuse the UBO if another primitive of the same mesh has already been drawn
			vk::DeviceSize instance_ubo_offset = resources.uniform_buffer_offset;
			vk::DeviceSize instance_ubo_size = sizeof(instance_gpu_data) + node.extra_shader_data.size();

			instance_gpu_data & object_ubo = *reinterpret_cast<instance_gpu_data *>(ubo + resources.uniform_buffer_offset);
			std::span<std::byte> extra_shader_data{reinterpret_cast<std::byte *>(ubo + resources.uniform_buffer_offset + sizeof(instance_gpu_data)), node.extra_shader_data.size()};

			resources.uniform_buffer_offset += utils::align_up(buffer_alignment, sizeof(instance_gpu_data) + node.extra_shader_data.size());

			vk::DeviceSize joints_ubo_offset = 0;
			if (!node.joints.empty())
			{
				joints_ubo_offset = resources.uniform_buffer_offset;
				glm::mat4 * joint_matrices = reinterpret_cast<glm::mat4 *>(ubo + resources.uniform_buffer_offset);
				resources.uniform_buffer_offset += utils::align_up(buffer_alignment, sizeof(glm::mat4) * 32);
				assert(node.joints.size() <= 32);

				for (auto && [idx, joint]: utils::enumerate(node.joints))
				{
					glm::mat4 & joint_transform = scene.get<components::node>(joint.first).transform_to_root;
					joint_matrices[idx] = glm::inverse(transform) * joint_transform * joint.second;
				}
			}

			object_ubo.model = transform;
			write_instance_matrices(object_ubo, view_index, transform);
			std::ranges::copy(node.extra_shader_data, extra_shader_data.begin());

			// Get the material
			std::shared_ptr<renderer::material> material = primitive.material_ ? primitive.material_ : default_material;

			// Get the pipeline
			pipeline_info info{
			        .renderpass = rp_info,
			        .vertex_shader_name = primitive.vertex_shader,
			        .fragment_shader_name = material->fragment_shader,
			        .vertex_layout = primitive.layout,
			        .cull_mode = primitive.cull_mode,
			        .front_face = primitive.front_face,
			        .topology = primitive.topology,
			        .blend_enable = material->blend_enable,
			        .depth_test_enable = rp_info.depth_attachment,
			        .depth_write_enable = rp_info.depth_attachment,

			        .nb_texcoords = 2, // TODO
			        .skinning = !node.joints.empty(),
			};

			if (material->double_sided)
				info.cull_mode = vk::CullModeFlagBits::eNone;

			if (node.reverse_side)
				info.front_face = reverse(info.front_face);
#if WIVRN_WAVE_RUNTIME
			// scene::projection_matrix flips Y for the Wave compositor, which
			// reverses triangle winding; compensate so backface culling keeps
			// culling the same faces.
			info.front_face = reverse(info.front_face);
#endif

			cb.bindPipeline(vk::PipelineBindPoint::eGraphics, *get_pipeline(info));

			cb.setViewport(0, vk::Viewport{
			                          .x = 0,
			                          .y = 0,
			                          .width = (float)output_size.width,
			                          .height = (float)output_size.height,
			                          .minDepth = 0,
			                          .maxDepth = 1,
			                  });

			cb.setScissor(0, vk::Rect2D{
			                         .offset = {0, 0},
			                         .extent = output_size,
			                 });

			if (primitive.indexed)
				cb.bindIndexBuffer(*node.mesh->buffer, primitive.index_offset, primitive.index_type);

			std::vector<vk::Buffer> buffers;
			buffers.resize(primitive.vertex_offset.size(), (vk::Buffer)*node.mesh->buffer);
			cb.bindVertexBuffers(0, buffers, primitive.vertex_offset);

			vk::DescriptorBufferInfo buffer_info_1{
			        .buffer = resources.uniform_buffer,
			        .offset = frame_ubo_offset,
			        .range = sizeof(frame_gpu_data),
			};
			vk::DescriptorBufferInfo buffer_info_2{
			        .buffer = resources.uniform_buffer,
			        .offset = instance_ubo_offset,
			        .range = instance_ubo_size,
			};
			vk::DescriptorBufferInfo buffer_info_3{
			        .buffer = resources.uniform_buffer,
			        .offset = joints_ubo_offset,
			        .range = sizeof(glm::mat4) * 32,
			};
			vk::DescriptorBufferInfo buffer_info_4{
			        .buffer = *material->buffer,
			        .offset = material->offset,
			        .range = sizeof(renderer::material::gpu_data),
			};

			// glTF materials may omit any of the five textures, but the shader
			// samples all of them and the descriptor set layout requires all five
			// bindings. Fall back to the default material's 1x1 textures so the
			// descriptors never reference a null image view (Adreno 540 faults).
			auto f = [&](std::shared_ptr<renderer::texture> & texture,
			             std::shared_ptr<renderer::texture> & fallback) {
				const renderer::texture & t = texture ? *texture : *fallback;
				return vk::DescriptorImageInfo{
				        .sampler = get_sampler(t.sampler),
				        .imageView = **(t.image_view),
				        .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
				};
			};

			std::array write_ds_image{
			        f(material->base_color_texture, default_material->base_color_texture),
			        f(material->metallic_roughness_texture, default_material->metallic_roughness_texture),
			        f(material->occlusion_texture, default_material->occlusion_texture),
			        f(material->emissive_texture, default_material->emissive_texture),
			        f(material->normal_texture, default_material->normal_texture),
			};
			std::array descriptors{
			        vk::WriteDescriptorSet{
			                .dstBinding = 0,
			                .descriptorCount = 1,
			                .descriptorType = vk::DescriptorType::eUniformBuffer,
			                .pBufferInfo = &buffer_info_1,
			        },
			        vk::WriteDescriptorSet{
			                .dstBinding = 1,
			                .descriptorCount = 1,
			                .descriptorType = vk::DescriptorType::eUniformBuffer,
			                .pBufferInfo = &buffer_info_2,
			        },
			        vk::WriteDescriptorSet{
			                .dstBinding = 2,
			                .descriptorCount = 1,
			                .descriptorType = vk::DescriptorType::eUniformBuffer,
			                .pBufferInfo = &buffer_info_3,
			        },
			        vk::WriteDescriptorSet{
			                .dstBinding = 3,
			                .descriptorCount = 1,
			                .descriptorType = vk::DescriptorType::eCombinedImageSampler,
			                .pImageInfo = write_ds_image.data(),
			        },
			        vk::WriteDescriptorSet{
			                .dstBinding = 4,
			                .descriptorCount = 1,
			                .descriptorType = vk::DescriptorType::eCombinedImageSampler,
			                .pImageInfo = write_ds_image.data() + 1,
			        },
			        vk::WriteDescriptorSet{
			                .dstBinding = 5,
			                .descriptorCount = 1,
			                .descriptorType = vk::DescriptorType::eCombinedImageSampler,
			                .pImageInfo = write_ds_image.data() + 2,
			        },
			        vk::WriteDescriptorSet{
			                .dstBinding = 6,
			                .descriptorCount = 1,
			                .descriptorType = vk::DescriptorType::eCombinedImageSampler,
			                .pImageInfo = write_ds_image.data() + 3,
			        },
			        vk::WriteDescriptorSet{
			                .dstBinding = 7,
			                .descriptorCount = 1,
			                .descriptorType = vk::DescriptorType::eCombinedImageSampler,
			                .pImageInfo = write_ds_image.data() + 4,
			        },
			        vk::WriteDescriptorSet{
			                .dstBinding = 8,
			                .descriptorCount = 1,
			                .descriptorType = vk::DescriptorType::eUniformBuffer,
			                .pBufferInfo = &buffer_info_4,
			        },
			};

			bind_descriptors(descriptors);

			if (primitive.indexed)
				cb.drawIndexed(primitive.index_count, 1, 0, 0, 0);
			else
				cb.draw(primitive.vertex_count, 1, 0, 0);
		}

		if (render_debug_draws and not debug_draw_vertices.empty())
		{
			size_t size_bytes = std::span{debug_draw_vertices}.size_bytes();
			if (not resources.debug_draw or resources.debug_draw.info().size < size_bytes)
			{
				resources.debug_draw = buffer_allocation{
				        device,
				        vk::BufferCreateInfo{
				                .size = size_bytes,
				                .usage = vk::BufferUsageFlagBits::eVertexBuffer,
				        },
				        VmaAllocationCreateInfo{
				                .flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT,
				                .usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST,
				        },
				        "scene_renderer::render (debug draw)",
				};
			}

			memcpy(resources.debug_draw.map(), debug_draw_vertices.data(), size_bytes);

			renderer::vertex_layout vertex_layout;
			vertex_layout.add_vertex_attribute("Position", vk::Format::eR32G32B32A32Sfloat, 0, 0);
			vertex_layout.add_vertex_attribute("Color", vk::Format::eR32G32B32A32Sfloat, 0, 1);

			pipeline_info info{
			        .renderpass = rp_info,
			        .vertex_shader_name = "debug_draw.vert",
			        .fragment_shader_name = "debug_draw.frag",
			        .vertex_layout = vertex_layout,
			        .cull_mode = vk::CullModeFlagBits::eFrontAndBack,
			        .front_face = vk::FrontFace::eClockwise,
			        .topology = vk::PrimitiveTopology::eLineList,
			        .blend_enable = true,

			        .depth_test_enable = false,
			        .depth_write_enable = false,
			};

			cb.bindPipeline(vk::PipelineBindPoint::eGraphics, *get_pipeline(info));

			cb.setViewport(0, vk::Viewport{
			                          .x = 0,
			                          .y = 0,
			                          .width = (float)output_size.width,
			                          .height = (float)output_size.height,
			                          .minDepth = 0,
			                          .maxDepth = 1,
			                  });

			cb.setScissor(0, vk::Rect2D{
			                         .offset = {0, 0},
			                         .extent = output_size,
			                 });

			{
				vk::DeviceSize instance_ubo_offset = resources.uniform_buffer_offset;
				instance_gpu_data & object_ubo = *reinterpret_cast<instance_gpu_data *>(ubo + resources.uniform_buffer_offset);
				resources.uniform_buffer_offset += utils::align_up(buffer_alignment, sizeof(instance_gpu_data));

				object_ubo.model = glm::identity<glm::mat4>();
				write_instance_matrices(object_ubo, view_index, glm::identity<glm::mat4>());

				vk::DescriptorBufferInfo buffer_info_1{
				        .buffer = resources.uniform_buffer,
				        .offset = frame_ubo_offset,
				        .range = sizeof(frame_gpu_data),
				};
				vk::DescriptorBufferInfo buffer_info_2{
				        .buffer = resources.uniform_buffer,
				        .offset = instance_ubo_offset,
				        .range = sizeof(instance_gpu_data),
				};
				vk::DescriptorBufferInfo buffer_info_3{
				        .buffer = resources.uniform_buffer,
				        .offset = 0,
				        .range = sizeof(glm::mat4) * 32,
				};
				vk::DescriptorBufferInfo buffer_info_4{
				        .buffer = *default_material->buffer,
				        .offset = default_material->offset,
				        .range = sizeof(renderer::material::gpu_data),
				};

				auto f = [&](renderer::texture & texture) {
					return vk::DescriptorImageInfo{
					        .sampler = get_sampler(texture.sampler),
					        .imageView = **(texture.image_view),
					        .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
					};
				};

				std::array write_ds_image{
				        f(*default_material->base_color_texture),
				        f(*default_material->metallic_roughness_texture),
				        f(*default_material->occlusion_texture),
				        f(*default_material->emissive_texture),
				        f(*default_material->normal_texture),
				};
				std::array descriptors{
				        vk::WriteDescriptorSet{
				                .dstBinding = 0,
				                .descriptorCount = 1,
				                .descriptorType = vk::DescriptorType::eUniformBuffer,
				                .pBufferInfo = &buffer_info_1,
				        },
				        vk::WriteDescriptorSet{
				                .dstBinding = 1,
				                .descriptorCount = 1,
				                .descriptorType = vk::DescriptorType::eUniformBuffer,
				                .pBufferInfo = &buffer_info_2,
				        },
				        vk::WriteDescriptorSet{
				                .dstBinding = 2,
				                .descriptorCount = 1,
				                .descriptorType = vk::DescriptorType::eUniformBuffer,
				                .pBufferInfo = &buffer_info_3,
				        },
				        vk::WriteDescriptorSet{
				                .dstBinding = 3,
				                .descriptorCount = 1,
				                .descriptorType = vk::DescriptorType::eCombinedImageSampler,
				                .pImageInfo = write_ds_image.data(),
				        },
				        vk::WriteDescriptorSet{
				                .dstBinding = 4,
				                .descriptorCount = 1,
				                .descriptorType = vk::DescriptorType::eCombinedImageSampler,
				                .pImageInfo = write_ds_image.data() + 1,
				        },
				        vk::WriteDescriptorSet{
				                .dstBinding = 5,
				                .descriptorCount = 1,
				                .descriptorType = vk::DescriptorType::eCombinedImageSampler,
				                .pImageInfo = write_ds_image.data() + 2,
				        },
				        vk::WriteDescriptorSet{
				                .dstBinding = 6,
				                .descriptorCount = 1,
				                .descriptorType = vk::DescriptorType::eCombinedImageSampler,
				                .pImageInfo = write_ds_image.data() + 3,
				        },
				        vk::WriteDescriptorSet{
				                .dstBinding = 7,
				                .descriptorCount = 1,
				                .descriptorType = vk::DescriptorType::eCombinedImageSampler,
				                .pImageInfo = write_ds_image.data() + 4,
				        },
				        vk::WriteDescriptorSet{
				                .dstBinding = 8,
				                .descriptorCount = 1,
				                .descriptorType = vk::DescriptorType::eUniformBuffer,
				                .pBufferInfo = &buffer_info_4,
				        },
				};

				bind_descriptors(descriptors);
			}

			cb.bindVertexBuffers(0, (vk::Buffer)resources.debug_draw, (vk::DeviceSize)0);
			cb.draw(debug_draw_vertices.size(), 1, 0, 0);
		}
	};
	// Compositing pass: the color image was written by a previous render pass
	// of this command buffer. The load operation must see those writes, and the
	// barrier must be merge-friendly (no layout transition, ColorAttachment
	// output stages only) so Adreno fuses the passes into one tile pass instead
	// of storing and reloading the whole image between them.
	if (not clear_color_attachment)
		cb.pipelineBarrier(
		        vk::PipelineStageFlagBits::eColorAttachmentOutput,
		        vk::PipelineStageFlagBits::eColorAttachmentOutput,
		        {},
		        {},
		        {},
		        vk::ImageMemoryBarrier{
		                .srcAccessMask = vk::AccessFlagBits::eColorAttachmentWrite,
		                .dstAccessMask = vk::AccessFlagBits::eColorAttachmentWrite,
		                .oldLayout = vk::ImageLayout::eColorAttachmentOptimal,
		                .newLayout = vk::ImageLayout::eColorAttachmentOptimal,
		                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		                .image = color_buffer,
		                .subresourceRange = {
		                        .aspectMask = vk::ImageAspectFlagBits::eColor,
		                        .baseMipLevel = 0,
		                        .levelCount = 1,
		                        .baseArrayLayer = 0,
		                        .layerCount = VK_REMAINING_ARRAY_LAYERS,
		                },
		        });

	for (uint32_t pass = 0; pass < pass_count; ++pass)
	{
		const size_t view_index = multiview ? 0 : pass;
		vk::DeviceSize frame_ubo_offset = write_frame_ubo(view_index);

		cb.beginRenderPass(vk::RenderPassBeginInfo{
		                           .renderPass = *renderpass,
		                           .framebuffer = *output.framebuffers[pass],
		                           .renderArea = {
		                                   .offset = {0, 0},
		                                   .extent = output_size,
		                           },
		                           .clearValueCount = clear_values.size(),
		                           .pClearValues = clear_values.data(),
		                   },
		                   vk::SubpassContents::eInline);

		draw_pass(view_index, frame_ubo_offset);

		cb.endRenderPass();
	}
}

void scene_renderer::render_gui(
        std::span<const gui_quad> quads,
        vk::Extent2D output_size,
        vk::Format color_format,
        vk::Format depth_format,
        vk::Image color_buffer,
        vk::ImageLayout color_layout,
        vk::Extent2D canvas_size,
        std::span<frame_info> frames)
{
	if (quads.empty())
		return;

	per_frame_resources & resources = current_frame();
	vk::raii::CommandBuffer & cb = resources.cb;
	uint8_t * ubo = resources.uniform_buffer.data();
	size_t buffer_alignment = std::max<size_t>(sizeof(glm::mat4), physical_device_properties.limits.minUniformBufferOffsetAlignment);

	const bool multiview = application::get_hmd_traits().supports_multiview;

	renderpass_info rp_info{
	        .color_format = color_format,
	        .depth_format = depth_format,
	        .keep_depth_buffer = false,
	        .msaa_samples = vk::SampleCountFlagBits::e1,
	        .multiview_count = multiview ? (uint32_t)frames.size() : 1u,
	        .view_count = (uint32_t)frames.size(),
	        .clear_color = false,
	        .depth_attachment = false,
	};
	vk::raii::RenderPass & renderpass = get_renderpass(rp_info).renderpass;
	scene_renderer::output_image & output = get_output_image_data(output_image_info{
	        .renderpass = rp_info,
	        .output_size = output_size,
	        .color = color_buffer,
	        .depth = {},
	});

	// Destination image: whoever wrote it (a previous render pass of this
	// command buffer, or an already-submitted command buffer) must complete
	// before the load operation reads it. The render pass subpass dependency
	// has an empty srcAccessMask and orders nothing by itself.
	cb.pipelineBarrier(
	        color_layout == vk::ImageLayout::eColorAttachmentOptimal ? vk::PipelineStageFlagBits::eColorAttachmentOutput : vk::PipelineStageFlagBits::eAllCommands,
	        vk::PipelineStageFlagBits::eColorAttachmentOutput,
	        {},
	        {},
	        {},
	        vk::ImageMemoryBarrier{
	                .srcAccessMask = color_layout == vk::ImageLayout::eColorAttachmentOptimal ? vk::AccessFlagBits::eColorAttachmentWrite : vk::AccessFlagBits::eMemoryWrite,
	                .dstAccessMask = vk::AccessFlagBits::eColorAttachmentWrite,
	                .oldLayout = color_layout,
	                .newLayout = vk::ImageLayout::eColorAttachmentOptimal,
	                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
	                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
	                .image = color_buffer,
	                .subresourceRange = {
	                        .aspectMask = vk::ImageAspectFlagBits::eColor,
	                        .baseMipLevel = 0,
	                        .levelCount = 1,
	                        .baseArrayLayer = 0,
	                        .layerCount = VK_REMAINING_ARRAY_LAYERS,
	                },
	        });

	// The imgui canvas is created with the session swapchain format, which is
	// the projection format used here.
	auto canvas_view = [&](vk::Image canvas) -> vk::raii::ImageView & {
		auto it = gui_canvas_views.find(canvas);
		if (it != gui_canvas_views.end())
			return it->second;
		return gui_canvas_views
		        .emplace(
		                canvas,
		                vk::raii::ImageView{
		                        device,
		                        vk::ImageViewCreateInfo{
		                                .image = canvas,
		                                .viewType = vk::ImageViewType::e2D,
		                                .format = color_format,
		                                .subresourceRange = {
		                                        .aspectMask = vk::ImageAspectFlagBits::eColor,
		                                        .baseMipLevel = 0,
		                                        .levelCount = 1,
		                                        .baseArrayLayer = 0,
		                                        .layerCount = 1,
		                                },
		                        }})
		        .first->second;
	};

	// The canvas render pass wrote it as a color attachment in an
	// already-submitted command buffer; make it sampleable. Once per image per
	// frame: a second render_gui call in the same frame must not transition an
	// image that is already shader-read-only.
	for (const gui_quad & quad: quads)
	{
		if (not gui_canvases_read_only.insert(quad.canvas).second)
			continue;

		cb.pipelineBarrier(
		        vk::PipelineStageFlagBits::eColorAttachmentOutput,
		        vk::PipelineStageFlagBits::eFragmentShader,
		        {},
		        {},
		        {},
		        vk::ImageMemoryBarrier{
		                .srcAccessMask = vk::AccessFlagBits::eColorAttachmentWrite,
		                .dstAccessMask = vk::AccessFlagBits::eShaderRead,
		                .oldLayout = vk::ImageLayout::eColorAttachmentOptimal,
		                .newLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
		                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		                .image = quad.canvas,
		                .subresourceRange = {
		                        .aspectMask = vk::ImageAspectFlagBits::eColor,
		                        .baseMipLevel = 0,
		                        .levelCount = 1,
		                        .baseArrayLayer = 0,
		                        .layerCount = VK_REMAINING_ARRAY_LAYERS,
		                },
		        });
	}

	struct gui_vertex
	{
		glm::vec3 position;
		glm::vec2 texcoord;
	};

	static constexpr glm::vec2 corners[4] = {
	        {-0.5f, -0.5f},
	        {0.5f, -0.5f},
	        {-0.5f, 0.5f},
	        {0.5f, 0.5f},
	};

	std::vector<gui_vertex> vertices;
	vertices.reserve(quads.size() * 4);
	for (const gui_quad & quad: quads)
	{
		// Triangle strip order: bottom-left, bottom-right, top-left, top-right.
		// Canvas pixels start at the top-left, so the quad's top edge
		// (y = +0.5) maps to the first row of the source rectangle.
		const glm::vec2 rect_min{float(quad.src_rect.offset.x), float(quad.src_rect.offset.y)};
		const glm::vec2 rect_extent{float(quad.src_rect.extent.width), float(quad.src_rect.extent.height)};
		const glm::vec2 inv_canvas{1.f / float(canvas_size.width), 1.f / float(canvas_size.height)};

		for (const glm::vec2 & corner: corners)
			vertices.push_back(gui_vertex{
			        {corner.x, corner.y, 0.f},
			        (rect_min + glm::vec2{corner.x + 0.5f, 0.5f - corner.y} * rect_extent) * inv_canvas,
			});
	}

	size_t size_bytes = std::span{vertices}.size_bytes();
	if (not resources.gui_quad or resources.gui_quad.info().size < size_bytes)
		resources.gui_quad = buffer_allocation{
		        device,
		        vk::BufferCreateInfo{
		                .size = size_bytes,
		                .usage = vk::BufferUsageFlagBits::eVertexBuffer,
		        },
		        VmaAllocationCreateInfo{
		                .flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT,
		                .usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST,
		        },
		        "scene_renderer::render_gui",
		};
	memcpy(resources.gui_quad.map(), vertices.data(), size_bytes);

	renderer::vertex_layout vertex_layout;
	vertex_layout.add_vertex_attribute("Position", vk::Format::eR32G32B32Sfloat, 0, 0);
	vertex_layout.add_vertex_attribute("Texcoord", vk::Format::eR32G32Sfloat, 0, 1);

	pipeline_info info{
	        .renderpass = rp_info,
	        .vertex_shader_name = "imgui_gui.vert",
	        .fragment_shader_name = "imgui_gui.frag",
	        .vertex_layout = vertex_layout,
	        .cull_mode = vk::CullModeFlagBits::eNone,
	        .front_face = vk::FrontFace::eClockwise,
	        .topology = vk::PrimitiveTopology::eTriangleStrip,
	        .blend_enable = true,
	        .depth_test_enable = false,
	        .depth_write_enable = false,
	        .dithering = false,
	};
	vk::Pipeline pipeline = *get_pipeline(info);

	// Clamp to edge: the UV rectangle must not sample neighbouring windows
	renderer::sampler_info sampler{
	        .wrapS = vk::SamplerAddressMode::eClampToEdge,
	        .wrapT = vk::SamplerAddressMode::eClampToEdge,
	};
	vk::Sampler clamp_sampler = get_sampler(sampler);

	// Bind descriptors for a draw: pushed directly, or allocated from the pool
	// and bound (same contract as scene_renderer::render).
	auto bind_descriptors = [&](std::span<vk::WriteDescriptorSet> descriptors) {
		if (push_descriptors)
		{
			cb.pushDescriptorSetKHR(vk::PipelineBindPoint::eGraphics, *pipeline_layout, 0, descriptors);
			return;
		}

		if (resources.descriptor_sets_used >= resources.descriptor_sets.size())
		{
			// Grow the set cache in batches; sets persist across frames
			std::vector<vk::DescriptorSetLayout> layouts(64, *layout_0);
			std::vector<vk::raii::DescriptorSet> fresh;
			try
			{
				fresh = device.allocateDescriptorSets(vk::DescriptorSetAllocateInfo{
				        .descriptorPool = *descriptor_pool,
				        .descriptorSetCount = uint32_t(layouts.size()),
				        .pSetLayouts = layouts.data()});
			}
			catch (const vk::OutOfPoolMemoryError & e)
			{
				(void)e;
				spdlog::error("Descriptor pool exhausted ({} sets), skipping draw", resources.descriptor_sets.size());
				return;
			}
			for (auto & s: fresh)
				resources.descriptor_sets.push_back(std::move(s));
		}

		vk::DescriptorSet dst = *resources.descriptor_sets[resources.descriptor_sets_used++];
		for (auto & d: descriptors)
			d.dstSet = dst;
		device.updateDescriptorSets(descriptors, {});
		cb.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *pipeline_layout, 0, dst, {});
	};

	std::array<vk::ClearValue, 2> clear_values{
	        vk::ClearColorValue{0, 0, 0, 0},
	        vk::ClearDepthStencilValue{0.0, 0},
	};

	const uint32_t pass_count = multiview ? 1u : (uint32_t)frames.size();

	for (uint32_t pass = 0; pass < pass_count; ++pass)
	{
		const size_t view_index = multiview ? 0 : pass;

		vk::DeviceSize frame_ubo_offset = resources.uniform_buffer_offset;
		frame_gpu_data & frame_ubo = *reinterpret_cast<frame_gpu_data *>(ubo + resources.uniform_buffer_offset);
		resources.uniform_buffer_offset += utils::align_up(buffer_alignment, sizeof(frame_gpu_data));
		frame_ubo.ambient_color = glm::vec4(0.1, 0.1, 0.1, 0);
		frame_ubo.light_color = glm::vec4(0.8, 0.8, 0.8, 0);
		frame_ubo.light_position = glm::vec4(1, 1, 1, 0);
		if (multiview)
			for (const auto && [frame_index, frame]: utils::enumerate(frames))
				frame_ubo.view[frame_index] = frame.view;
		else
			frame_ubo.view[0] = frames[view_index].view;

		cb.beginRenderPass(vk::RenderPassBeginInfo{
		                           .renderPass = *renderpass,
		                           .framebuffer = *output.framebuffers[pass],
		                           .renderArea = {
		                                   .offset = {0, 0},
		                                   .extent = output_size,
		                           },
		                           .clearValueCount = clear_values.size(),
		                           .pClearValues = clear_values.data(),
		                   },
		                   vk::SubpassContents::eInline);

		cb.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline);
		cb.setViewport(0, vk::Viewport{
		                          .x = 0,
		                          .y = 0,
		                          .width = (float)output_size.width,
		                          .height = (float)output_size.height,
		                          .minDepth = 0,
		                          .maxDepth = 1,
		                  });
		cb.setScissor(0, vk::Rect2D{
		                         .offset = {0, 0},
		                         .extent = output_size,
		                 });
		cb.bindVertexBuffers(0, (vk::Buffer)resources.gui_quad, (vk::DeviceSize)0);

		for (size_t quad_index = 0; quad_index < quads.size(); ++quad_index)
		{
			const gui_quad & quad = quads[quad_index];

			vk::DeviceSize instance_ubo_offset = resources.uniform_buffer_offset;
			instance_gpu_data & object_ubo = *reinterpret_cast<instance_gpu_data *>(ubo + resources.uniform_buffer_offset);
			resources.uniform_buffer_offset += utils::align_up(buffer_alignment, sizeof(instance_gpu_data));
			object_ubo.model = quad.model;
			if (multiview)
				for (const auto && [frame_index, frame]: utils::enumerate(frames))
				{
					object_ubo.modelview[frame_index] = frame.view * quad.model;
					object_ubo.modelviewproj[frame_index] = frame.projection * frame.view * quad.model;
				}
			else
			{
				object_ubo.modelview[0] = frames[view_index].view * quad.model;
				object_ubo.modelviewproj[0] = frames[view_index].projection * frames[view_index].view * quad.model;
			}

			vk::DescriptorBufferInfo buffer_info_1{
			        .buffer = resources.uniform_buffer,
			        .offset = frame_ubo_offset,
			        .range = sizeof(frame_gpu_data),
			};
			vk::DescriptorBufferInfo buffer_info_2{
			        .buffer = resources.uniform_buffer,
			        .offset = instance_ubo_offset,
			        .range = sizeof(instance_gpu_data),
			};
			vk::DescriptorBufferInfo buffer_info_3{
			        .buffer = resources.uniform_buffer,
			        .offset = 0,
			        .range = sizeof(glm::mat4) * 32,
			};
			vk::DescriptorBufferInfo buffer_info_4{
			        .buffer = *default_material->buffer,
			        .offset = default_material->offset,
			        .range = sizeof(renderer::material::gpu_data),
			};

			vk::DescriptorImageInfo canvas_image_info{
			        .sampler = clamp_sampler,
			        .imageView = *canvas_view(quad.canvas),
			        .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
			};

			auto f = [&](renderer::texture & texture) {
				return vk::DescriptorImageInfo{
				        .sampler = get_sampler(texture.sampler),
				        .imageView = **(texture.image_view),
				        .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
				};
			};

			std::array write_ds_image{
			        f(*default_material->metallic_roughness_texture),
			        f(*default_material->occlusion_texture),
			        f(*default_material->emissive_texture),
			        f(*default_material->normal_texture),
			};
			std::array descriptors{
			        vk::WriteDescriptorSet{
			                .dstBinding = 0,
			                .descriptorCount = 1,
			                .descriptorType = vk::DescriptorType::eUniformBuffer,
			                .pBufferInfo = &buffer_info_1,
			        },
			        vk::WriteDescriptorSet{
			                .dstBinding = 1,
			                .descriptorCount = 1,
			                .descriptorType = vk::DescriptorType::eUniformBuffer,
			                .pBufferInfo = &buffer_info_2,
			        },
			        vk::WriteDescriptorSet{
			                .dstBinding = 2,
			                .descriptorCount = 1,
			                .descriptorType = vk::DescriptorType::eUniformBuffer,
			                .pBufferInfo = &buffer_info_3,
			        },
			        vk::WriteDescriptorSet{
			                .dstBinding = 3,
			                .descriptorCount = 1,
			                .descriptorType = vk::DescriptorType::eCombinedImageSampler,
			                .pImageInfo = &canvas_image_info,
			        },
			        vk::WriteDescriptorSet{
			                .dstBinding = 4,
			                .descriptorCount = 1,
			                .descriptorType = vk::DescriptorType::eCombinedImageSampler,
			                .pImageInfo = write_ds_image.data(),
			        },
			        vk::WriteDescriptorSet{
			                .dstBinding = 5,
			                .descriptorCount = 1,
			                .descriptorType = vk::DescriptorType::eCombinedImageSampler,
			                .pImageInfo = write_ds_image.data() + 1,
			        },
			        vk::WriteDescriptorSet{
			                .dstBinding = 6,
			                .descriptorCount = 1,
			                .descriptorType = vk::DescriptorType::eCombinedImageSampler,
			                .pImageInfo = write_ds_image.data() + 2,
			        },
			        vk::WriteDescriptorSet{
			                .dstBinding = 7,
			                .descriptorCount = 1,
			                .descriptorType = vk::DescriptorType::eCombinedImageSampler,
			                .pImageInfo = write_ds_image.data() + 3,
			        },
			        vk::WriteDescriptorSet{
			                .dstBinding = 8,
			                .descriptorCount = 1,
			                .descriptorType = vk::DescriptorType::eUniformBuffer,
			                .pBufferInfo = &buffer_info_4,
			        },
			};

			bind_descriptors(descriptors);

			// One triangle strip of 4 vertices per quad
			cb.draw(4, 1, uint32_t(quad_index) * 4, 0);
		}

		cb.endRenderPass();
	}
}

void scene_renderer::debug_draw_clear()
{
	debug_draw_vertices.clear();
}

void scene_renderer::debug_draw_box(const glm::mat4 & model, glm::vec3 min, glm::vec3 max, glm::vec4 color)
{
	std::array<glm::vec4, 8> v{
	        model * glm::vec4{min.x, min.y, min.z, 1}, //        2-------6              ^
	        model * glm::vec4{min.x, min.y, max.z, 1}, //       /|      /|              | +Y
	        model * glm::vec4{min.x, max.y, min.z, 1}, //      3-------7 |              |
	        model * glm::vec4{min.x, max.y, max.z, 1}, //      | |     | |              O--> +X
	        model * glm::vec4{max.x, min.y, min.z, 1}, //      | 0-----|-4             /
	        model * glm::vec4{max.x, min.y, max.z, 1}, //      |/      |/             v
	        model * glm::vec4{max.x, max.y, min.z, 1}, //      1-------5            +Z
	        model * glm::vec4{max.x, max.y, max.z, 1}, //
	};

	debug_draw_vertices.emplace_back(v[0], color);
	debug_draw_vertices.emplace_back(v[1], color);
	debug_draw_vertices.emplace_back(v[1], color);
	debug_draw_vertices.emplace_back(v[3], color);
	debug_draw_vertices.emplace_back(v[3], color);
	debug_draw_vertices.emplace_back(v[2], color);
	debug_draw_vertices.emplace_back(v[2], color);
	debug_draw_vertices.emplace_back(v[0], color);

	debug_draw_vertices.emplace_back(v[4], color);
	debug_draw_vertices.emplace_back(v[5], color);
	debug_draw_vertices.emplace_back(v[5], color);
	debug_draw_vertices.emplace_back(v[7], color);
	debug_draw_vertices.emplace_back(v[7], color);
	debug_draw_vertices.emplace_back(v[6], color);
	debug_draw_vertices.emplace_back(v[6], color);
	debug_draw_vertices.emplace_back(v[4], color);

	debug_draw_vertices.emplace_back(v[0], color);
	debug_draw_vertices.emplace_back(v[4], color);
	debug_draw_vertices.emplace_back(v[1], color);
	debug_draw_vertices.emplace_back(v[5], color);
	debug_draw_vertices.emplace_back(v[2], color);
	debug_draw_vertices.emplace_back(v[6], color);
	debug_draw_vertices.emplace_back(v[3], color);
	debug_draw_vertices.emplace_back(v[7], color);
}
