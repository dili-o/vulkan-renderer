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

  HDEBUG("Attribute offset: {}", offsetof(Vertex, color));
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

  HeapAllocator *allocator = &MemoryService::instance()->system_allocator;
  //  vertices.init(allocator, 3);
  //  vertices.push({{0.0f, -0.5f}, {1.0f, 0.0f, 0.0f}});
  //  vertices.push({{0.5f, 0.5f}, {0.0f, 1.0f, 0.0f}});
  //  vertices.push({{-0.5f, 0.5f}, {0.0f, 0.0f, 1.0f}});
}

void RendererFrontEnd::shutdown() {
  backend->shutdown();
  hfree(backend, &MemoryService::instance()->system_allocator);
  HELIX_SERVICE_SHUTDOWN_MSG(RendererFrontEnd);
}

void RendererFrontEnd::on_resize(u16 width, u16 height) {
  backend->on_resize(width, height);
}

bool RendererFrontEnd::draw_frame(RenderPacket *packet) {

  if (begin_frame(packet)) {

    bool result = end_frame(packet);
    if (!result) {
      HCRITICAL("End frame failed!");
      return false;
    }
  }

  return true;
}

bool RendererFrontEnd::begin_frame(RenderPacket *packet) {
  return backend->begin_frame(packet);
}

bool RendererFrontEnd::end_frame(RenderPacket *packet) {
  return backend->end_frame(packet);
}
} // namespace Helix
