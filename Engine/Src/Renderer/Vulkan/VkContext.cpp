#include "VkContext.hpp"
#include "Renderer/Vulkan/VkGpuDevice.hpp"

namespace Helix {
void VkContext::begin(u32 cbuffer_index_) {
  command_buffers[cbuffer_index_].begin();
  cbuffer_index = cbuffer_index_;
};

void VkContext::end(u32 cbuffer_index) {
  command_buffers[cbuffer_index].end();
  ready_buffers_index[ready_buffer_count++] = cbuffer_index;
};

void VkContext::resource_barrier(const BarrierDescription *barrier) {
  if (barrier->resource_type == ResourceType::Texture) {
#define SRC_INDEX 0
#define DST_INDEX 1
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
    command_buffers[cbuffer_index].transition_image(
        image, src_layout, dst_layout, *src_stage, *dst_stage);
  } else {
    HASSERT_MSG(false, "Implement Buffer Resource Barriers");
  }
};

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
  command_buffers[cbuffer_index].bind_viewport(&viewport);
}

void VkContext::set_scissor(f32 x, f32 y, f32 width, f32 height) {
  VkRect2D rect{};
  rect.offset = {(i32)x, (i32)y};
  rect.extent = {(u32)width, (u32)height};
  command_buffers[cbuffer_index].bind_scissors(rect);
}

void VkContext::bind_pipeline(PipelineHandle handle) {
  command_buffers[cbuffer_index].bind_pipeline(handle);
};

void VkContext::bind_vertex_buffer() {};

void VkContext::bind_index_buffer() {};

void VkContext::draw(u32 vertex_count, u32 instance_count, u32 first_vertex,
                     u32 first_instance) {
  command_buffers[cbuffer_index].draw(vertex_count, instance_count,
                                      first_vertex, first_instance);
};

void VkContext::bind_renderpass(RenderPassHandle handle) {
  command_buffers[cbuffer_index].bind_renderpass(handle);
}

void VkContext::end_current_pass() {
  command_buffers[cbuffer_index].end_current_renderpass();
}

// Compute
void VkContext::dispatch(u32 x, u32 y, u32 z) {};
// Transfer
void VkContext::data_to_buffer() {};
void VkContext::buffer_to_buffer() {};
void VkContext::buffer_to_texture() {};

} // namespace Helix
