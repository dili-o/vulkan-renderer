#pragma once

#include "Containers/ResourcePool.hpp"
#include "Core/Service.hpp"
#include "Core/String.hpp"
#include "GPUResources.hpp"
#include "Renderer/GPUResourceTypes.hpp"
#include "RendererTypes.hpp"

namespace Helix {

const u32 max_frames_in_flight = 2;

struct RendererBackend;

struct RendererFrontEnd : public Service {
  virtual void init(void *config) override;
  virtual void shutdown() override;

  HELIX_DECLARE_SERVICE(RendererFrontEnd);

  void on_resize(u16 width, u16 height);

  bool draw_frame(RenderPacket *packet);

  bool begin_frame(RenderPacket *packet);

  bool end_frame(RenderPacket *packet);

  bool load_model(cstring path);

  BufferHandle create_buffer(BufferCreation &creation);
  void destroy_buffer(BufferHandle handle);

  PipelineHandle create_pipeline(PipelineCreation &creation);
  void destroy_pipeline(PipelineHandle handle);

  bool update_shader_uniform_set(ShaderUniformSet &set,
                                 PipelineHandle pipeline);

  u32 current_frame;
  RendererBackend *backend{nullptr};

  StringBuffer string_buffer{};

  ResourcePool<BufferResource> buffers{};
  ResourcePool<PipelineResource> pipelines{};
  BufferHandle vertex_buffer{};
  BufferHandle index_buffer{};
  BufferHandle uniform_buffers[max_frames_in_flight];
  BufferHandle second_buffers[max_frames_in_flight];
  PipelineHandle pipeline{};
};
} // namespace Helix
