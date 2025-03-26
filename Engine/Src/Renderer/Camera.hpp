#pragma once

#include "Core/Defines.hpp"
#include "Core/Event.hpp"
#include <glm/glm.hpp>

namespace Helix {

struct Camera {
  bool is_active{false};
  glm::vec3 velocity{0.f};
  glm::vec3 position{0.f};

  f32 move_speed{1.f};

  f32 pitch{0.f};
  f32 yaw{0.f};

  void init();
  glm::mat4 get_view();
  glm::mat4 get_rotation();

  void update(f32 delta_time);
  bool on_key_event(u16 event_code, void *sender, void *listener,
                    EventContext context);

  bool on_mouse_event(u16 event_code, void *sender, void *listener,
                      EventContext context);
  bool on_mouse_button_event(u16 event_code, void *sender, void *listener,
                             EventContext context);
  bool on_mouse_scroll_event(u16 event_code, void *sender, void *listener,
                             EventContext context);
};
} // namespace Helix
