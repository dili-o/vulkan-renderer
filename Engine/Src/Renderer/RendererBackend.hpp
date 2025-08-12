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

RendererBackend *RendererBackendCreate(RendererBackendType type);

} // namespace Helix
