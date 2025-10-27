#include "CommandBuffer.hpp"
#include "Renderer/RendererBackend.hpp"

namespace Helix {
struct VkContext final : public Context {
  virtual void begin(u32 cbuffer_index) override;
  virtual void end(u32 cbuffer_index) override;
  virtual void resource_barrier(const BarrierDescription *barrier) override;

  // Graphics
  virtual void set_viewport(f32 x, f32 y, f32 width, f32 height, f32 min_depth,
                            f32 max_depth) override;
  virtual void set_scissor(f32 x, f32 y, f32 width, f32 height) override;
  virtual void bind_pipeline(PipelineHandle handle) override;
  virtual void bind_vertex_buffer() override;
  virtual void bind_index_buffer() override;
  virtual void draw(u32 vertex_count, u32 instance_count, u32 first_vertex,
                    u32 first_instance) override;
  virtual void bind_renderpass(RenderPassHandle handle) override;
  virtual void end_current_pass() override;
  // Compute
  virtual void dispatch(u32 x, u32 y, u32 z) override;
  // Transfer
  virtual void data_to_buffer() override;
  virtual void buffer_to_buffer() override;
  virtual void buffer_to_texture() override;

  VkGpuDevice *device;
  VkQueue vk_queue;
  VkCommandPool vk_command_pool;
  // TODO: Make the size configurable
  VulkanCommandBuffer command_buffers[2];
  u32 ready_buffers_index[2];
  u32 ready_buffer_count;
  u32 cbuffer_index;

  // TODO Make more configurable
  VkSemaphore wait_semaphore;
  VkSemaphore signal_semaphore;
  VkSemaphore timeline_semaphore;
  u64 signal_value;
};

struct VkWorkReceipt final : public WorkReceipt {
  u64 wait_value;
  VkSemaphore vk_timeline_semaphore;
};

} // namespace Helix
