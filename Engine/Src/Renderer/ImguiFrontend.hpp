#pragma once
#include "Core/Defines.hpp"
#include "Renderer/RendererTypes.hpp"

struct ImGuiContext;

namespace hlx {

struct RendererFrontEnd;
struct RendererBackend;

struct ImguiLayerConfiguration {
  void *window_handle = nullptr;
  RendererBackendType type;
  RendererFrontEnd *frontend = nullptr;
  u32 max_frame_in_flight = 1;
};

static uint32_t s_vb_size = 665536, s_ib_size = 665536;

struct HLX_API ImguiFrontend : public Service {
  virtual void init(void *configuration) override;
  virtual void shutdown() override;

  ImGuiContext *get_ImGuiContext();

  bool platform_init(void *configuration);
  void platform_shutdown(void *configuration);

  bool handle_events(void *event);

  void begin_frame();

  void render_frame();

  TextureHandle font_texture;
  PipelineHandle pipeline;
  Array<BufferHandle> vertex_buffers;
  Array<BufferHandle> index_buffers;
  HELIX_DECLARE_SERVICE(ImguiFrontend);
};

} // namespace hlx
