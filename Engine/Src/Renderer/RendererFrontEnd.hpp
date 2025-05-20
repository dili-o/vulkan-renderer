#pragma once

#include "Core/Service.hpp"
#include "Core/String.hpp"
#include "Renderer/GPUResourceTypes.hpp"
#include "Renderer/GPUResources.hpp"
#include "RendererTypes.hpp"

namespace Helix {

const u32 max_frames_in_flight = 2;

struct RendererBackend;
struct Scene;

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
  BindingSetLayoutHandle
  create_binding_set_layout(BindingSetLayoutCreation &creation);
  BindingSetHandle create_binding_set(BindingSetCreation &creation);
  RenderPassHandle create_render_pass(RenderPassCreation &creation);

  BufferInfo access_buffer_view(BufferHandle handle);
  TextureInfo access_texture_view(TextureHandle handle);
  PipelineInfo access_pipeline_view(PipelineHandle handle);

  void destroy_buffer(BufferHandle handle);
  void destroy_pipeline(PipelineHandle handle);
  void destroy_texture(TextureHandle handle);
  void destroy_binding_set(BindingSetHandle handle);
  void destroy_render_pass(RenderPassHandle handle);

  bool update_binding_set(BindingSetHandle set,
                          BindingSetUpdateInfo *update_infos, u32 update_count);
  void set_pipeline_binding_set(PipelineHandle pipeline, BindingSetHandle set,
                                u32 set_index);

  void upload_buffer_data(void *data, BufferHandle dst_buffer, u32 size,
                          u32 offset);
  void update_draw_commands(Scene *scene);
  void print_gpu_stats();

  u32 current_frame;
  RendererBackend *backend{nullptr};

  StringBuffer string_buffer{};

  BufferHandle uniform_buffers[max_frames_in_flight];

  PipelineHandle pbr_pipeline{};
  PipelineHandle depth_prepass_pipeline{};

  TextureHandle default_albedo_texture;
  TextureHandle default_normal_texture;

  BindingSetLayoutHandle scene_set_layout{};
  BindingSetHandle scene_sets[max_frames_in_flight];

  BindingSetLayoutHandle bindless_set_layout{};
  BindingSetHandle bindless_set{};

  RenderPassHandle depth_prepass{};
};

} // namespace Helix
