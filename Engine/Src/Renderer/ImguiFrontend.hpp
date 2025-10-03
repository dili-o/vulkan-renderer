#pragma once
#include "Core/Defines.hpp"
#include "Renderer/RendererTypes.hpp"

namespace Helix {

struct RendererFrontEnd;
struct RendererBackend;

struct ImguiBackend {
  virtual void init(void *configuration) = 0;
  virtual void shutdown() = 0;

  virtual void render_frame(RenderPacket *packet) = 0;
};

struct ImguiLayerConfiguration {
  void *window_handle = nullptr;
  RendererBackendType type;
  RendererFrontEnd *frontend = nullptr;
  u32 max_frame_in_flight = 1;
};

static uint32_t s_vb_size = 665536, s_ib_size = 665536;

struct ImguiFrontend : public Service {
  virtual void init(void *configuration) override;
  virtual void shutdown() override;

  bool platform_init(void *configuration);
  bool platform_shutdown(void *configuration);

  bool handle_events(void *event);

  void begin_frame();

  void render_frame(RenderPacket *packet);

  HELIX_DECLARE_SERVICE(ImguiFrontend);

private:
  ImguiBackend *backend = nullptr;
};

} // namespace Helix
