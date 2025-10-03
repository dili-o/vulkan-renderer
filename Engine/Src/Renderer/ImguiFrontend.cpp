#include "ImguiFrontend.hpp"
#include "Core/Memory.hpp"
#include "Core/Profiler.hpp"
#include "Renderer/Vulkan/VulkanImguiBackend.hpp"
// Vendor
#include <imgui_internal.h>

namespace Helix {
static ImguiBackend *imgui_create_backend(RendererBackendType type) {

  if (type == RENDERER_BACKEND_TYPE_VULKAN) {
    void *memory = halloca(sizeof(VulkanImguiBackend),
                           &MemoryService::instance()->system_allocator);
    // Create 'new' backend and return it.
    return new (memory) VulkanImguiBackend();
  }

  HCRITICAL("Unknown Backend Type");
  return nullptr;
}

static ImguiFrontend *s_imgui_service{nullptr};
ImguiFrontend *ImguiFrontend ::instance() { return s_imgui_service; }

void ImguiFrontend::init(void *config_) {

  if (s_imgui_service) {
    HELIX_SERVICE_RECREATE_MSG(ImguiFrontend);
    return;
  }

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGui::StyleColorsDark();

  ImguiLayerConfiguration *config = (ImguiLayerConfiguration *)config_;
  backend = imgui_create_backend(config->type);
  if (!backend) {
    HCRITICAL("Unable to initialise ImGui Backend!");
    return;
  }

  platform_init(config);

  backend->init(config_);

  s_imgui_service = this;
  HELIX_SERVICE_INIT_MSG(ImguiFrontend);
}

void ImguiFrontend::shutdown() {

  platform_shutdown(nullptr);

  ImGui::DestroyContext();
  backend->shutdown();
  MemoryService::instance()->system_allocator.deallocate(backend);

  s_imgui_service = nullptr;
  HELIX_SERVICE_SHUTDOWN_MSG(ImguiFrontend);
}

void ImguiFrontend::render_frame(RenderPacket *packet) {
  HELIX_PROFILER_FUNCTION();
  backend->render_frame(packet);
}

} // namespace Helix
