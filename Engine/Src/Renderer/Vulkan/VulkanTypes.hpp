#pragma once

#include "Containers/ResourcePool.hpp"
#include "Core/Defines.hpp"
#include "Renderer/GPUResources.hpp"

#include <cstdint>
#include <vk_mem_alloc.h>
#include <volk.h>

namespace Helix {

#define MAX_SWAPCHAIN_IMAGES 3

// TODO: Add Vk
using DescriptorSetLayoutHandle = ResourceHandle;
using DescriptorSetHandle = ResourceHandle;
using ImageViewHandle = ResourceHandle;
using ImageHandle = ResourceHandle;
using SamplerHandle = ResourceHandle;

struct QueueFamilyIndices {
  u32 graphics_family_index{UINT32_MAX};
  u32 transfer_family_index{UINT32_MAX};
  u32 compute_family_index{UINT32_MAX};

  bool is_complete() {
    return graphics_family_index != UINT32_MAX &&
           transfer_family_index != UINT32_MAX &&
           compute_family_index != UINT32_MAX;
  }
};

struct ResourceQueueObject {
  VkObjectType type;
  ResourceHandle handle;
  cstring name{nullptr};
};

struct SamplerCreation {
  VkFilter min_filter = VK_FILTER_LINEAR;
  VkFilter mag_filter = VK_FILTER_LINEAR;
  VkSamplerMipmapMode mip_filter = VK_SAMPLER_MIPMAP_MODE_LINEAR;

  VkSamplerAddressMode address_mode_u = VK_SAMPLER_ADDRESS_MODE_REPEAT;
  VkSamplerAddressMode address_mode_v = VK_SAMPLER_ADDRESS_MODE_REPEAT;
  VkSamplerAddressMode address_mode_w = VK_SAMPLER_ADDRESS_MODE_REPEAT;

  cstring name = nullptr;
};

struct VulkanBuffer {
  VkBuffer vk_handle{VK_NULL_HANDLE};
  VmaAllocation vma_allocation{VK_NULL_HANDLE};
  void *mapped_data{nullptr};
  cstring name{nullptr};
  MemoryAccess::Enum memory_access{MemoryAccess::None};
  BufferUsage::Enum usage{BufferUsage::None};
  VkDeviceAddress device_address;
};

struct VulkanDescriptorSetLayout {
  VkDescriptorSetLayout vk_handle{VK_NULL_HANDLE};
  cstring name{nullptr};
  i32 reference_count = 0; // Number of Descriptor sets that use this layout
  bool is_bindless = false;
};

struct VulkanDescriptorSet {
  VkDescriptorSet vk_handle{VK_NULL_HANDLE};
  DescriptorSetLayoutHandle set_layout;
  cstring name{nullptr};
  i32 reference_count = 0; // Number of Pipelines that use this set
};

struct VulkanPipeline {
  VkPipeline vk_handle{VK_NULL_HANDLE};
  VkPipelineLayout vk_layout{VK_NULL_HANDLE};
  DescriptorSetHandle *sets = nullptr;
  u32 set_count = 0;
  VkPipelineBindPoint bind_point;
  cstring name{nullptr};
};

struct VulkanImage {
  VkImage vk_handle{VK_NULL_HANDLE};
  VmaAllocation vma_allocation{VK_NULL_HANDLE};
  VkImageLayout current_layout{VK_IMAGE_LAYOUT_UNDEFINED};
  VkFormat vk_format;
  VkExtent3D vk_extents;
  VkImageUsageFlags vk_usage;
  u32 mip_count = 1;
  u32 views_count = 0; // Tracks the number of views that view this image;
  cstring name = nullptr;
};

struct VulkanImageView {
  VkImageView vk_handle{VK_NULL_HANDLE};
  ImageHandle image;
  SamplerHandle sampler;
  u32 base_mip_level = 0;
  u32 base_array_level = 0;
  cstring name = nullptr;
};

struct VulkanSampler {
  VkSampler vk_handle{VK_NULL_HANDLE};
};

struct VulkanSwapchain {
  VkSurfaceFormatKHR vk_surface_format{};
  VkPresentModeKHR vk_present_mode = VK_PRESENT_MODE_FIFO_KHR;
  u32 image_count = 0;
  u32 current_image_index = 0;
  ImageHandle images[MAX_SWAPCHAIN_IMAGES];
  ImageViewHandle image_views[MAX_SWAPCHAIN_IMAGES];
  VkSwapchainKHR vk_handle{VK_NULL_HANDLE};
};

struct VulkanRenderPass {};

struct IndexedDrawCommand {
  u32 indexCount;
  u32 instanceCount;
  u32 firstIndex;
  i32 vertexOffset;
  u32 firstInstance;
};

} // namespace Helix
