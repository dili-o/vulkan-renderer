#pragma once

#include "Containers/ResourcePool.hpp"
#include "Core/Service.hpp"
#include "Core/String.hpp"
#include "Renderer/GPUResourceTypes.hpp"
#include "Renderer/GPUResources.hpp"
#include "RendererTypes.hpp"

namespace Helix {

const u32 max_frames_in_flight = 2;

struct RendererBackend;

struct RendererFrontEnd : public Service {
  virtual void init(void *config) override;
  virtual void shutdown() override;

  HELIX_DECLARE_SERVICE(RendererFrontEnd);

public:
  void on_resize(u16 width, u16 height);

  bool render_frame(RenderPacket *packet);

  bool begin_frame(RenderPacket *packet);

  bool end_frame(RenderPacket *packet);

  BufferHandle create_buffer(BufferCreation &creation);
  PipelineHandle create_pipeline(PipelineCreation &creation);
  TextureHandle create_texture(TextureCreation &creation);

  BufferResource *access_buffer(BufferHandle handle);

  void destroy_buffer(BufferHandle handle);
  void destroy_pipeline(PipelineHandle handle);
  void destroy_texture(TextureHandle handle);

  bool update_shader_uniform_set(ShaderUniformSet &set,
                                 PipelineHandle pipeline);

  void print_gpu_stats();

  u32 current_frame;
  RendererBackend *backend{nullptr};

  StringBuffer string_buffer{};

  BufferHandle uniform_buffers[max_frames_in_flight];

  PipelineHandle pbr_pipeline{};

  TextureHandle default_albedo_texture;
  TextureHandle default_normal_texture;

private:
  ResourcePool<BufferResource> buffers{};
  ResourcePool<PipelineResource> pipelines{};
  ResourcePool<TextureResource> textures{};
};

} // namespace Helix
