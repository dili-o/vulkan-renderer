#pragma once

#include "Core/Defines.hpp"

#include <cstdint>
#include <volk.h>
#include <vulkan/vulkan_core.h>

namespace Helix {

#define MAX_SWAPCHAIN_IMAGES 3

struct QueueFamilyIndices {
  u32 graphics_family_index{UINT32_MAX};

  bool is_complete() { return graphics_family_index != UINT32_MAX; }
};

struct VulkanSwapchain {
  VkSurfaceFormatKHR vk_surface_format{};
  VkPresentModeKHR vk_present_mode = VK_PRESENT_MODE_FIFO_KHR;
  VkExtent2D vk_extents{};
  u32 image_count = 0;
  VkImage vk_images[MAX_SWAPCHAIN_IMAGES];
  VkImageView vk_image_views[MAX_SWAPCHAIN_IMAGES];
  VkSwapchainKHR vk_handle{VK_NULL_HANDLE};
};

} // namespace Helix
