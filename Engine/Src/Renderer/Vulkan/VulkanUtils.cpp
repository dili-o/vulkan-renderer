#include "VulkanUtils.hpp"
#include "Core/Assert.hpp"
#include "Renderer/GPUResourceTypes.hpp"
#include "Renderer/GPUResources.hpp"
#include "vk_mem_alloc.h"

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
  if (_usage & BufferUsage::TransferDst)
    usage |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;
  if (_usage & BufferUsage::IndexedIndirect)
    usage |= VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;
  if (_usage & BufferUsage::ShaderAddress)
    usage |= VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
  if (_usage & BufferUsage::Storage)
    usage |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;

  return usage;
}

VkMemoryPropertyFlags to_vk_mem_property_flags(MemoryAccess::Enum _usage) {
  VkMemoryPropertyFlags usage{};
  if (_usage & MemoryAccess::GPU_ONLY)
    usage |= VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
  if (_usage & MemoryAccess::CPU_TO_GPU)
    usage |= VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
             VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
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
  case TextureFormat::R32_UINT:
    return VK_FORMAT_R32_UINT;
  case TextureFormat::R32_SINT:
    return VK_FORMAT_R32_SINT;
  case TextureFormat::R32_SFLOAT:
    return VK_FORMAT_R32_SFLOAT;
  }
}

VkImageUsageFlags to_vk_image_usage_flags(TextureUsage::Enum _usage) {
  VkImageUsageFlags usage = 0;

  HASSERT_MSG(_usage != TextureUsage::Undefined,
              "to_vk_image_usage_flags(): _usage is TextureUsage::Undefined!");

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

VkPrimitiveTopology
to_vk_primitive_topology(PrimitiveType::Enum primitive_type) {
  switch (primitive_type) {
  case PrimitiveType::Triangle:
    return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  case PrimitiveType::Line:
    return VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
  }
}

VkCompareOp to_vk_compare_op(CompareOp::Enum compare_op) {
  switch (compare_op) {
  case CompareOp::Never:
    return VK_COMPARE_OP_NEVER;
  case CompareOp::Less:
    return VK_COMPARE_OP_LESS;
  case CompareOp::Equal:
    return VK_COMPARE_OP_EQUAL;
  case CompareOp::LessOrEqual:
    return VK_COMPARE_OP_LESS_OR_EQUAL;
  case CompareOp::Greater:
    return VK_COMPARE_OP_GREATER;
  case CompareOp::NotEqual:
    return VK_COMPARE_OP_NOT_EQUAL;
  case CompareOp::GreaterOrEqual:
    return VK_COMPARE_OP_GREATER_OR_EQUAL;
  case CompareOp::Always:
    return VK_COMPARE_OP_ALWAYS;
  }
}

VkDescriptorType to_vk_descriptor_type(BindingType::Enum binding_type) {
  switch (binding_type) {
  case BindingType::UniformBuffer:
    return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
  case BindingType::CombinedSampler:
    return VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  case BindingType::StorageBuffer:
    return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
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

  HASSERT(stage);
  return stage;
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
  case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
    return VK_ACCESS_2_TRANSFER_READ_BIT;
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
  case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
    return VK_ACCESS_2_TRANSFER_READ_BIT;
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

VkFilter to_vk_filter(SamplerFilter::Enum filter) {
  switch (filter) {
  case SamplerFilter::Linear:
    return VK_FILTER_LINEAR;
  case SamplerFilter::Nearest:
    return VK_FILTER_NEAREST;
  default:
    HASSERT_MSG(false, "Unkown SamplerFilter::Enum type!");
  }
}

VkSamplerMipmapMode to_vk_sampler_mipmap_mode(SamplerFilter::Enum filter) {
  switch (filter) {
  case SamplerFilter::Linear:
    return VK_SAMPLER_MIPMAP_MODE_LINEAR;
  case SamplerFilter::Nearest:
    return VK_SAMPLER_MIPMAP_MODE_NEAREST;
  default:
    HASSERT_MSG(false, "Unkown SamplerFilter::Enum type!");
  }
}

VkSamplerAddressMode
to_vk_sampler_address_mode(SamplerAddressMode::Enum address_mode) {
  switch (address_mode) {
  case SamplerAddressMode::Repeat:
    return VK_SAMPLER_ADDRESS_MODE_REPEAT;
  case SamplerAddressMode::MirroredRepeat:
    return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
  case SamplerAddressMode::ClampToEdge:
    return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  case SamplerAddressMode::ClampToBorder:
    return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
  case SamplerAddressMode::MirrorClampToEdge:
    return VK_SAMPLER_ADDRESS_MODE_MIRROR_CLAMP_TO_EDGE;
  default:
    HASSERT_MSG(false, "Unkown SamplerAddressMode::Enum type!");
  }
}

VkBorderColor to_vk_border_color(BorderColor::Enum border_color) {
  switch (border_color) {
  case BorderColor::IntOpaqueBlack:
    return VK_BORDER_COLOR_INT_OPAQUE_BLACK;
  case BorderColor::IntOpaqueWhite:
    return VK_BORDER_COLOR_INT_OPAQUE_WHITE;
  case BorderColor::FloatOpaqueBlack:
    return VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
  case BorderColor::FloatOpaqueWhite:
    return VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
  default:
    HASSERT_MSG(false, "Unkown BorderColor::Enum type!");
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
      has_depth_or_stencil(image->vk_format) ? VK_IMAGE_ASPECT_DEPTH_BIT
                                             : VK_IMAGE_ASPECT_COLOR_BIT;
  image_barrier.subresourceRange.baseMipLevel = 0;
  image_barrier.subresourceRange.levelCount = 1;
  image_barrier.subresourceRange.baseArrayLayer = 0;
  image_barrier.subresourceRange.layerCount = 1;
  return image_barrier;
}

} // namespace Helix
