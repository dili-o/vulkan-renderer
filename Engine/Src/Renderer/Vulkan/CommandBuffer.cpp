#include "CommandBuffer.hpp"
#include "Containers/ResourcePool.hpp"
#include "Renderer/GPUResourceTypes.hpp"
#include "Renderer/Vulkan/VkGpuDevice.hpp"
#include "Renderer/Vulkan/VulkanTypes.hpp"
#include "VulkanUtils.hpp"

namespace Helix {

#pragma region VulkanCommandBuffer

void VulkanCommandBuffer::init(VkCommandPool vk_command_pool,
                               VkCommandBufferLevel vk_level,
                               VkGpuDevice *device_, cstring name) {
  VkCommandBufferAllocateInfo alloc_info{};
  alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  alloc_info.commandPool = vk_command_pool;
  alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  alloc_info.commandBufferCount = 1;

  VK_CHECK(
      vkAllocateCommandBuffers(device_->vk_device, &alloc_info, &vk_handle));
  state = CommandBufferState::Initial;
  vk_pool = vk_command_pool;

  device = device_;
}

void VulkanCommandBuffer::reset() {
  VK_CHECK(vkResetCommandBuffer(vk_handle, 0));
  state = CommandBufferState::Initial;
}

void VulkanCommandBuffer::free() {
  vkFreeCommandBuffers(device->vk_device, vk_pool, 1, &vk_handle);
  state = CommandBufferState::Invalid;
}

void VulkanCommandBuffer::begin(VkCommandBufferUsageFlags flags) {
  VkCommandBufferBeginInfo begin_info{};
  begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  begin_info.flags = flags;

  if (state == CommandBufferState::Recording) {
    HWARN("Command buffer is already in CommandBufferState::Recording");
    return;
  }
  VK_CHECK(vkBeginCommandBuffer(vk_handle, &begin_info));
  state = CommandBufferState::Recording;
}

void VulkanCommandBuffer::end() {
  VK_CHECK(vkEndCommandBuffer(vk_handle));
  state = CommandBufferState::Executable;
}

void VulkanCommandBuffer::transition_image(TextureHandle image_handle,
                                           VkImageLayout old_layout,
                                           VkImageLayout new_layout,
                                           VkPipelineStageFlags2 src_stage,
                                           VkPipelineStageFlags2 dst_stage,
                                           u32 src_queue_family_index,
                                           u32 dst_queue_family_index) {
  // TODO: Better way to select src and dst stages
  VulkanImage *image = device->images.obtain(image_handle);
  transition_image(image, old_layout, new_layout, src_stage, dst_stage,
                   src_queue_family_index, dst_queue_family_index);
}

void VulkanCommandBuffer::transition_image(
    VulkanImage *image, VkImageLayout old_layout, VkImageLayout new_layout,
    VkPipelineStageFlags2 src_stage, VkPipelineStageFlags2 dst_stage,
    u32 src_queue_family_index, u32 dst_queue_family_index) {

  VkImageMemoryBarrier2 image_barrier =
      create_image_barrier(image, old_layout, new_layout, src_stage, dst_stage,
                           src_queue_family_index, dst_queue_family_index);
  image_barrier.subresourceRange.levelCount = image->mip_count;

  pipeline_barrier(&image_barrier, 1);

  image->current_layout = image_barrier.newLayout;
}

void VulkanCommandBuffer::pipeline_barrier(
    VkImageMemoryBarrier2 *image_memory_barriers,
    u32 image_memory_barrier_count) {

  VkDependencyInfo dependency_info{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
  dependency_info.dependencyFlags = 0;
  dependency_info.imageMemoryBarrierCount = image_memory_barrier_count;
  dependency_info.pImageMemoryBarriers = image_memory_barriers;

  vkCmdPipelineBarrier2(vk_handle, &dependency_info);
}

void VulkanCommandBuffer::bind_renderpass(RenderPassHandle render_pass_handle) {
  RenderPass *render_pass = device->access_render_pass(render_pass_handle);

  VkRenderingAttachmentInfo color_attachment_infos[MAX_COLOR_ATTACHMENTS];

  for (u32 i = 0; i < render_pass->num_colour_attachments; ++i) {
    VkRenderingAttachmentInfo &color_attachment_info =
        color_attachment_infos[i];
    color_attachment_info.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    VulkanImageView *view = device->access_image_view(
        render_pass->colour_attachments[i].texture_handle);
    color_attachment_info.imageView = view->vk_handle;
    color_attachment_info.imageLayout =
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    color_attachment_info.loadOp =
        to_vk_load_op(render_pass->colour_attachments[i].load_op);

    color_attachment_info.storeOp =
        to_vk_store_op(render_pass->colour_attachments[i].store_op);

    color_attachment_info.clearValue = {{{0.f, 0.f, 0.1f, 1.0f}}};
    color_attachment_info.clearValue.color.uint32[0] = UINT32_MAX;
    color_attachment_info.resolveMode = VK_RESOLVE_MODE_NONE;
    color_attachment_info.pNext = nullptr;
    color_attachment_info.resolveImageLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    color_attachment_info.resolveImageView = VK_NULL_HANDLE;
  }

  VkRenderingAttachmentInfo depth_attachment_info{
      VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
  bool has_depth =
      is_handle_valid(render_pass->depth_attachment.texture_handle);

  if (has_depth) {
    VulkanImageView *depth_image_view =
        device->access_image_view(render_pass->depth_attachment.texture_handle);
    VulkanImage *depth_image = device->access_image(depth_image_view->image);
    depth_attachment_info.imageView = depth_image_view->vk_handle;
    depth_attachment_info.imageLayout = depth_image->current_layout;
    depth_attachment_info.loadOp =
        to_vk_load_op(render_pass->depth_attachment.load_op);
    depth_attachment_info.storeOp =
        to_vk_store_op(render_pass->depth_attachment.store_op);
    depth_attachment_info.clearValue.color = {{0.0f, 0.0f, 0.0f, 1.0f}};
    depth_attachment_info.clearValue.depthStencil = {1.f, 0};
    depth_attachment_info.resolveMode = VK_RESOLVE_MODE_NONE;
  }

  VulkanImage *swapchain_image =
      device->access_image(device->swapchain.images[0]);
  VkRenderingInfo render_info{VK_STRUCTURE_TYPE_RENDERING_INFO};
  render_info.layerCount = 1;
  render_info.renderArea = {
      {0, 0},
      {swapchain_image->vk_extents.width, swapchain_image->vk_extents.height}};
  render_info.viewMask = 0;
  render_info.colorAttachmentCount = render_pass->num_colour_attachments;
  render_info.pColorAttachments = color_attachment_infos;
  render_info.pDepthAttachment = has_depth ? &depth_attachment_info : nullptr;
  render_info.pStencilAttachment = nullptr;

  vkCmdBeginRendering(vk_handle, &render_info);
}

void VulkanCommandBuffer::end_current_renderpass() {
  vkCmdEndRendering(vk_handle);
}

void VulkanCommandBuffer::bind_pipeline(PipelineHandle handle) {
  VulkanPipeline *pipeline = device->access_pipeline(handle);
  if (pipeline) {
    vkCmdBindPipeline(vk_handle, pipeline->bind_point, pipeline->vk_handle);
    current_pipeline = handle;
  }
#ifdef _DEBUG
  else {
    HERROR("VulkanCommandBuffer::bind_pipeline - Invalid pipeline handle!");
  }
#endif
}

void VulkanCommandBuffer::bind_viewport(VkViewport *viewport) {
  vkCmdSetViewport(vk_handle, 0, 1, viewport);
}

void VulkanCommandBuffer::bind_scissors(VkRect2D rect) {
  vkCmdSetScissor(vk_handle, 0, 1, &rect);
}

void VulkanCommandBuffer::bind_vertex_buffer(BufferHandle handle,
                                             u32 first_binding,
                                             u32 binding_count) {

  VulkanBuffer *buffer = device->access_buffer(handle);
  VkBuffer vertex_buffers[] = {buffer->vk_handle};
  VkDeviceSize offsets[] = {0};

  vkCmdBindVertexBuffers(vk_handle, first_binding, binding_count,
                         vertex_buffers, offsets);
}

void VulkanCommandBuffer::bind_index_buffer(BufferHandle handle, u32 offset,
                                            VkIndexType index_type) {
  VulkanBuffer *buffer = device->access_buffer(handle);
  vkCmdBindIndexBuffer(vk_handle, buffer->vk_handle, offset, index_type);
}

void VulkanCommandBuffer::bind_descriptor_sets(VkDescriptorSet dset,
                                               u32 set_index) {

  VulkanPipeline *pipeline = device->access_pipeline(current_pipeline);
  vkCmdBindDescriptorSets(vk_handle, pipeline->bind_point, pipeline->vk_layout,
                          set_index, 1, &dset, 0, nullptr);
}

void VulkanCommandBuffer::push_constants(u32 offset, u32 size, void *data) {
  VulkanPipeline *pipeline = device->access_pipeline(current_pipeline);
  if (pipeline) {
    // TODO: Specify the shader stage
    vkCmdPushConstants(vk_handle, pipeline->vk_layout, VK_SHADER_STAGE_ALL,
                       offset, size, data);
  }
#ifdef _DEBUG
  else {
    HERROR("VulkanCommandBuffer::push_constants - Invalid pipeline handle!");
  }
#endif
}

void VulkanCommandBuffer::draw_indexed(u32 index_count, u32 instance_count,
                                       u32 first_index, i32 vertex_offset,
                                       u32 first_instance) {

  vkCmdDrawIndexed(vk_handle, index_count, instance_count, first_index,
                   vertex_offset, first_instance);
}

void VulkanCommandBuffer::draw(u32 vertex_count, u32 instance_count,
                               u32 first_vertex, u32 first_instance) {
  vkCmdDraw(vk_handle, vertex_count, instance_count, first_vertex,
            first_instance);
}

void VulkanCommandBuffer::draw_indexed_indirect_count(
    VkBuffer indirect_buffer, VkDeviceSize indirect_buffer_offset,
    VkBuffer count_buffer, VkDeviceSize count_buffer_offset, u32 max_draw_count,
    u32 indirect_buffer_stride) {
  vkCmdDrawIndexedIndirectCount(
      vk_handle, indirect_buffer, indirect_buffer_offset, count_buffer,
      count_buffer_offset, max_draw_count, indirect_buffer_stride);
}

void VulkanCommandBuffer::dispatch(u32 group_x, u32 group_y, u32 group_z) {
  vkCmdDispatch(vk_handle, group_x, group_y, group_z);
}

void VulkanCommandBuffer::copy_buffer_to_buffer(VkBuffer dst_buffer,
                                                u32 dst_offset,
                                                VkBuffer src_buffer,
                                                u32 src_offset, u32 size,
                                                VkQueue vk_queue) {
  VkBufferCopy2 region{VK_STRUCTURE_TYPE_BUFFER_COPY_2};
  region.srcOffset = src_offset;
  region.dstOffset = dst_offset;
  region.size = size;

  VkCopyBufferInfo2 buffer_info{VK_STRUCTURE_TYPE_COPY_BUFFER_INFO_2};
  buffer_info.srcBuffer = src_buffer;
  buffer_info.dstBuffer = dst_buffer;
  buffer_info.regionCount = 1;
  buffer_info.pRegions = &region;

  vkCmdCopyBuffer2(vk_handle, &buffer_info);
}

void VulkanCommandBuffer::copy_buffer_to_image(VkImageHandle dst_image,
                                               VkBuffer src_buffer, u32 size,
                                               VkQueue vk_queue,
                                               bool generate_mips) {
  // Tranisiton image (including mip levels) to transfer dst
  VulkanImage *image = device->access_image(dst_image);
  transition_image(
      image, image->current_layout, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
      VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT, VK_PIPELINE_STAGE_2_COPY_BIT);

  VkBufferImageCopy2 region{VK_STRUCTURE_TYPE_BUFFER_IMAGE_COPY_2};
  region.bufferOffset = 0;
  region.imageSubresource.aspectMask = has_depth_or_stencil(image->vk_format)
                                           ? VK_IMAGE_ASPECT_DEPTH_BIT
                                           : VK_IMAGE_ASPECT_COLOR_BIT;
  region.imageSubresource.mipLevel = 0;
  region.imageSubresource.baseArrayLayer = 0;
  region.imageSubresource.layerCount = 1;
  region.imageOffset = {0, 0, 0};
  region.imageExtent = image->vk_extents;

  VkCopyBufferToImageInfo2 buffer_image_info{
      VK_STRUCTURE_TYPE_COPY_BUFFER_TO_IMAGE_INFO_2};
  buffer_image_info.srcBuffer = src_buffer;
  buffer_image_info.dstImage = image->vk_handle;
  buffer_image_info.dstImageLayout = image->current_layout;
  buffer_image_info.regionCount = 1;
  buffer_image_info.pRegions = &region;

  vkCmdCopyBufferToImage2(vk_handle, &buffer_image_info);

  // Blit stage
  transition_image(image, image->current_layout,
                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                   VK_PIPELINE_STAGE_2_COPY_BIT, VK_PIPELINE_STAGE_2_BLIT_BIT);

  if (generate_mips) {
    VkImageMemoryBarrier2 image_barrier{
        VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
    image_barrier.image = image->vk_handle;
    image_barrier.subresourceRange.aspectMask =
        has_depth_or_stencil(image->vk_format) ? VK_IMAGE_ASPECT_DEPTH_BIT
                                               : VK_IMAGE_ASPECT_COLOR_BIT;
    image_barrier.subresourceRange.levelCount = 1;
    image_barrier.subresourceRange.baseArrayLayer = 0;
    image_barrier.subresourceRange.layerCount = 1;

    i32 image_width = image->vk_extents.width;
    i32 image_height = image->vk_extents.height;
    VkImageAspectFlags aspect_mask = has_depth_or_stencil(image->vk_format)
                                         ? VK_IMAGE_ASPECT_DEPTH_BIT
                                         : VK_IMAGE_ASPECT_COLOR_BIT;
    image_barrier.srcStageMask = VK_PIPELINE_STAGE_2_BLIT_BIT;

    for (u32 i = 1; i < image->mip_count; ++i) {
      // Transition blit src to transfer src layout
      image_barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
      image_barrier.dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
      image_barrier.dstStageMask = VK_PIPELINE_STAGE_2_BLIT_BIT;
      image_barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
      image_barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
      image_barrier.subresourceRange.baseMipLevel = i - 1;
      pipeline_barrier(&image_barrier, 1);

      VkImageBlit2 blit_region{VK_STRUCTURE_TYPE_IMAGE_BLIT_2};
      blit_region.srcSubresource.aspectMask = aspect_mask;
      blit_region.srcSubresource.mipLevel = i - 1;
      blit_region.srcSubresource.baseArrayLayer = 0;
      blit_region.srcSubresource.layerCount = 1;
      blit_region.srcOffsets[0] = {0, 0, 0};
      blit_region.srcOffsets[1] = {image_width, image_height, 1};

      blit_region.dstSubresource.aspectMask = aspect_mask;
      blit_region.dstSubresource.mipLevel = i;
      blit_region.dstSubresource.baseArrayLayer = 0;
      blit_region.dstSubresource.layerCount = 1;
      blit_region.dstOffsets[0] = {0, 0, 0};
      blit_region.dstOffsets[1] = {image_width > 1 ? image_width / 2 : 1,
                                   image_height > 1 ? image_height / 2 : 1, 1};

      VkBlitImageInfo2 blit2{VK_STRUCTURE_TYPE_BLIT_IMAGE_INFO_2};
      blit2.srcImage = image->vk_handle;
      blit2.srcImageLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
      blit2.dstImage = image->vk_handle;
      blit2.dstImageLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
      blit2.regionCount = 1;
      blit2.pRegions = &blit_region;
      blit2.filter = VK_FILTER_LINEAR;

      vkCmdBlitImage2(vk_handle, &blit2);

      // Transition blit src to shader optimal
      image_barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
      image_barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
      image_barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
      image_barrier.srcStageMask = VK_PIPELINE_STAGE_2_BLIT_BIT;
      image_barrier.dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
      image_barrier.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;

      pipeline_barrier(&image_barrier, 1);
      image_barrier.srcStageMask = VK_PIPELINE_STAGE_2_BLIT_BIT;

      if (image_width > 1)
        image_width /= 2;
      if (image_height > 1)
        image_height /= 2;
    }

    // Transition final mip to shader optimal
    image_barrier.subresourceRange.baseMipLevel = image->mip_count - 1;
    image_barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    image_barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    image_barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    image_barrier.srcStageMask = VK_PIPELINE_STAGE_2_BLIT_BIT;
    image_barrier.dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
    image_barrier.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;

    pipeline_barrier(&image_barrier, 1);

  } else {
    transition_image(
        image, image->current_layout, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_PIPELINE_STAGE_2_COPY_BIT, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT);
  }
  image->current_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
}

void VulkanCommandBuffer::push_marker(cstring name) {
#ifdef _DEBUG
  VkDebugUtilsLabelEXT label = {VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT};
  label.pLabelName = name;
  label.color[0] = 1.0f;
  label.color[1] = 1.0f;
  label.color[2] = 1.0f;
  label.color[3] = 1.0f;
  device->pfnCmdBeginDebugUtilsLabelEXT(vk_handle, &label);
#endif
}

void VulkanCommandBuffer::insert_marker(cstring name) {
#ifdef _DEBUG
  VkDebugUtilsLabelEXT label = {VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT};
  label.pLabelName = name;
  label.color[0] = 1.0f;
  label.color[1] = 1.0f;
  label.color[2] = 1.0f;
  label.color[3] = 1.0f;
  device->pfnCmdInsertDebugUtilsLabelEXT(vk_handle, &label);
#endif
}

void VulkanCommandBuffer::pop_marker() {
#ifdef _DEBUG
  device->pfnCmdEndDebugUtilsLabelEXT(vk_handle);
#endif // _DEBUG
}

#pragma endregion VulkanCommandBuffer
} // namespace Helix
