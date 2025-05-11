#include "Sandbox.hpp"
#include "Core/Application.hpp"
#include "Core/Engine.hpp"
#include "Platform/Process.hpp"
#include <tracy/Tracy.hpp>

#include "Renderer/Scene.hpp"

namespace Helix {

static bool sandbox_key_event(u16 code, void *sender, void *listener,
                              EventContext context) {
  u16 key_code = context.data.u16[0];
  if (key_code == SDL_SCANCODE_F) {
    Platform::instance()->toggle_fullscreen();
  }
  return true;
}

void Sandbox::init() {

  CameraConfiguration config{};
  config.position = {0.f, 0.f, 2.f};
  config.far_plane = 1'000.f;
  camera.init(config);

  scene.init();

  EventService::instance()->register_event(SDL_EVENT_KEY_DOWN, 0,
                                           sandbox_key_event);
  HINFO("Game Initialised");
}

void Sandbox::shutdown() {
  scene.shutdown();
  EventService::instance()->unregister_event(SDL_EVENT_KEY_DOWN, 0,
                                             sandbox_key_event);

  HINFO("Game Shutdown");
}

void Sandbox::update(RenderPacket *packet) {
  ZoneScopedN("Game::update");

  packet->camera = &camera;
  packet->game = this;

  camera.update(packet->delta_time);
  scene.update(packet);
}

void Sandbox::render_frame(f32 dt) {
  ZoneScoped;
  ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar |
                           ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove;
  ImGui::SetNextWindowBgAlpha(0.25f);
  ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSize(ImVec2(256, 96), ImGuiCond_FirstUseEver);
  if (ImGui::Begin("Frame time", NULL, flags)) {
    ImGui::Text("Frame time: %.3f ms",
                Application::instance()->get_delta_time() * 1000.f);
    ImGui::Checkbox("Limit Frames", &Application::instance()->limit_frames);
    ImGui::End();
  }

  if (!Platform::instance()->is_fullscreen) {

    static bool profiler_loaded = false;
    if (!profiler_loaded) {
      ImGui::SetNextWindowPos(ImVec2(94, 96), ImGuiCond_Always);
      ImGui::SetNextWindowSize(ImVec2(162, 40), ImGuiCond_Always);
      if (ImGui::Begin("Start Profiler", NULL, flags)) {
        if (ImGui::Button("Start Profiler")) {
          profiler_loaded = true;
          if (!launch_tracy_profiler()) {
            HERROR("Unable to start Tracy Profiler");
          }
        }
        ImGui::End();
      }
    }

    ImGui::SetNextWindowPos(ImVec2(0, 96), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(94, 40), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Load Model", NULL, flags)) {
      if (ImGui::Button("Load Model")) {
        char *file_path = nullptr;
        char *file_name = nullptr;
        if (FileService::open_file_dialog(
                &file_name, &file_path,
                &MemoryService::instance()->system_allocator)) {
          if (file_path && file_name) {
            string_replace(file_path, '\\', '/');
            scene.load_mesh(file_path, file_name);

            MemoryService::instance()->system_allocator.deallocate(file_name);
            MemoryService::instance()->system_allocator.deallocate(file_path);
            // model_loaded = true;
          }
        }
      }
      ImGui::End();
    }

    scene.node_hierarchy.imgui_draw_node_hierarchy();
    scene.node_hierarchy.imgui_draw_node_property();

    // ImGui::ShowDemoWindow(&show_demo);
  }
}
void Sandbox::resize(u32 width, u32 height) {}

} // namespace Helix
