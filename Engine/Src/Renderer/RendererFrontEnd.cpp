#include "Renderer/RendererFrontEnd.hpp"
#include "Core/Log.hpp"
#include "Core/Memory.hpp"
#include "RendererBackend.hpp"
#include "RendererTypes.hpp"

namespace Helix {

static RendererFrontEnd *s_renderer_frontend{nullptr};
RendererFrontEnd *RendererFrontEnd::instance() { return s_renderer_frontend; }

void RendererFrontEnd::init(void *_config) {
  if (s_renderer_frontend) {
    HELIX_SERVICE_RECREATE_MSG(RendererFrontEnd);
    return;
  }

  RendererConfig *config = (RendererConfig *)_config;
  backend = RendererBackendCreate(config->backend_type);

  if (!backend) {
    HCRITICAL("Unable to get backend!");
    return;
  }

  if (!backend->init(config)) {
    HCRITICAL("Failed to create backend!");
    return;
  }

  HELIX_SERVICE_INIT_MSG(RendererFrontEnd);
  s_renderer_frontend = this;
}

void RendererFrontEnd::shutdown() {
  backend->shutdown();
  hfree(backend, &MemoryService::instance()->system_allocator);
  HELIX_SERVICE_SHUTDOWN_MSG(RendererFrontEnd);
}

bool RendererFrontEnd::draw_frame(RenderPacket *packet) {

  if (begin_frame(packet->delta_time)) {

    bool result = end_frame(packet->delta_time);
    if (!result) {
      HCRITICAL("End frame failed!");
      return false;
    }
  }

  return true;
}

bool RendererFrontEnd::begin_frame(f32 delta_time) {
  return backend->begin_frame(delta_time);
}

bool RendererFrontEnd::end_frame(f32 delta_time) {
  return backend->end_frame(delta_time);
}
} // namespace Helix
