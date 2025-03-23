#include "Camera.hpp"
#include "Core/Input.hpp"
#include "Core/Log.hpp"
#include "Platform/Platform.hpp"
#include "SDL3/SDL_events.h"
#include "SDL3/SDL_scancode.h"
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/quaternion.hpp>
#include <glm/gtx/transform.hpp>

namespace Helix {
void Camera::init() {

  velocity = glm::vec3(0.f);
  position = glm::vec3(0.f, 0.f, 2.f);

  pitch = 0.f;
  yaw = 0.f;

  is_active = false;
}

glm::mat4 Camera::get_view() {
  glm::mat4 camera_translation = glm::translate(glm::mat4(1.f), position);
  glm::mat4 camera_rotation = get_rotation();
  return glm::inverse(camera_translation * camera_rotation);
}

glm::mat4 Camera::get_rotation() {
  glm::quat pitch_rotation = glm::angleAxis(pitch, glm::vec3{1.f, 0.f, 0.f});
  glm::quat yaw_rotation = glm::angleAxis(yaw, glm::vec3{0.f, -1.f, 0.f});

  return glm::toMat4(yaw_rotation) * glm::toMat4(pitch_rotation);
}

void Camera::update(f32 delta_time) {
  glm::mat4 camera_rotation = get_rotation();
  position +=
      glm::vec3(camera_rotation * glm::vec4(velocity * 0.5f, 0.f)) * delta_time;
}

bool Camera::on_key_event(u16 event_code, void *sender, void *listener,
                          EventContext context) {

  if (!is_active)
    return false;

  switch (event_code) {
  case SDL_EVENT_KEY_DOWN: {
    u16 key_code = context.data.u16[0];
    if (key_code == SDL_SCANCODE_A) {
      velocity.x = -1;
    } else if (key_code == SDL_SCANCODE_D) {
      velocity.x = 1;
    } else if (key_code == SDL_SCANCODE_W) {
      velocity.z = -1;
    } else if (key_code == SDL_SCANCODE_S) {
      velocity.z = 1;
    } else if (key_code == SDL_SCANCODE_SPACE) {
      velocity.y = 1;
    } else if (key_code == SDL_SCANCODE_LCTRL) {
      velocity.y = -1;
    }
  } break;
  case SDL_EVENT_KEY_UP: {
    u16 key_code = context.data.u16[0];
    if (key_code == SDL_SCANCODE_A) {
      velocity.x = 0;
    } else if (key_code == SDL_SCANCODE_D) {
      velocity.x = 0;
    } else if (key_code == SDL_SCANCODE_W) {
      velocity.z = 0;
    } else if (key_code == SDL_SCANCODE_S) {
      velocity.z = 0;
    } else if (key_code == SDL_SCANCODE_SPACE) {
      velocity.y = 0;
    } else if (key_code == SDL_SCANCODE_LCTRL) {
      velocity.y = 0;
    }
  } break;
  }
  return false;
}

bool Camera::on_mouse_event(u16 event_code, void *sender, void *listener,
                            EventContext context) {
  if (!is_active)
    return false;

  if (event_code == SDL_EVENT_MOUSE_MOTION) {
    i16 x = context.data.u16[0];
    i16 y = context.data.u16[1];
    yaw += (f32)x / 300.f;
    pitch -= (f32)y / 300.f;
    pitch = glm::clamp(pitch, -89.0f, 89.0f);
  }
  return false;
}

bool Camera::on_mouse_button_event(u16 event_code, void *sender, void *listener,
                                   EventContext context) {
  Platform *platform = Platform::instance();
  if (event_code == SDL_EVENT_MOUSE_BUTTON_DOWN) {
    if (context.data.u16[0] == BUTTON_RIGHT) {

      SDL_SetWindowRelativeMouseMode((SDL_Window *)platform->platform_handle,
                                     true);
      is_active = true;
    }
  } else if (event_code == SDL_EVENT_MOUSE_BUTTON_UP) {
    if (context.data.u16[0] == BUTTON_RIGHT) {

      SDL_SetWindowRelativeMouseMode((SDL_Window *)platform->platform_handle,
                                     false);
      is_active = false;
      velocity = glm::vec3(0.f);
    }
  }
  return false;
}

} // namespace Helix
