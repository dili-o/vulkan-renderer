#include "Sandbox.hpp"
#include "Core/Engine.hpp"
#include "glm/trigonometric.hpp"
#include <cmath>

namespace Helix {

void Sandbox::init() {

  CameraConfiguration config{};
  config.position = {0.f, 0.f, 2.f};
  camera.init(config);

  RendererFrontEnd::instance()->load_model(ASSETS_PATH "/Models/HaloArmour/",
                                           ASSETS_PATH
                                           "/Models/HaloArmour/halo_armor.obj");
  // RendererFrontEnd::instance()->load_model(
  //     ASSETS_PATH "/Models/Sponza/", ASSETS_PATH
  //     "/Models/Sponza/sponza.obj");

  HINFO("Game Initialised");
}
void Sandbox::shutdown() { HINFO("Game Shutdown"); }

void Sandbox::update(f32 dt) {}

void Sandbox::render_frame(f32 dt) {
  bool show_demo = true;
  ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar |
                           ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove;
  ImGui::SetNextWindowBgAlpha(0.25f);
  ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSize(ImVec2(256, 96), ImGuiCond_FirstUseEver);
  if (ImGui::Begin("Frame time", &show_demo, flags)) {
    ImGui::Text("Frame time: %.3f ms",
                Application::instance()->get_delta_time() * 1000.f);
    ImGui::Text("Camera Position: %.2f, %.2f, %.2f", camera.position.x,
                camera.position.y, camera.position.z);
    ImGui::Text("Camera Pitch: %.3f", fmod(glm::degrees(camera.pitch), 360.f));
    ImGui::Text("Camera Yaw: %.3f", fmod(glm::degrees(camera.yaw), 360.f));
    ImGui::End();
  }

  // ImGui::ShowDemoWindow(&show_demo);
}
void Sandbox::resize(u32 width, u32 height) {}

} // namespace Helix
