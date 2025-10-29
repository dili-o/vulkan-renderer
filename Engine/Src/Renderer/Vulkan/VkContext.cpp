#include "VkContext.hpp"
#include "Renderer/Vulkan/VkGpuDevice.hpp"

namespace Helix {
void VkContext::begin(u32 cbuffer_index_) {
  cbuffer_index = cbuffer_index_;
  current_cb().begin();
}

void VkContext::end(u32 cbuffer_index) {
  current_cb().end();
  ready_buffers_index[ready_buffer_count++] = cbuffer_index;
}

void VkContext::wait_on_queue() { vkQueueWaitIdle(vk_queue); }

void VkContext::resource_barrier(const BarrierDescription *barrier) {
#define SRC_INDEX 0
#define DST_INDEX 1
  if (barrier->resource_type == ResourceType::Texture) {
    VkPipelineStageFlags2 stages[2];
    VkImageLayout src_layout = VK_IMAGE_LAYOUT_UNDEFINED;
    VkImageLayout dst_layout = VK_IMAGE_LAYOUT_UNDEFINED;
    VkPipelineStageFlags2 *src_stage = stages;
    VkPipelineStageFlags2 *dst_stage = stages + 1;

    if (barrier->dst_state == ResourceState::RenderTarget) {
      dst_layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
      stages[DST_INDEX] = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    } else if (barrier->dst_state == ResourceState::Present) {
      dst_layout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
      dst_stage = stages + SRC_INDEX;
    }

    if (barrier->src_state == ResourceState::Present) {
      src_layout = VK_IMAGE_LAYOUT_UNDEFINED;
      src_stage = stages + DST_INDEX;
    } else if (barrier->src_state == ResourceState::RenderTarget) {
      src_layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
      stages[SRC_INDEX] = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    }

    VulkanImage *image = device->access_image(barrier->resource_handle);
    current_cb().transition_image(image, src_layout, dst_layout, *src_stage,
                                  *dst_stage);
  } else {
    HASSERT_MSG(false, "Implement Buffer Resource Barriers");
  }
}

void VkContext::bind_set(BindingSetHandle set_handle, u32 set_index) {
  VulkanDescriptorSet *set = device->access_descriptor_set(set_handle);
  current_cb().bind_descriptor_sets(set->vk_handle, set_index);
}

void VkContext::push_shader_constants(u32 size, void *data) {
  current_cb().push_constants(0, size, data);
}

// Graphics
void VkContext::set_viewport(f32 x, f32 y, f32 width, f32 height, f32 min_depth,
                             f32 max_depth) {
  // Setup view_port and scissor
  VkViewport viewport{};
  viewport.x = x;
  viewport.y = y;
  viewport.width = width;
  viewport.height = height;
  viewport.minDepth = min_depth;
  viewport.maxDepth = max_depth;
  current_cb().bind_viewport(&viewport);
}

void VkContext::set_scissor(f32 x, f32 y, f32 width, f32 height) {
  VkRect2D rect{};
  rect.offset = {(i32)x, (i32)y};
  rect.extent = {(u32)width, (u32)height};
  current_cb().bind_scissors(rect);
}

void VkContext::bind_pipeline(PipelineHandle handle) {
  current_cb().bind_pipeline(handle);
}

void VkContext::bind_vertex_buffer(BufferHandle handle, u32 first_binding,
                                   u32 binding_count) {
  current_cb().bind_vertex_buffer(handle, first_binding, binding_count);
}

void VkContext::bind_index_buffer(BufferHandle handle, u32 offset,
                                  bool use_u16_index) {
  current_cb().bind_index_buffer(handle, offset,
                                 use_u16_index ? VK_INDEX_TYPE_UINT16
                                               : VK_INDEX_TYPE_UINT32);
}

void VkContext::draw(u32 vertex_count, u32 instance_count, u32 first_vertex,
                     u32 first_instance) {
  current_cb().draw(vertex_count, instance_count, first_vertex, first_instance);
}

void VkContext::draw_indexed(u32 index_count, u32 instance_count,
                             u32 first_index, i32 vertex_offset,
                             u32 first_instance) {
  current_cb().draw_indexed(index_count, instance_count, first_index,
                            vertex_offset, first_instance);
}

void VkContext::bind_renderpass(RenderPassHandle handle) {
  current_cb().bind_renderpass(handle);
}

void VkContext::end_current_pass() { current_cb().end_current_renderpass(); }

// Compute
void VkContext::dispatch(u32 x, u32 y, u32 z) {};
// Transfer
void VkContext::copy_data_to_buffer() {}

void VkContext::copy_buffer_to_buffer() {}

void VkContext::copy_buffer_to_texture(TextureHandle dst_texture,
                                       BufferHandle src_buffer, u64 copy_size) {
  VulkanImageView *view = device->access_image_view(dst_texture);
  VulkanBuffer *buffer = device->access_buffer(src_buffer);
  current_cb().copy_buffer_to_image(view->image, buffer->vk_handle, copy_size,
                                    vk_queue, false);
}

} // namespace Helix
