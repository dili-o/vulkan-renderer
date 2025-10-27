#include "ImguiFrontend.hpp"
#include "Core/Memory.hpp"
#include "Core/Profiler.hpp"
// Vendor
#include <imgui_internal.h>

namespace Helix {

static ImguiFrontend *s_imgui_service{nullptr};
ImguiFrontend *ImguiFrontend ::instance() { return s_imgui_service; }

void ImguiFrontend::init(void *config_) {

  s_imgui_service = this;
  HELIX_SERVICE_INIT_MSG(ImguiFrontend);
}

void ImguiFrontend::shutdown() {

  s_imgui_service = nullptr;
  HELIX_SERVICE_SHUTDOWN_MSG(ImguiFrontend);
}

void ImguiFrontend::render_frame(RenderPacket *packet) {
  HELIX_PROFILER_FUNCTION();
}

} // namespace Helix
