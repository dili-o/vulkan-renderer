#include "Sandbox.hpp"
#include "Core/Application.hpp"
#include "Core/Engine.hpp"
#include "Core/Log.hpp"
#include "Platform/Process.hpp"
#include "Renderer/Scene.hpp"
// Vendor
#include <imgui/imgui.h>
#include <tracy/Tracy.hpp>

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

  scene.update(packet);
}

void Sandbox::render_frame(f32 dt) { ZoneScoped; }
void Sandbox::resize(u32 width, u32 height) {}

} // namespace Helix
