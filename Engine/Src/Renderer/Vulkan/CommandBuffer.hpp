#pragma once

#include "Core/Defines.hpp"
#include "Renderer/GPUResourceTypes.hpp"
#include "VulkanTypes.hpp"

namespace Helix {
struct VulkanBackend;

namespace CommandBufferState {
enum Enum { Initial, Recording, Executable, Pending, Invalid };
}

struct VulkanCommandBuffer {
  void init(VkCommandPool pool, VkCommandBufferLevel level,
            VulkanBackend *backend);
  void begin(VkCommandBufferUsageFlags flags = 0);
  void end();
  void reset();
  void free();

  // TODO: Fully implement this when you've added textures
  void transition_image(VkImage image);
  void transition_image2(VkImage image);
  // TODO: Fully implement this when you've added renderpasses and framebuffers
  void bind_renderpass(VkExtent2D extents, VkImageView image);
  void end_current_renderpass();

  void bind_pipeline(PipelineHandle handle);

  // TODO: Maybe bind viewport and scissors in bind_renderpass instead
  void bind_viewport(VkExtent2D extents);
  void bind_scissors(VkExtent2D extents);

  // TODO: Make more configurable
  void bind_vertex_buffer(VkBuffer vertex_buffer);
  void bind_index_buffer(VkBuffer index_buffer);
  void bind_descriptor_sets(PipelineHandle pipeline_handle,
                            VkDescriptorSet dset);

  void draw_indexed(u32 index_count, u32 instance_count, u32 first_index,
                    i32 vertex_offset, u32 first_instance);

  // TODO: Right now each call always submits and waits for the queue to be idle
  // This should be able to record any upload commands
  void copy_buffer_to_buffer(VkBuffer dst_buffer, VkBuffer src_buffer, u32 size,
                             VkQueue vk_queue);

  VulkanBackend *backend{nullptr};
  VkCommandBuffer vk_handle{VK_NULL_HANDLE};
  VkCommandPool vk_pool{VK_NULL_HANDLE};
  CommandBufferState::Enum state;
};

struct CommandBufferManager {
  void init(VulkanBackend *backend, u32 queue_family_index, u32 num_threads,
            u32 max_frames_in_flight);
  void shutdown();

  void reset_pool(u32 thread_index);

  VulkanCommandBuffer *get_command_buffer(u32 frame, u32 thread_index,
                                          bool begin);

  u32 max_frames_in_flight = 0;
  Array<VkCommandPool> vk_command_pools;
  Array<VulkanCommandBuffer> command_buffers;
  // VkCommandPool vk_transfer_pool;
  VulkanBackend *backend = nullptr;
};
} // namespace Helix
