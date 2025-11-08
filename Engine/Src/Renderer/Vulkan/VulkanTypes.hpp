#pragma once

#include "Containers/ResourcePool.hpp"
#include "Core/Defines.hpp"
#include "Renderer/GPUResources.hpp"

#include <cstdint>
#include <vk_mem_alloc.h>
#include <volk.h>

namespace hlx {

#define MAX_SWAPCHAIN_IMAGES 3

struct VkImageTag{};

using VkDescriptorSetLayoutHandle = BindingSetLayoutHandle;
using VkDescriptorSetHandle = BindingSetHandle;
using VkImageViewHandle = TextureHandle;
using VkImageHandle = ResourceHandle<VkImageTag>;

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
  u32 index;
  u32 generation;
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
  bool is_bindless = false;
  cstring name{nullptr};
};

struct VulkanDescriptorSet {
  VkDescriptorSet vk_handle{VK_NULL_HANDLE};
  VkDescriptorSetLayoutHandle set_layout;
  cstring name{nullptr};
};

struct VulkanPipeline {
  VkPipeline vk_handle{VK_NULL_HANDLE};
  VkPipelineLayout vk_layout{VK_NULL_HANDLE};
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
  u32 array_count = 1;
  cstring name = nullptr;
};

struct VulkanImageView {
  VkImageView vk_handle{VK_NULL_HANDLE};
  VkImageHandle image;
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
  VkImageHandle images[MAX_SWAPCHAIN_IMAGES];
  VkImageViewHandle image_views[MAX_SWAPCHAIN_IMAGES];
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

} // namespace hlx
