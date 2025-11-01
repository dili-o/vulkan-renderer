#include "CommandBuffer.hpp"
#include "Renderer/RendererBackend.hpp"

namespace hlx {
struct VkContext final : public Context {
  virtual void begin(u32 cbuffer_index) override;
  virtual void end(u32 cbuffer_index) override;
  virtual void wait_on_queue() override;
  virtual void resource_barrier(const BarrierDescription *barrier) override;
  virtual void bind_set(BindingSetHandle set, u32 set_index) override;

  virtual void push_shader_constants(u32 size, void *data) override;
  // Graphics
  virtual void set_viewport(f32 x, f32 y, f32 width, f32 height, f32 min_depth,
                            f32 max_depth) override;
  virtual void set_scissor(f32 x, f32 y, f32 width, f32 height) override;
  virtual void bind_pipeline(PipelineHandle handle) override;
  virtual void bind_vertex_buffer(BufferHandle handle, u32 first_binding,
                                  u32 binding_count) override;
  virtual void bind_index_buffer(BufferHandle handle, u32 offset,
                                 bool use_u16_index) override;
  virtual void draw(u32 vertex_count, u32 instance_count, u32 first_vertex,
                    u32 first_instance) override;
  virtual void draw_indexed(u32 index_count, u32 instance_count,
                            u32 first_index, i32 vertex_offset,
                            u32 first_instance) override;
  virtual void bind_renderpass(RenderPassHandle render_pass_handle,
                               u32 extents[2], u32 offsets[2]) override;
  virtual void end_current_pass() override;
  // Compute
  virtual void dispatch(u32 x, u32 y, u32 z) override;
  // Transfer
  virtual void copy_buffer_to_buffer(BufferHandle dst_buffer,
                                     BufferHandle src_buffer,
                                     u64 size) override;
  virtual void copy_buffer_to_texture(TextureHandle dst_texture,
                                      BufferHandle src_buffer,
                                      u64 copy_size) override;

  inline VulkanCommandBuffer &current_cb() {
    return command_buffers[cbuffer_index];
  }

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

} // namespace hlx
