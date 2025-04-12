#include "ImguiFrontend.hpp"
#include "Core/Memory.hpp"
#include "Renderer/Vulkan/VulkanImguiBackend.hpp"
#include <SDL3/SDL_events.h>
#include <imgui/backends/imgui_impl_sdl3.h>

namespace Helix {
ImguiBackend *ImguiCreateBackend(RendererBackendType type) {

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

  ImguiLayerConfiguration *config = (ImguiLayerConfiguration *)config_;
  backend = ImguiCreateBackend(config->type);
  if (!backend) {
    HCRITICAL("Unable to initialise ImGui Backend!");
    return;
  }

  backend->init(config_);

  s_imgui_service = this;
  HELIX_SERVICE_INIT_MSG(ImguiFrontend);
}

void ImguiFrontend::handle_events(void *event_) {

  SDL_Event *event = (SDL_Event *)event_;
  ImGui_ImplSDL3_ProcessEvent(event);
}

void ImguiFrontend::shutdown() {
  backend->shutdown();
  MemoryService::instance()->system_allocator.deallocate(backend);

  s_imgui_service = nullptr;
  HELIX_SERVICE_SHUTDOWN_MSG(ImguiFrontend);
}

} // namespace Helix
