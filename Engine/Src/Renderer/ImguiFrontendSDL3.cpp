#include "ImguiFrontend.hpp"

#ifdef HELIX_PLATFORM_SDL3
#include "Core/Profiler.hpp"
#include "Renderer/RendererTypes.hpp"
// Vendor
#include <SDL3/SDL_events.h>
#include <imgui/backends/imgui_impl_sdl3.h>
#include <imgui/imgui_internal.h>

namespace Helix {

bool ImguiFrontend::platform_init(void *configuration) {
  // ImguiLayerConfiguration *config = (ImguiLayerConfiguration *)configuration;
  //
  // if (config->type == RendererBackendType::RENDERER_BACKEND_TYPE_VULKAN) {
  //   return ImGui_ImplSDL3_InitForVulkan((SDL_Window *)config->window_handle);
  // }
  // return false;
  return true;
}

bool ImguiFrontend::platform_shutdown(void *configuration) {
  // ImGui_ImplSDL3_Shutdown();
  return true;
}

bool ImguiFrontend::handle_events(void *event_) {
  // SDL_Event *event = (SDL_Event *)event_;
  // ImGui_ImplSDL3_ProcessEvent(event);
  // if (event->type == SDL_EVENT_QUIT || event->type ==
  // SDL_EVENT_WINDOW_RESIZED)
  //   return false;
  // return ImGui::GetCurrentContext()->NavWindow;
  return false;
}

void ImguiFrontend::begin_frame() {
  // HELIX_PROFILER_FUNCTION();
  // ImGui_ImplSDL3_NewFrame();
  // ImGui::NewFrame();
}
} // namespace Helix
#endif // HELIX_PLATFORM_SDL3
