#pragma once

#include "Core/Service.hpp"
#include "Core/String.hpp"
#include "Renderer/GPUResourceTypes.hpp"
#include "Renderer/GPUResources.hpp"
#include "RendererTypes.hpp"

#define MAX_DRAW_COMMANDS 1000
#define MAX_BINDLESS_UPDATE_PER_FRAME 20

// Pipeline Names
#define PBR_PIPELINE_NAME "Pbr_Pipeline"
#define DEPTH_PREPASS_PIPELINE_NAME "Depth_Prepass_Pipeline"
#define IMGUI_PIPELINE_NAME "ImGui_Pipeline"
#define FRUSTUM_PIPELINE_NAME "Frustum_Pipeline"
#define CULLING_PIPELINE_NAME "Culling_Pipeline"

namespace Helix {

const u32 max_frames_in_flight = 2;

constexpr u64 max_vertex_count = 12'338'977;

constexpr u64 max_index_count = max_vertex_count * 3;
constexpr u64 max_draw_count = (max_index_count + 2) / 3;
constexpr u64 max_material_count = (max_index_count + 2) / 3;

struct GpuDevice;
struct Context;
struct WorkReceipt;
struct TransferContext;
struct ComputeContext;

struct HLX_API RendererFrontEnd : public Service {
  virtual void init(void *config) override;
  virtual void shutdown() override;

  HELIX_DECLARE_SERVICE(RendererFrontEnd);

  void on_resize(u16 width, u16 height);
  bool begin_frame(RenderPacket *packet);
  bool end_frame(RenderPacket *packet);

  BufferHandle create_buffer(const BufferCreation &creation);
  TextureHandle create_texture(const TextureCreation &creation);
  SamplerHandle create_sampler(const SamplerCreation &creation);
  BindingSetHandle create_binding_set(const BindingSetCreation &creation);
  BindingSetLayoutHandle
  create_binding_set_layout(const BindingSetLayoutCreation &creation);
  PipelineHandle create_pipeline(const PipelineCreation &creation);
  RenderPassHandle create_render_pass(const RenderPassCreation &creation);

  void destroy_buffer(BufferHandle handle);
  void destroy_texture(TextureHandle handle);
  void destroy_sampler(SamplerHandle handle);
  void destroy_binding_set(BindingSetHandle handle);
  void destroy_binding_set_layout(BindingSetLayoutHandle handle);
  void destroy_pipeline(PipelineHandle handle);
  void destroy_render_pass(RenderPassHandle handle);

  void resize_texture(TextureHandle handle, u32 width, u32 height);

  bool update_binding_set(BindingSetHandle set,
                          BindingSetUpdateInfo *update_infos, u32 update_count);
  void set_pipeline_binding_set(PipelineHandle pipeline, BindingSetHandle set,
                                u32 set_index);

  void *get_buffer_map(BufferHandle handle);
  void copy_data_to_image(void *data, TextureHandle texture, u64 texture_size);
  void copy_data_to_buffer(void *data, BufferHandle dst_buffer, u64 size);
  void copy_buffer_to_buffer(BufferHandle src_buffer, BufferHandle dst_buffer,
                             u64 size);
  void print_gpu_stats();

  GpuDevice *device{nullptr};
  Context *graphics_context{nullptr};
  Context *transfer_context{nullptr};
  WorkReceipt *frame_receipts[max_frames_in_flight];
  u32 backbuffer_index;
  // TODO: Make configurable
  TextureHandle backbuffers[3];
  TextureHandle depth_texture;
  RenderPassHandle main_pass;
  BindingSetLayoutHandle bindless_set_layout;
  BindingSetHandle bindless_set;
  SamplerHandle default_sampler;
  u32 current_frame_in_flight;

  BufferHandle staging_buffer{};
  u32 staging_buffer_current_size;

  StringBuffer string_buffer{};
  Array<TextureHandle> bindless_textures_to_update{};
};

} // namespace Helix
