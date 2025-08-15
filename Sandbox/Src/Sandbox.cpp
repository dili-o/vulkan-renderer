#include "Sandbox.hpp"
#include "Core/Application.hpp"
#include "Core/Engine.hpp"
#include "Core/Log.hpp"
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
  config.position = {0.f, 0.f, 0.f};
  config.far_plane = 1'000.f;
  camera.init(config);

  scene.init();
  scene.load_mesh("D:/StratusGFX/Resources/", "SponzaCurtains.glb");

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

glm::vec3 get_frustum_corner(const glm::mat4 &inv_proj_view, float x, float y,
                             float z) {
  glm::vec4 ndc_point(x, y, z, 1.0f);
  glm::vec4 world_point = inv_proj_view * ndc_point;
  return glm::vec3(world_point) / world_point.w;
}

// The intersect_three_planes function adapted for glm::vec4 planes
glm::vec3 intersect_three_planes(const glm::vec4 &p1, const glm::vec4 &p2,
                                 const glm::vec4 &p3) {
  // Create matrix A from the normal vectors (first 3 components of each plane)
  glm::mat3 A(glm::vec3(p1.x, p1.y, p1.z), // First row: normal of plane 1
              glm::vec3(p2.x, p2.y, p2.z), // Second row: normal of plane 2
              glm::vec3(p3.x, p3.y, p3.z)  // Third row: normal of plane 3
  );

  // Create vector b from the distance components (negated w components)
  glm::vec3 b(-p1.w, -p2.w, -p3.w);

  // Check if planes are nearly parallel (determinant close to zero)
  float det = glm::determinant(A);
  if (abs(det) < 1e-6f) {
    // Planes are nearly parallel, return origin as fallback
    return glm::vec3(0.0f);
  }

  // Solve the system: A * x = b  =>  x = A^(-1) * b
  return glm::inverse(A) * b;
}

void Sandbox::update(RenderPacket *packet) {
  ZoneScopedN("Game::update");

  packet->camera = &camera;
  packet->game = this;

  camera.update(packet->delta_time);

  packet->freeze_camera = freeze_camera;

  if (!freeze_camera) {
    packet->inv_previous_view_proj =
        glm::inverse(camera.get_projection() * camera.get_view());
    packet->previous_view = camera.get_view();
    packet->previous_proj = camera.get_projection();
  }

  // glm::mat4 projection_transpose =
  //     glm::transpose(camera.get_projection() * camera.get_view());
  //
  // glm::vec4 left_plane = normalize_plane(
  //     projection_transpose[3] + projection_transpose[0]); // x + w  < 0;
  // glm::vec4 right_plane = normalize_plane(
  //     projection_transpose[3] - projection_transpose[0]); // x - w  < 0;
  // glm::vec4 top_plane = normalize_plane(projection_transpose[3] +
  //                                       projection_transpose[1]); // y + w  <
  //                                       0;
  // glm::vec4 bottom_plane = normalize_plane(
  //     projection_transpose[3] - projection_transpose[1]); // y - w  < 0;
  // glm::vec4 near_plane = normalize_plane(
  //     projection_transpose[3] + projection_transpose[2]); // z + w  < 0;
  // glm::vec4 far_plane = normalize_plane(projection_transpose[3] -
  //                                       projection_transpose[2]); // z - w  <
  //                                       0;
  //                                                                 //
  // glm::mat4 inv_proj_view =
  //     glm::inverse(camera.get_projection() * camera.get_view());
  // glm::vec3 near_left_top = get_frustum_corner(inv_proj_view, -1.f, -1.f,
  // -1.f); glm::vec3 near_left_bottom =
  //     get_frustum_corner(inv_proj_view, -1.f, 1.f, -1.f);
  // glm::vec3 near_right_top = get_frustum_corner(inv_proj_view, 1.f, -1.f,
  // -1.f); glm::vec3 near_right_bottom =
  //     get_frustum_corner(inv_proj_view, 1.f, 1.f, -1.f);
  // glm::vec3 far_left_top = get_frustum_corner(inv_proj_view, -1.f,
  // -1.f, 1.f); glm::vec3 far_left_bottom = get_frustum_corner(inv_proj_view,
  // -1.f, 1.f, 1.f); glm::vec3 far_right_top =
  // get_frustum_corner(inv_proj_view, 1.f, -1.f, 1.f); glm::vec3
  // far_right_bottom = get_frustum_corner(inv_proj_view, 1.f, 1.f, 1.f);

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
    ImGui::Checkbox("Freeze Camera", &freeze_camera);
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
