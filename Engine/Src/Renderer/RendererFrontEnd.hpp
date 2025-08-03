#pragma once

#include "Containers/HashMap.hpp"
#include "Core/Service.hpp"
#include "Core/String.hpp"
#include "Renderer/GPUResourceTypes.hpp"
#include "Renderer/GPUResources.hpp"
#include "RendererTypes.hpp"

// Pipeline Names
#define PBR_PIPELINE_NAME "Pbr_Pipeline"
#define DEPTH_PREPASS_PIPELINE_NAME "Depth_Prepass_Pipeline"
#define IMGUI_PIPELINE_NAME "ImGui_Pipeline"

namespace Helix {

const u32 max_frames_in_flight = 2;

constexpr u64 max_vertex_count = 12'338'977;

constexpr u64 max_index_count = max_vertex_count * 3;
constexpr u64 max_draw_count = (max_index_count + 2) / 3;
constexpr u64 max_material_count = (max_index_count + 2) / 3;

struct RendererBackend;
struct Scene;

struct RendererFrontEnd : public Service {
  virtual void init(void *config) override;
  virtual void shutdown() override;

  HELIX_DECLARE_SERVICE(RendererFrontEnd);

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

  void upload_buffer_data(void *data, BufferHandle dst_buffer, u64 size,
                          u64 offset);
  void update_draw_commands(Scene *scene);
  void print_gpu_stats();

  u32 current_frame;
  RendererBackend *backend{nullptr};

  StringBuffer string_buffer{};

  BufferHandle uniform_buffers[max_frames_in_flight];

  // PipelineHandle pbr_pipeline{};
  // PipelineHandle depth_prepass_pipeline{};

  TextureHandle default_albedo_texture;
  TextureHandle default_normal_texture;

  BindingSetLayoutHandle scene_set_layout{};
  BindingSetHandle scene_sets[max_frames_in_flight];

  BindingSetLayoutHandle bindless_set_layout{};
  BindingSetHandle bindless_set{};

  RenderPassHandle depth_prepass{};

  UnifiedBuffer<Vertex> unified_vertex_buffer{};
  UnifiedBuffer<u32> unified_index_buffer{};
  UnifiedBuffer<glm::mat4> unified_transform_buffer{};
  UnifiedBuffer<GPUPBRMaterial> unified_material_buffer{};
  // Caches the handles of the created pipelines
  HashMap<StringView, PipelineHandle> pipelines_map{};
};

} // namespace Helix
