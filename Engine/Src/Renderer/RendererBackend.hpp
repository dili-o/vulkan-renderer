#pragma once
#include "RendererTypes.hpp"

namespace Helix {

struct Platform;

struct RendererBackend {
  virtual bool init(void *config) = 0;
  virtual bool shutdown() = 0;
  virtual bool on_resize(u16 width, u16 height) = 0;
  virtual bool begin_frame(RenderPacket *packet) = 0;
  virtual bool end_frame(RenderPacket *packet) = 0;

  Platform *platform{nullptr};
  u64 frame_number{0};
};

RendererBackend *RendererBackendCreate(RendererBackendType type);

} // namespace Helix
