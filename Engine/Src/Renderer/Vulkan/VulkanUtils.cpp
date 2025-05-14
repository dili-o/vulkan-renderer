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
  case TextureFormat::R8G8B8A8_SRGB:
    return VK_FORMAT_R8G8B8A8_SRGB;
  case TextureFormat::R8G8B8A8_UNORM:
    return VK_FORMAT_R8G8B8A8_UNORM;
  }
}

VkImageUsageFlags to_vk_image_usage_flags(TextureUsage::Enum _usage) {
  VkImageUsageFlags usage = 0;
  if (_usage & TextureUsage::Compute)
    usage |= VK_IMAGE_USAGE_STORAGE_BIT;

  if (_usage & TextureUsage::RenderTarget)
    usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

  if (_usage & TextureUsage::TransferSrc)
    usage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;

  if (_usage & TextureUsage::TransferDest)
    usage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;

  if (_usage & TextureUsage::Depth)
    usage |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;

  if (_usage & TextureUsage::Sampled)
    usage |= VK_IMAGE_USAGE_SAMPLED_BIT;

  return usage;
}

VkAccessFlags2 to_vk_src_access_flags(VkImageLayout layout) {
  switch (layout) {
  case VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL:
    return VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
  case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
    return VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
  case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
    return VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
  case VK_IMAGE_LAYOUT_UNDEFINED:
    return VK_ACCESS_2_NONE;
  case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
    return VK_ACCESS_2_TRANSFER_WRITE_BIT;
  case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
    return VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
  case VK_IMAGE_LAYOUT_PRESENT_SRC_KHR:
    return VK_ACCESS_2_NONE;
  case VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_STENCIL_ATTACHMENT_OPTIMAL:
    return VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
  case VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL:
    return VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
  default:
    HERROR("Unknown layout");
    return VK_ACCESS_2_NONE;
  }
}

VkCullModeFlags to_vk_cull_mode_flags(CullMode::Enum cull_mode) {
  switch (cull_mode) {
  case CullMode::None:
    return VK_CULL_MODE_NONE;
  case CullMode::Front:
    return VK_CULL_MODE_FRONT_BIT;
  case CullMode::Back:
    return VK_CULL_MODE_BACK_BIT;
  case CullMode::FrontAndBack:
    return VK_CULL_MODE_FRONT_AND_BACK;
  }
}

VkDescriptorType to_vk_descriptor_type(BindingType::Enum binding_type) {
  switch (binding_type) {
  case BindingType::UniformBuffer:
    return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
  case BindingType::CombinedSampler:
    return VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  }
}

cstring to_compiler_stage(ShaderStage::Enum stage) {
  switch (stage) {
  case ShaderStage::Vertex:
    return "vert";
  case ShaderStage::Fragment:
    return "frag";
  case ShaderStage::Compute:
    return "comp";
  default:
    HERROR("Unknown shader stage!");
    return nullptr;
  }
}

VkShaderStageFlags to_vk_shader_stage(ShaderStage::Enum stage_) {
  VkShaderStageFlags stage{};
  if (stage_ == ShaderStage::AllStage)
    return VK_SHADER_STAGE_ALL;
  if (stage_ & ShaderStage::Vertex)
    stage |= VK_SHADER_STAGE_VERTEX_BIT;
  if (stage_ & ShaderStage::Fragment)
    stage |= VK_SHADER_STAGE_FRAGMENT_BIT;
  if (stage_ & ShaderStage::Compute)
    stage |= VK_SHADER_STAGE_COMPUTE_BIT;

  return stage;
}

VkAccessFlags2 to_vk_dst_access_flags(VkImageLayout layout) {
  switch (layout) {
  case VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL:
    return VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
  case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
    return VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
  case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
    return VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
  case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
    return VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
  case VK_IMAGE_LAYOUT_UNDEFINED:
    return VK_ACCESS_2_NONE;
  case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
    return VK_ACCESS_2_TRANSFER_WRITE_BIT;
  case VK_IMAGE_LAYOUT_PRESENT_SRC_KHR:
    return VK_ACCESS_2_NONE;
  case VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_STENCIL_ATTACHMENT_OPTIMAL:
    return VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
  case VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL:
    return VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
  default:
    HERROR("Unknown layout");
    return VK_ACCESS_2_NONE;
  }
}

VkAttachmentLoadOp to_vk_load_op(LoadOp::Enum load_op) {
  switch (load_op) {
  case LoadOp::Clear:
    return VK_ATTACHMENT_LOAD_OP_CLEAR;
  case LoadOp::Load:
    return VK_ATTACHMENT_LOAD_OP_LOAD;
  case LoadOp::DontCare:
    return VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  }
}

VkAttachmentStoreOp to_vk_store_op(StoreOp::Enum store_op) {
  switch (store_op) {
  case StoreOp::DontCare:
    return VK_ATTACHMENT_STORE_OP_DONT_CARE;
  case StoreOp::Store:
    return VK_ATTACHMENT_STORE_OP_STORE;
  }
}

VkImageMemoryBarrier2
create_image_barrier(VulkanImage *image, VkImageLayout old_layout,
                     VkImageLayout new_layout, VkPipelineStageFlags2 src_stage,
                     VkPipelineStageFlags2 dst_stage,
                     u32 src_queue_family_index, u32 dst_queue_family_index) {

  VkImageMemoryBarrier2 image_barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
  image_barrier.srcStageMask = src_stage;
  image_barrier.srcAccessMask = to_vk_src_access_flags(old_layout);
  image_barrier.dstStageMask = dst_stage;
  image_barrier.dstAccessMask = to_vk_dst_access_flags(new_layout);
  image_barrier.oldLayout = old_layout;
  image_barrier.newLayout = new_layout;
  image_barrier.srcQueueFamilyIndex = src_queue_family_index;
  image_barrier.dstQueueFamilyIndex = dst_queue_family_index;
  image_barrier.image = image->vk_handle;
  image_barrier.subresourceRange.aspectMask =
      has_depth_or_stencil(image->format) ? VK_IMAGE_ASPECT_DEPTH_BIT
                                          : VK_IMAGE_ASPECT_COLOR_BIT;
  image_barrier.subresourceRange.baseMipLevel = 0;
  image_barrier.subresourceRange.levelCount = 1;
  image_barrier.subresourceRange.baseArrayLayer = 0;
  image_barrier.subresourceRange.layerCount = 1;
  return image_barrier;
}

} // namespace Helix
