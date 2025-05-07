#include "Sandbox.hpp"
#include "Core/Engine.hpp"
#include "Core/String.hpp"
#include "Platform/File.hpp"
#include "glm/trigonometric.hpp"
#include <cmath>
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
  if (!Platform::instance()->is_fullscreen) {

    bool show_demo = true;
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar |
                             ImGuiWindowFlags_NoResize |
                             ImGuiWindowFlags_NoMove;
    ImGui::SetNextWindowBgAlpha(0.25f);
    ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(256, 96), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Frame time", &show_demo, flags)) {
      ImGui::Text("Frame time: %.3f ms",
                  Application::instance()->get_delta_time() * 1000.f);
      ImGui::Text("Camera Position: %.2f, %.2f, %.2f", camera.position.x,
                  camera.position.y, camera.position.z);
      ImGui::Text("Camera Pitch: %.3f",
                  fmod(glm::degrees(camera.pitch), 360.f));
      ImGui::Text("Camera Yaw: %.3f", fmod(glm::degrees(camera.yaw), 360.f));
      ImGui::End();
    }

    static bool model_loaded = false;
    if (!model_loaded) {
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
    }

    scene.node_hierarchy.imgui_draw_node_hierarchy();
    scene.node_hierarchy.imgui_draw_node_property();

    // ImGui::ShowDemoWindow(&show_demo);
  }
}
void Sandbox::resize(u32 width, u32 height) {}

} // namespace Helix
