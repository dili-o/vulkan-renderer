#include "Camera.hpp"
#include "Core/Event.hpp"
#include "Core/Input.hpp"
#include "Platform/Platform.hpp"
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_scancode.h>
#include <glm/gtx/hash.hpp>
#include <glm/gtx/quaternion.hpp>
#include <glm/gtx/transform.hpp>

namespace Helix {

static u16 s_left_button = SDL_SCANCODE_A;
static u16 s_right_button = SDL_SCANCODE_D;
static u16 s_forward_button = SDL_SCANCODE_W;
static u16 s_backward_button = SDL_SCANCODE_S;
static u16 s_up_button = SDL_SCANCODE_SPACE;
static u16 s_down_button = SDL_SCANCODE_LCTRL;

// Move these into Camera.cpp
static bool camera_move_event(u16 code, void *sender, void *listener,
                              EventContext context) {
  Camera *cam = (Camera *)listener;
  cam->on_key_event(code == SDL_EVENT_KEY_DOWN, context.data.u16[0]);
  return true;
}

static bool camera_mouse_event(u16 code, void *sender, void *listener,
                               EventContext context) {
  Camera *cam = (Camera *)listener;
  cam->on_mouse_event(context.data.i16[0], context.data.i16[1]);
  return true;
}

static bool camera_mouse_button_event(u16 code, void *sender, void *listener,
                                      EventContext context) {
  Camera *cam = (Camera *)listener;
  cam->on_mouse_button_event(code == SDL_EVENT_MOUSE_BUTTON_DOWN,
                             context.data.u16[0]);
  return true;
}

static bool camera_scroll_event(u16 code, void *sender, void *listener,
                                EventContext context) {
  Camera *cam = (Camera *)listener;
  cam->on_mouse_scroll_event(context.data.i8[0]);
  return true;
}

static bool camera_resize_event(u16 code, void *sender, void *listener,
                                EventContext context) {
  Camera *cam = (Camera *)listener;
  cam->on_window_resize(context.data.i32[0], context.data.i32[1]);
  return true;
}

void Camera::init(CameraConfiguration &config) {

  velocity = glm::vec3(0.f);
  pitch = 0.f;
  yaw = 0.f;
  is_active = false;

  position = config.position;
  near_plane = config.near_plane;
  far_plane = config.far_plane;
  fov = config.fov;
  aspect_ratio = config.aspect_ratio;

  EventService *event_service = EventService::instance();

  event_service->register_event(SDL_EVENT_KEY_DOWN, this, camera_move_event);
  event_service->register_event(SDL_EVENT_KEY_UP, this, camera_move_event);
  event_service->register_event(SDL_EVENT_MOUSE_MOTION, this,
                                camera_mouse_event);
  event_service->register_event(SDL_EVENT_MOUSE_BUTTON_DOWN, this,
                                camera_mouse_button_event);
  event_service->register_event(SDL_EVENT_MOUSE_BUTTON_UP, this,
                                camera_mouse_button_event);
  event_service->register_event(SDL_EVENT_MOUSE_WHEEL, this,
                                camera_scroll_event);
  event_service->register_event(SDL_EVENT_WINDOW_RESIZED, this,
                                camera_resize_event);
}

glm::mat4 Camera::get_rotation() {
  glm::quat pitch_rotation = glm::angleAxis(pitch, glm::vec3{1.f, 0.f, 0.f});
  glm::quat yaw_rotation = glm::angleAxis(yaw, glm::vec3{0.f, -1.f, 0.f});

  return glm::toMat4(yaw_rotation) * glm::toMat4(pitch_rotation);
}

glm::mat4 Camera::get_view() {
  glm::mat4 camera_translation = glm::translate(glm::mat4(1.f), position);
  glm::mat4 camera_rotation = get_rotation();
  return glm::inverse(camera_translation * camera_rotation);
}

glm::mat4 Camera::get_projection() {
  glm::mat4 projection =
      glm::perspective(glm::radians(fov), aspect_ratio, near_plane, far_plane);
  projection[1][1] *= -1.f;
  return projection;
}

void Camera::update(f32 delta_time) {
  glm::mat4 camera_rotation = get_rotation();
  position +=
      glm::vec3(camera_rotation * glm::vec4(velocity * move_speed, 1.f)) *
      delta_time;
}

void Camera::on_key_event(bool key_down, u16 key_code) {
  if (!is_active)
    return;

  if (key_down) {

    if (key_code == s_left_button) {
      velocity.x = -1;
    } else if (key_code == s_right_button) {
      velocity.x = 1;
    } else if (key_code == s_forward_button) {
      velocity.z = -1;
    } else if (key_code == s_backward_button) {
      velocity.z = 1;
    } else if (key_code == s_up_button) {
      velocity.y = 1;
    } else if (key_code == s_down_button) {
      velocity.y = -1;
    }
  } else {
    if (key_code == s_left_button) {
      velocity.x = 0;
    } else if (key_code == s_right_button) {
      velocity.x = 0;
    } else if (key_code == s_forward_button) {
      velocity.z = 0;
    } else if (key_code == s_backward_button) {
      velocity.z = 0;
    } else if (key_code == s_up_button) {
      velocity.y = 0;
    } else if (key_code == s_down_button) {
      velocity.y = 0;
    }
  }
}

void Camera::on_mouse_event(i16 x, i16 y) {
  if (!is_active)
    return;

  yaw += (f32)x / 300.f;
  pitch -= (f32)y / 300.f;
  pitch = glm::clamp(pitch, -89.0f, 89.0f);
}

void Camera::on_mouse_button_event(bool key_down, u16 key_code) {
  Platform *platform = Platform::instance();
  if (key_down) {
    if (key_code == BUTTON_RIGHT) {

      SDL_SetWindowRelativeMouseMode((SDL_Window *)platform->platform_handle,
                                     true);
      is_active = true;
    }
  } else {
    if (key_code == BUTTON_RIGHT) {

      SDL_SetWindowRelativeMouseMode((SDL_Window *)platform->platform_handle,
                                     false);
      is_active = false;
      velocity = glm::vec3(0.f);
    }
  }
}

void Camera::on_mouse_scroll_event(i8 direction) {
  if (InputService::instance()->is_key_down(SDL_SCANCODE_LSHIFT)) {
    move_speed = direction > 0 ? (move_speed + 0.5f) : (move_speed - 0.5f);

    move_speed = glm::clamp(move_speed, 0.5f, 10.f);
  } else {
    f32 scroll_factor = 2.f;
    fov += (direction * -scroll_factor);
    fov = glm::clamp(fov, scroll_factor, 45.f);
  }
}

void Camera::on_window_resize(i32 width, i32 height) {
  aspect_ratio = (f32)width / height;
}
} // namespace Helix
