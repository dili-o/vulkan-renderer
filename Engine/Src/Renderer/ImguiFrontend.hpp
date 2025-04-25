#pragma once
#include "Core/Defines.hpp"
#include "Renderer/RendererTypes.hpp"

#include <../../Vendor/imgui/imgui.h>
// #include <imgui.h>

namespace Helix {

struct RendererFrontEnd;
struct RendererBackend;

struct ImguiBackend {
  virtual void init(void *configuration) = 0;
  virtual void shutdown() = 0;

  virtual void begin_frame() = 0;
  virtual void render_frame(RenderPacket *packet) = 0;

  RendererFrontEnd *frontend = nullptr;
};

struct ImguiLayerConfiguration {
  void *window_handle = nullptr;
  RendererBackendType type;
  RendererFrontEnd *frontend = nullptr;
};

struct ImguiFrontend : public Service {
  virtual void init(void *configuration) override;
  virtual void shutdown() override;

  void handle_events(void *event);

  void begin_frame();

  void render_frame(RenderPacket *packet);

  HELIX_DECLARE_SERVICE(ImguiFrontend);

private:
  ImguiBackend *backend = nullptr;
};

ImguiBackend *ImguiCreateBackend(RendererBackendType type);

} // namespace Helix
