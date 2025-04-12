#include "CommandBuffer.hpp"
#include "Core/Memory.hpp"
#include "Renderer/GPUResourceTypes.hpp"
#include "Renderer/Vulkan/VulkanTypes.hpp"
#include "VulkanBackend.hpp"
#include "VulkanUtils.hpp"
#include <vulkan/vulkan_core.h>

namespace Helix {

#pragma region VulkanCommandBuffer

void VulkanCommandBuffer::init(VkCommandPool pool, VkCommandBufferLevel level,
                               VulkanBackend *_backend, cstring name) {
  backend = _backend;

  VkCommandBufferAllocateInfo alloc_info{};
  alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  alloc_info.commandPool = pool;
  alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  alloc_info.commandBufferCount = 1;

  VK_CHECK(
      vkAllocateCommandBuffers(backend->vk_device, &alloc_info, &vk_handle));
  if (name)
    backend->set_resource_name(VK_OBJECT_TYPE_COMMAND_BUFFER, (u64)vk_handle,
                               name);
  state = CommandBufferState::Initial;
  vk_pool = pool;
}

void VulkanCommandBuffer::reset() {
  VK_CHECK(vkResetCommandBuffer(vk_handle, 0));
  state = CommandBufferState::Initial;
}

void VulkanCommandBuffer::free() {
  vkFreeCommandBuffers(backend->vk_device, vk_pool, 1, &vk_handle);
  state = CommandBufferState::Invalid;
}

void VulkanCommandBuffer::begin(VkCommandBufferUsageFlags flags) {
  VkCommandBufferBeginInfo begin_info{};
  begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  begin_info.flags = flags;

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
  VulkanImage *image = backend->images.obtain(image_handle);
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

void VulkanCommandBuffer::bind_renderpass(VkExtent2D extents,
                                          VkImageView view) {
  VkRenderingAttachmentInfo color_attachment_info{
      VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
  color_attachment_info.imageView = view;
  color_attachment_info.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
  color_attachment_info.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  color_attachment_info.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  color_attachment_info.clearValue = {{{0.f, 0.f, 0.1f, 1.0f}}};
  color_attachment_info.resolveMode = VK_RESOLVE_MODE_NONE;

  VkRenderingAttachmentInfo depth_attachment_info{
      VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
  depth_attachment_info.imageView =
      backend->image_views.obtain(backend->depth_handle)->vk_handle;
  depth_attachment_info.imageLayout =
      VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
  depth_attachment_info.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  depth_attachment_info.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  depth_attachment_info.clearValue.color = {{0.0f, 0.0f, 0.0f, 1.0f}};
  depth_attachment_info.clearValue.depthStencil = {1.f, 0};
  depth_attachment_info.resolveMode = VK_RESOLVE_MODE_NONE;

  VkRenderingInfo render_info{VK_STRUCTURE_TYPE_RENDERING_INFO};
  render_info.layerCount = 1;
  render_info.renderArea = {{0, 0}, extents};
  render_info.viewMask = 0;
  render_info.colorAttachmentCount = 1;
  render_info.pColorAttachments = &color_attachment_info;
  render_info.pDepthAttachment = &depth_attachment_info;
  render_info.pStencilAttachment = nullptr;

  vkCmdBeginRendering(vk_handle, &render_info);
}

void VulkanCommandBuffer::end_current_renderpass() {
  vkCmdEndRendering(vk_handle);
}

void VulkanCommandBuffer::bind_pipeline(PipelineHandle handle) {
  VulkanPipeline *pipeline = backend->access_pipeline(handle);
  vkCmdBindPipeline(vk_handle, pipeline->bind_point, pipeline->vk_handle);
}

void VulkanCommandBuffer::bind_viewport(VkExtent2D extents) {
  VkViewport viewport{};
  viewport.x = 0.0f;
  viewport.y = 0.0f;
  viewport.width = (f32)extents.width;
  viewport.height = (f32)extents.height;
  viewport.minDepth = 0.0f;
  viewport.maxDepth = 1.0f;
  vkCmdSetViewport(vk_handle, 0, 1, &viewport);
}

void VulkanCommandBuffer::bind_scissors(VkRect2D rect) {
  vkCmdSetScissor(vk_handle, 0, 1, &rect);
}

void VulkanCommandBuffer::bind_vertex_buffer(BufferHandle handle,
                                             u32 first_binding,
                                             u32 binding_count) {

  VulkanBuffer *buffer = backend->access_buffer(handle);
  VkBuffer vertex_buffers[] = {buffer->vk_handle};
  VkDeviceSize offsets[] = {0};

  vkCmdBindVertexBuffers(vk_handle, first_binding, binding_count,
                         vertex_buffers, offsets);
}

void VulkanCommandBuffer::bind_index_buffer(BufferHandle handle, u32 offset,
                                            VkIndexType index_type) {
  VulkanBuffer *buffer = backend->access_buffer(handle);
  vkCmdBindIndexBuffer(vk_handle, buffer->vk_handle, offset, index_type);
}

void VulkanCommandBuffer::bind_descriptor_sets(PipelineHandle pipeline_handle,
                                               VkDescriptorSet dset,
                                               u32 set_index) {

  VulkanPipeline *pipeline = backend->access_pipeline(pipeline_handle);
  vkCmdBindDescriptorSets(vk_handle, pipeline->bind_point, pipeline->vk_layout,
                          set_index, 1, &dset, 0, nullptr);
}

void VulkanCommandBuffer::push_constants(VkPipelineLayout layout,
                                         VkShaderStageFlagBits stage,
                                         u32 offset, u32 size, void *data) {
  vkCmdPushConstants(vk_handle, layout, stage, offset, size, data);
}

void VulkanCommandBuffer::draw_indexed(u32 index_count, u32 instance_count,
                                       u32 first_index, i32 vertex_offset,
                                       u32 first_instance) {

  vkCmdDrawIndexed(vk_handle, index_count, instance_count, first_index,
                   vertex_offset, first_instance);
}

void VulkanCommandBuffer::copy_buffer_to_buffer(VkBuffer dst_buffer,
                                                VkBuffer src_buffer, u32 size,
                                                VkQueue vk_queue) {
  // TODO: Add a flag that lets you record multiple commands before submitting

  begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);

  VkBufferCopy2 region{VK_STRUCTURE_TYPE_BUFFER_COPY_2};
  region.srcOffset = 0;
  region.dstOffset = 0;
  region.size = size;

  VkCopyBufferInfo2 buffer_info{VK_STRUCTURE_TYPE_COPY_BUFFER_INFO_2};
  buffer_info.srcBuffer = src_buffer;
  buffer_info.dstBuffer = dst_buffer;
  buffer_info.regionCount = 1;
  buffer_info.pRegions = &region;

  vkCmdCopyBuffer2(vk_handle, &buffer_info);

  end();

  VkSubmitInfo submit_info{};
  submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
  submit_info.commandBufferCount = 1;
  submit_info.pCommandBuffers = &vk_handle;

  vkQueueSubmit(vk_queue, 1, &submit_info, VK_NULL_HANDLE);
  vkQueueWaitIdle(vk_queue);
}

void VulkanCommandBuffer::copy_buffer_to_image(TextureHandle dst_image,
                                               VkBuffer src_buffer, u32 size,
                                               VkQueue vk_queue) {
  // Tranisiton image
  VulkanImage *image = backend->access_image(dst_image);
  transition_image(
      image, image->current_layout, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
      VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT, VK_PIPELINE_STAGE_2_COPY_BIT);

  VkBufferImageCopy2 region{VK_STRUCTURE_TYPE_BUFFER_IMAGE_COPY_2};
  region.bufferOffset = 0;
  region.imageSubresource.aspectMask = has_depth_or_stencil(image->format)
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
}

void VulkanCommandBuffer::push_marker(cstring name) {
#ifdef _DEBUG
  VkDebugUtilsLabelEXT label = {VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT};
  label.pLabelName = name;
  label.color[0] = 1.0f;
  label.color[1] = 1.0f;
  label.color[2] = 1.0f;
  label.color[3] = 1.0f;
  backend->pfnCmdBeginDebugUtilsLabelEXT(vk_handle, &label);
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
  backend->pfnCmdInsertDebugUtilsLabelEXT(vk_handle, &label);
#endif
}

void VulkanCommandBuffer::pop_marker() {
#ifdef _DEBUG
  backend->pfnCmdEndDebugUtilsLabelEXT(vk_handle);
#endif // _DEBUG
}

#pragma endregion VulkanCommandBuffer

#pragma region CommandBufferManager
void CommandBufferManager::init(VulkanBackend *_backend, u32 queue_family_index,
                                u32 num_threads, u32 _max_frames_in_flight,
                                cstring name) {
  backend = _backend;
  max_frames_in_flight = _max_frames_in_flight;
  HeapAllocator *allocator = &MemoryService::instance()->system_allocator;
  vk_command_pools.init(allocator, num_threads, num_threads);
  command_buffers.init(allocator, num_threads * max_frames_in_flight,
                       num_threads * max_frames_in_flight);
  for (u32 i = 0; i < num_threads; ++i) {
    VkCommandPoolCreateInfo pool_info{
        VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pool_info.queueFamilyIndex = queue_family_index;
    VK_CHECK(vkCreateCommandPool(backend->vk_device, &pool_info,
                                 backend->vk_allocation_callbacks,
                                 &vk_command_pools[i]));
    if (name) {
      backend->set_resource_name(
          VK_OBJECT_TYPE_COMMAND_POOL, (u64)vk_command_pools[i],
          backend->string_buffer.append_use_f("%s_%d", name, i));
    }
    for (u32 j = 0; j < max_frames_in_flight; ++j) {
      command_buffers[(i * max_frames_in_flight) + j].init(
          vk_command_pools[i], VK_COMMAND_BUFFER_LEVEL_PRIMARY, backend,
          backend->string_buffer.append_use_f("%s%d_CommandBuffer_%d", name, i,
                                              j));
    }
  }
}

void CommandBufferManager::shutdown() {
  for (VulkanCommandBuffer command_buffer : command_buffers) {
    command_buffer.free();
  }
  for (VkCommandPool vk_command_pool : vk_command_pools) {

    vkDestroyCommandPool(backend->vk_device, vk_command_pool,
                         backend->vk_allocation_callbacks);
  }
  command_buffers.shutdown();
  vk_command_pools.shutdown();
}

void CommandBufferManager::reset_pool(u32 thread_index) {
  VkCommandPool &vk_command_pool = vk_command_pools[thread_index];
  vkResetCommandPool(backend->vk_device, vk_command_pool, 0);
  for (u32 i = 0; i < max_frames_in_flight; ++i) {
    command_buffers[(thread_index * max_frames_in_flight) + i].state =
        CommandBufferState::Initial;
  }
}

VulkanCommandBuffer *CommandBufferManager::get_command_buffer(u32 frame,
                                                              u32 thread_index,
                                                              bool begin) {
  VulkanCommandBuffer *buffer =
      &command_buffers[(max_frames_in_flight * thread_index) + frame];
  if (begin) {
    buffer->begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
  }
  return buffer;
}
#pragma endregion CommandBufferManager
} // namespace Helix
