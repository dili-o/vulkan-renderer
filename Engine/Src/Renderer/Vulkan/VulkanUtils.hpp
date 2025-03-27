#pragma once
#include "Renderer/GPUResources.hpp"
#include "VulkanTypes.hpp"
// #include <vulkan/vulkan_core.h>

namespace Helix {
#define VK_CHECK(call)                                                         \
  do {                                                                         \
    VkResult result_ = call;                                                   \
    HASSERT_MSGS(result_ == VK_SUCCESS, "Error code: {}", (u32)result_);       \
  } while (0)

VkBufferUsageFlags to_vk_usage_flags(BufferUsage::Enum _usage);

VkMemoryPropertyFlags to_vk_mem_property_flags(MemoryAccess::Enum _usage);

VmaMemoryUsage to_vma_mem_usage_flags(MemoryAccess::Enum _usage);

VkPipelineBindPoint to_vk_bind_point(PipelineType::Enum type);
} // namespace Helix
