#include "VulkanUtils.hpp"
#include "Renderer/GPUResourceTypes.hpp"
#include "Renderer/GPUResources.hpp"
#include "vk_mem_alloc.h"
#include <vulkan/vulkan_core.h>

namespace Helix {

VkBufferUsageFlags to_vk_buffer_usage_flags(BufferUsage::Enum _usage) {
  VkBufferUsageFlags usage{};
  if (_usage & BufferUsage::Vertex)
    usage |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
  if (_usage & BufferUsage::Index)
    usage |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
  if (_usage & BufferUsage::Uniform)
    usage |= VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
  if (_usage & BufferUsage::TransferSrc)
    usage |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
  if (_usage & BufferUsage::TransferDest)
    usage |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;
  return usage;
}

VkMemoryPropertyFlags to_vk_mem_property_flags(MemoryAccess::Enum _usage) {
  VkMemoryPropertyFlags usage{};

  if (_usage & MemoryAccess::GPU_ONLY)
    usage |= VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
  if (_usage & MemoryAccess::CPU_TO_GPU)
    usage |= VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
             VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
  if (_usage & MemoryAccess::GPU_TO_CPU)
    usage |= VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
             VK_MEMORY_PROPERTY_HOST_COHERENT_BIT |
             VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
  if (_usage & MemoryAccess::CPU_ONLY)
    usage |= VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
             VK_MEMORY_PROPERTY_HOST_CACHED_BIT;

  return usage;
}

VmaMemoryUsage to_vma_mem_usage_flags(MemoryAccess::Enum _usage) {

  switch (_usage) {
  case MemoryAccess::None:
    return VMA_MEMORY_USAGE_UNKNOWN;
  case MemoryAccess::GPU_ONLY:
    return VMA_MEMORY_USAGE_GPU_ONLY;
  case MemoryAccess::CPU_TO_GPU:
    return VMA_MEMORY_USAGE_CPU_TO_GPU;
  case MemoryAccess::GPU_TO_CPU:
    return VMA_MEMORY_USAGE_GPU_TO_CPU;
  case MemoryAccess::CPU_ONLY:
    return VMA_MEMORY_USAGE_CPU_ONLY;
  }
}

VkPipelineBindPoint to_vk_bind_point(PipelineType::Enum type) {
  switch (type) {
  case PipelineType::Graphics:
    return VK_PIPELINE_BIND_POINT_GRAPHICS;
  case PipelineType::Compute:
    return VK_PIPELINE_BIND_POINT_COMPUTE;
  }
}

VkFormat to_vk_format(TextureFormat::Enum format) {
  switch (format) {
  case TextureFormat::Undefined:
    return VK_FORMAT_UNDEFINED;
  case TextureFormat::D32:
    return VK_FORMAT_D32_SFLOAT;
  case TextureFormat::B8G8R8A8_UNORM:
    return VK_FORMAT_B8G8R8A8_UNORM;
  }
}

VkImageUsageFlags to_vk_image_usage_flags(TextureUsage::Enum _usage) {
  VkImageUsageFlags usage = 0;
  switch (_usage) {
  case TextureUsage::Compute:
    usage |= VK_IMAGE_USAGE_STORAGE_BIT;
  case TextureUsage::RenderTarget:
    usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
  case TextureUsage::TransferSrc:
    usage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
  case TextureUsage::TransferDest:
    usage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
  }
  return usage;
}

} // namespace Helix
