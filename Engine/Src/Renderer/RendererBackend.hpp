#pragma once
#include "GPUResources.hpp"
#include "Renderer/GPUResourceTypes.hpp"
#include "RendererTypes.hpp"

namespace Helix {

struct Platform;
struct BufferCreation;
struct PipelineCreation;

////////////////////////////////////////
/// WorkReceipt
////////////////////////////////////////
struct WorkReceipt {};

////////////////////////////////////////
/// Context
////////////////////////////////////////
struct Context {
  virtual void begin(u32 cbuffer_index) = 0;
  virtual void end(u32 cbuffer_index) = 0;
  virtual void wait_on_queue() = 0;
  virtual void resource_barrier(const BarrierDescription *barrier) = 0;
  virtual void bind_set(BindingSetHandle set, u32 set_index) = 0;
  virtual void push_shader_constants(u32 size, void *data) = 0;

  // Graphics
  virtual void set_viewport(f32 x, f32 y, f32 width, f32 height, f32 min_depth,
                            f32 max_depth) = 0;
  virtual void set_scissor(f32 x, f32 y, f32 width, f32 height) = 0;
  virtual void bind_pipeline(PipelineHandle handle) = 0;
  virtual void bind_vertex_buffer(BufferHandle handle, u32 first_binding,
                                  u32 binding_count) = 0;
  virtual void bind_index_buffer(BufferHandle handle, u32 offset,
                                 bool use_u16_index) = 0;
  virtual void draw(u32 vertex_count, u32 instance_count, u32 first_vertex,
                    u32 first_instance) = 0;
  virtual void draw_indexed(u32 index_count, u32 instance_count,
                            u32 first_index, i32 vertex_offset,
                            u32 first_instance) = 0;
  virtual void bind_renderpass(RenderPassHandle handle) = 0;
  virtual void end_current_pass() = 0;
  // Compute
  virtual void dispatch(u32 x, u32 y, u32 z) = 0;
  // Transfer
  virtual void copy_data_to_buffer() = 0;
  virtual void copy_buffer_to_buffer() = 0;
  virtual void copy_buffer_to_texture(TextureHandle dst_texture,
                                      BufferHandle src_buffer,
                                      u64 copy_size) = 0;

  ContextType::Enum type;
};

////////////////////////////////////////
/// GpuDevice
////////////////////////////////////////
struct GpuDevice {
  virtual u32 create_backbuffers(u32 width, u32 height, u32 count) = 0;
  virtual void process_display_changes() = 0;

  virtual BufferHandle create_buffer(const BufferCreation &creation) = 0;
  virtual TextureHandle create_texture(const TextureCreation &creation) = 0;
  virtual SamplerHandle create_sampler(const SamplerCreation &creation) = 0;
  virtual BindingSetHandle
  create_binding_set(const BindingSetCreation &creation) = 0;
  virtual BindingSetLayoutHandle
  create_binding_set_layout(const BindingSetLayoutCreation &creation) = 0;
  virtual PipelineHandle create_pipeline(const PipelineCreation &creation) = 0;
  virtual RenderPassHandle
  create_render_pass(const RenderPassCreation &creation) = 0;

  virtual PipelineInfo access_pipeline_view(PipelineHandle handle) = 0;

  virtual void destroy_buffer(BufferHandle handle) = 0;
  virtual void destroy_texture(TextureHandle handle) = 0;
  virtual void destroy_sampler(SamplerHandle handle) = 0;
  virtual void destroy_binding_set(BindingSetHandle handle) = 0;
  virtual void destroy_binding_set_layout(BindingSetLayoutHandle handle) = 0;
  virtual void destroy_pipeline(PipelineHandle handle) = 0;
  virtual void destroy_render_pass(RenderPassHandle handle) = 0;

  virtual void destroy_context(Context *context) = 0;
  virtual void destroy_receipt(WorkReceipt *receipt) = 0;

  virtual WorkReceipt *create_receipt() = 0;
  virtual Context *create_context(ContextType::Enum type) = 0;

  virtual u32 get_next_image_index(Context *context,
                                   u32 current_frame_in_flight) = 0;
  virtual TextureHandle get_backbuffer_texture(u32 index) = 0;
  virtual void submit_work(Context *context, WorkReceipt *receipt) = 0;
  virtual void wait_on_work(WorkReceipt *receipt) = 0;
  virtual void present_to_display() = 0;

  virtual void *get_buffer_map(BufferHandle handle) = 0;
  virtual bool update_binding_set(BindingSetHandle set,
                                  BindingSetUpdateInfo *update_infos,
                                  u32 update_count) = 0;

  virtual void set_render_pass_texture(RenderPassHandle handle,
                                       TextureHandle texture_handle,
                                       bool is_depth, u32 index = 0) = 0;

  virtual void resize_backbuffers() = 0;

  RendererBackendType type;
  bool resize_frame{false};
  u64 frame_number;
};

GpuDevice *create_device(RendererBackendType type);
bool destroy_device(GpuDevice *device);
//////////////////////////////////////////////

} // namespace Helix
