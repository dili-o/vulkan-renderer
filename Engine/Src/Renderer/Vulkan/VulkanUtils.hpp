#pragma once
#include "Renderer/GPUResources.hpp"
#include "VulkanTypes.hpp"

namespace Helix {
VkBufferUsageFlags to_vk_usage_flags(BufferUsage::Enum _usage);

VkMemoryPropertyFlags to_vk_mem_property_flags(MemoryAccess::Enum _usage);

VmaMemoryUsage to_vma_mem_usage_flags(MemoryAccess::Enum _usage);
} // namespace Helix
