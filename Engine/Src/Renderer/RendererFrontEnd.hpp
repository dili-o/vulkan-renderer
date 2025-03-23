#pragma once

#include "Containers/Array.hpp"
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

  bool begin_frame(RenderPacket *packet);

  bool end_frame(RenderPacket *packet);

  RendererBackend *backend{nullptr};

  Array<Vertex> vertices{};
};
} // namespace Helix
