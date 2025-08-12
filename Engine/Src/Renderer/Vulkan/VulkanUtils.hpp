#pragma once
#include "Renderer/GPUResources.hpp"
#include "VulkanTypes.hpp"
#include <vulkan/vulkan_core.h>

namespace Helix {
#define VK_CHECK(call)                                                         \
  do {                                                                         \
    VkResult result_ = call;                                                   \
    HASSERT_MSGS(result_ == VK_SUCCESS, "Error code: {}", (u32)result_);       \
  } while (0)

VkBufferUsageFlags to_vk_buffer_usage_flags(BufferUsage::Enum _usage);

VkMemoryPropertyFlags to_vk_mem_property_flags(MemoryAccess::Enum _usage);

VmaMemoryUsage to_vma_mem_usage_flags(MemoryAccess::Enum _usage);

VkPipelineBindPoint to_vk_bind_point(PipelineType::Enum type);

VkFormat to_vk_format(TextureFormat::Enum format);

VkImageUsageFlags to_vk_image_usage_flags(TextureUsage::Enum usage);

VkAccessFlags2 to_vk_src_access_flags(VkImageLayout layout);

VkAccessFlags2 to_vk_dst_access_flags(VkImageLayout layout);

VkCullModeFlags to_vk_cull_mode_flags(CullMode::Enum cull_mode);

VkPrimitiveTopology
to_vk_primitive_topology(PrimitiveType::Enum primitive_type);

VkCompareOp to_vk_compare_op(CompareOp::Enum compare_op);

VkDescriptorType to_vk_descriptor_type(BindingType::Enum binding_type);

cstring to_compiler_stage(ShaderStage::Enum stage);

VkShaderStageFlags to_vk_shader_stage(ShaderStage::Enum stage);

VkAttachmentLoadOp to_vk_load_op(LoadOp::Enum load_op);

VkAttachmentStoreOp to_vk_store_op(StoreOp::Enum store_op);

VkImageMemoryBarrier2
create_image_barrier(VulkanImage *image, VkImageLayout old_layout,
                     VkImageLayout new_layout, VkPipelineStageFlags2 src_stage,
                     VkPipelineStageFlags2 dst_stage,
                     u32 src_queue_family_index = VK_QUEUE_FAMILY_IGNORED,
                     u32 dst_queue_family_index = VK_QUEUE_FAMILY_IGNORED);

inline bool has_depth_or_stencil(VkFormat value) {
  return value >= VK_FORMAT_D16_UNORM && value <= VK_FORMAT_D32_SFLOAT_S8_UINT;
}
inline bool has_depth_or_stencil(TextureFormat::Enum format) {
  return format == TextureFormat::D32;
}

} // namespace Helix
