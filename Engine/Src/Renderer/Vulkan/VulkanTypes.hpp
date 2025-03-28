#pragma once

#include "Containers/Array.hpp"
#include "Containers/ResourcePool.hpp"
#include "Core/Defines.hpp"

#include <cstdint>
#include <vk_mem_alloc.h>
#include <volk.h>
#include <vulkan/vulkan_core.h>

namespace Helix {

#define MAX_SWAPCHAIN_IMAGES 3

using DescriptorSetLayoutHandle = ResourceHandle;
using DescriptorSetHandle = ResourceHandle;
using ImageViewHandle = ResourceHandle;
using ImageHandle = ResourceHandle;

struct QueueFamilyIndices {
  u32 graphics_family_index{UINT32_MAX};
  u32 transfer_family_index{UINT32_MAX};

  bool is_complete() {
    return graphics_family_index != UINT32_MAX &&
           transfer_family_index != UINT32_MAX;
  }
};

struct VulkanBuffer {
  VkBuffer vk_handle{VK_NULL_HANDLE};
  VmaAllocation vma_allocation{VK_NULL_HANDLE};
  void *mapped_data{nullptr};
  cstring name{nullptr};
};

struct VulkanDescriptorSetLayout {
  VkDescriptorSetLayout vk_handle{VK_NULL_HANDLE};
  Array<VkDescriptorSetLayoutBinding> vk_bindings;
  u32 set_index = 0;

  Array<DescriptorSetHandle> allocated_sets{};
  cstring name{nullptr};
};

struct VulkanDescriptorSet {
  VkDescriptorSet vk_handle{VK_NULL_HANDLE};
  DescriptorSetLayoutHandle set_layout;
  cstring name{nullptr};
};

struct VulkanPipeline {
  VkPipeline vk_handle{VK_NULL_HANDLE};
  VkPipelineLayout vk_layout{VK_NULL_HANDLE};
  DescriptorSetLayoutHandle *set_layouts = nullptr;
  u32 set_layout_count = 0;
  VkPipelineBindPoint bind_point;
  cstring name{nullptr};
};

struct VulkanImage {
  VkImage vk_handle{VK_NULL_HANDLE};
  VmaAllocation vma_allocation{VK_NULL_HANDLE};
  VkImageLayout current_layout{VK_IMAGE_LAYOUT_UNDEFINED};
  VkFormat format;
  VkExtent3D vk_extents;
  u32 views_count = 0; // Tracks the number of views that view this image;
  cstring name = nullptr;
};

struct VulkanImageView {
  VkImageView vk_handle{VK_NULL_HANDLE};
  ResourceHandle image;
  cstring name = nullptr;
};

struct VulkanSwapchain {
  VkSurfaceFormatKHR vk_surface_format{};
  VkPresentModeKHR vk_present_mode = VK_PRESENT_MODE_FIFO_KHR;
  VkExtent2D vk_extents{};
  u32 image_count = 0;
  ResourceHandle images[MAX_SWAPCHAIN_IMAGES];
  ResourceHandle image_views[MAX_SWAPCHAIN_IMAGES];
  VkSwapchainKHR vk_handle{VK_NULL_HANDLE};
};

} // namespace Helix
