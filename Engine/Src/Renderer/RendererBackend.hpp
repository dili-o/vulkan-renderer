#pragma once
#include "GPUResources.hpp"
#include "RendererTypes.hpp"

namespace Helix {

struct Platform;
struct BufferCreation;
struct PipelineCreation;

struct RendererBackend {
  virtual bool init(void *config) = 0;
  virtual bool shutdown() = 0;
  virtual bool on_resize(u16 width, u16 height) = 0;
  virtual bool begin_frame(RenderPacket *packet) = 0;
  virtual bool end_frame(RenderPacket *packet) = 0;

  virtual BufferHandle create_buffer(BufferCreation &creation) = 0;
  virtual PipelineHandle create_pipeline(PipelineCreation &creation) = 0;
  virtual TextureHandle create_texture(TextureCreation &creation) = 0;

  virtual void destroy_buffer(BufferHandle handle) = 0;
  virtual void destroy_pipeline(BufferHandle handle) = 0;
  virtual void destroy_texture(TextureHandle handle) = 0;

  virtual bool update_shader_uniform_set(ShaderUniformSet &set,
                                         PipelineHandle pipeline) = 0;

  virtual void print_gpu_stats() = 0;

  Platform *platform{nullptr};
  u64 frame_number{0};
};

RendererBackend *RendererBackendCreate(RendererBackendType type);

} // namespace Helix
