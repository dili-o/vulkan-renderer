#pragma once

#include "Core/Defines.hpp"
#include <glm/glm.hpp>

namespace Helix {

struct CameraConfiguration {
  glm::vec3 position{0.f};
  f32 near_plane{0.1f};
  f32 far_plane{100.f};
  f32 fov{45.f};
  f32 aspect_ratio{1280.f / 720.f};
};

struct Camera {
  bool is_active{false};
  glm::vec3 velocity{0.f};
  glm::vec3 position{0.f};

  f32 move_speed{1.f};

  f32 pitch{0.f};
  f32 yaw{0.f};

  f32 near_plane{0.1f};
  f32 far_plane{100.f};
  f32 fov{45.f};
  f32 aspect_ratio{1280.f / 720.f};

  void init(CameraConfiguration &camera_config);
  glm::mat4 get_rotation();
  glm::mat4 get_view();
  glm::mat4 get_projection();

  void update(f32 delta_time);

  void on_key_event(bool key_down, u16 key_code);

  void on_mouse_event(i16 x, i16 y);
  void on_mouse_button_event(bool key_down, u16 key_code);
  void on_mouse_scroll_event(i8 direction);
  void on_window_resize(i32 width, i32 height);
};
} // namespace Helix
