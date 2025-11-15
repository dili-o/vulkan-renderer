#pragma once
#include "Core/Defines.hpp"
#include "Renderer/RendererTypes.hpp"

struct ImGuiContext;

namespace hlx {

struct RendererFrontEnd;
struct RendererBackend;

struct ImguiLayerConfiguration {
  RendererBackendType type;
  RendererFrontEnd *frontend = nullptr;
  u32 max_frame_in_flight = 1;
};

static uint32_t s_vb_size = 665536, s_ib_size = 665536;

struct HLX_API ImguiFrontend {
  void init(const ImguiLayerConfiguration &config);
  void shutdown();

  static ImguiFrontend *instance();

  ImGuiContext *get_ImGuiContext();

  bool platform_init(const ImguiLayerConfiguration &config);
  void platform_shutdown();

  bool handle_events(void *event);

  void begin_frame();

  void render_frame();

  TextureHandle font_texture;
  PipelineHandle pipeline;
  Array<BufferHandle> vertex_buffers;
  Array<BufferHandle> index_buffers;
};

} // namespace hlx
