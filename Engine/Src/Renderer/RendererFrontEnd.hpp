#pragma once

#include "Core/Service.hpp"
#include "RendererTypes.hpp"

namespace Helix {

struct RendererBackend;

struct RendererFrontEnd : public Service {
  virtual void init(void *config) override;
  virtual void shutdown() override;

  HELIX_DECLARE_SERVICE(RendererFrontEnd);

  void on_resize(u16 width, u16 height);

  bool draw_frame(RenderPacket *packet);

  bool begin_frame(f32 delta_time);

  bool end_frame(f32 delta_time);

  RendererBackend *backend{nullptr};
};
} // namespace Helix
