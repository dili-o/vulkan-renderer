#pragma once

#include "Core/Defines.hpp"
#include "Renderer/GPUResourceTypes.hpp"
#include "VulkanTypes.hpp"
#include <vulkan/vulkan_core.h>

namespace Helix {
struct VulkanBackend;

namespace CommandBufferState {
enum Enum { Initial, Recording, Executable, Pending, Invalid };
}

struct VulkanCommandBuffer {
  void init(VkCommandPool pool, VkCommandBufferLevel level,
            VulkanBackend *backend, cstring name = nullptr);
  void begin(VkCommandBufferUsageFlags flags = 0);
  void end();
  void reset();
  void free();

  void transition_image(VulkanImage *image, VkImageLayout old_layout,
                        VkImageLayout new_layout,
                        VkPipelineStageFlags2 src_stage,
                        VkPipelineStageFlags2 dst_stage,
                        u32 src_queue_family_index = VK_QUEUE_FAMILY_IGNORED,
                        u32 dst_queue_family_index = VK_QUEUE_FAMILY_IGNORED);

  void transition_image(TextureHandle image_handle, VkImageLayout old_layout,
                        VkImageLayout new_layout,
                        VkPipelineStageFlags2 src_stage,
                        VkPipelineStageFlags2 dst_stage,
                        u32 src_queue_family_index = VK_QUEUE_FAMILY_IGNORED,
                        u32 dst_queue_family_index = VK_QUEUE_FAMILY_IGNORED);

  // TODO: Support buffer and memory barriers
  void pipeline_barrier(VkImageMemoryBarrier2 *image_memory_barriers,
                        u32 image_memory_barrier_count);

  // TODO: Fully implement this when you've added renderpasses and framebuffers
  void bind_renderpass(VkExtent2D extents, VkImageView view);
  void end_current_renderpass();

  void bind_pipeline(PipelineHandle handle);

  // TODO: Maybe bind viewport and scissors in bind_renderpass instead
  void bind_viewport(VkExtent2D extents);
  void bind_scissors(VkExtent2D extents);

  // TODO: Make more configurable
  void bind_vertex_buffer(BufferHandle handle);
  void bind_vertex_buffer(VkBuffer vertex_buffer);
  void bind_index_buffer(BufferHandle handle);
  void bind_index_buffer(VkBuffer index_buffer);
  void bind_descriptor_sets(PipelineHandle pipeline_handle,
                            VkDescriptorSet dset, u32 set_index);

  void push_constants(VkPipelineLayout layout, VkShaderStageFlagBits stage,
                      u32 offset, u32 size, void *data);

  void draw_indexed(u32 index_count, u32 instance_count, u32 first_index,
                    i32 vertex_offset, u32 first_instance);

  // TODO: Right now each call always submits and waits for the queue to be idle
  // This should be able to record any upload commands
  // Maybe also have a version of this function that takes in ResourceHandles
  // instead
  void copy_buffer_to_buffer(VkBuffer dst_buffer, VkBuffer src_buffer, u32 size,
                             VkQueue vk_queue);

  // Note: This function transitions the image to
  // VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL but doesn't transition it back to it's
  // previous state
  void copy_buffer_to_image(TextureHandle dst_image, VkBuffer src_buffer,
                            u32 size, VkQueue vk_queue);

  void push_marker(cstring name);
  void insert_marker(cstring name);
  void pop_marker();

  VulkanBackend *backend{nullptr};
  VkCommandBuffer vk_handle{VK_NULL_HANDLE};
  VkCommandPool vk_pool{VK_NULL_HANDLE};
  CommandBufferState::Enum state;
};

struct CommandBufferManager {
  void init(VulkanBackend *backend, u32 queue_family_index, u32 num_threads,
            u32 max_frames_in_flight, cstring name = nullptr);
  void shutdown();

  void reset_pool(u32 thread_index);

  VulkanCommandBuffer *get_command_buffer(u32 frame, u32 thread_index,
                                          bool begin);

  // TODO: Make a function to submit all command buffers for a pool index;
  u32 max_frames_in_flight = 0;
  Array<VkCommandPool> vk_command_pools;
  Array<VulkanCommandBuffer> command_buffers;
  // VkCommandPool vk_transfer_pool;
  VulkanBackend *backend = nullptr;
};
} // namespace Helix
