#pragma once
#include "GPUResources.hpp"
#include "RendererTypes.hpp"

namespace Helix {

struct Platform;
struct BufferCreation;
struct PipelineCreation;
struct Scene;

struct RendererBackend {
  virtual bool init(void *config) = 0;
  virtual bool shutdown() = 0;
  virtual bool on_resize(u16 width, u16 height) = 0;
  virtual bool begin_frame(RenderPacket *packet) = 0;
  virtual void render_frame(RenderPacket *packet) = 0;
  virtual bool end_frame(RenderPacket *packet) = 0;

  virtual BufferHandle create_buffer(BufferCreation &creation) = 0;
  virtual PipelineHandle create_pipeline(PipelineCreation &creation) = 0;
  virtual TextureHandle create_texture(TextureCreation &creation) = 0;
  virtual BindingSetLayoutHandle
  create_binding_set_layout(BindingSetLayoutCreation &creation) = 0;
  virtual BindingSetHandle create_binding_set(BindingSetCreation &creation) = 0;
  virtual RenderPassHandle create_render_pass(RenderPassCreation &creation) = 0;

  virtual BufferInfo access_buffer_view(BufferHandle handle) = 0;
  virtual TextureInfo access_texture_view(TextureHandle handle) = 0;
  virtual PipelineInfo access_pipeline_view(PipelineHandle handle) = 0;

  virtual void destroy_buffer(BufferHandle handle) = 0;
  virtual void destroy_pipeline(BufferHandle handle) = 0;
  virtual void destroy_texture(TextureHandle handle) = 0;
  virtual void destroy_binding_set(BindingSetHandle handle) = 0;
  virtual void destroy_render_pass(RenderPassHandle handle) = 0;

  virtual RenderPassHandle get_swapchain_pass() = 0;

  virtual bool update_binding_set(BindingSetHandle set,
                                  BindingSetUpdateInfo *update_infos,
                                  u32 update_count) = 0;
  virtual void set_pipeline_binding_set(PipelineHandle pipeline,
                                        BindingSetHandle set,
                                        u32 set_index) = 0;
  virtual void upload_buffer_data(void *data, BufferHandle dst_buffer, u64 size,
                                  u64 offset) = 0;
  virtual void upload_to_image(void *data, TextureHandle dst_image) = 0;

  virtual void update_draw_commands(Scene *scene) = 0;
  virtual void print_gpu_stats() = 0;

  Platform *platform{nullptr};
  u64 frame_number{0};
};

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
  virtual void resource_barrier(const BarrierDescription *barrier) = 0;

  // Graphics
  virtual void bind_pipeline() = 0;
  virtual void bind_vertex_buffer() = 0;
  virtual void bind_index_buffer() = 0;
  virtual void draw() = 0;
  virtual void bind_renderpass(RenderPassHandle handle) = 0;
  virtual void end_current_pass() = 0;
  // Compute
  virtual void dispatch(u32 x, u32 y, u32 z) = 0;
  // Transfer
  virtual void data_to_buffer() = 0;
  virtual void buffer_to_buffer() = 0;
  virtual void buffer_to_texture() = 0;

  ContextType::Enum type;
};

////////////////////////////////////////
/// GpuDevice
////////////////////////////////////////
struct GpuDevice {
  // TODO: Actually implement this
  virtual u32 create_backbuffers(u32 width, u32 height, u32 count) = 0;
  virtual void process_display_changes() = 0;
  virtual void create_buffer() = 0;
  virtual void create_texture() = 0;
  virtual void create_pipeline() = 0;
  virtual RenderPassHandle
  create_render_pass(const RenderPassCreation &creation) = 0;

  virtual void destroy_render_pass(RenderPassHandle handle) = 0;

  virtual Context *create_context(ContextType::Enum type) = 0;
  virtual void destroy_context(Context *context) = 0;
  virtual WorkReceipt *create_receipt() = 0;
  virtual void destroy_receipt(WorkReceipt *receipt) = 0;

  virtual u32 get_next_image_index(Context *context,
                                   u32 current_frame_in_flight) = 0;
  virtual TextureHandle get_backbuffer_texture(u32 index) = 0;
  virtual void submit_work(Context *context, WorkReceipt *receipt) = 0;
  virtual void wait_on_work(WorkReceipt *receipt) = 0;
  virtual void present_to_display() = 0;

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
