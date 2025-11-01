#include "Input.hpp"
#include "Core/Event.hpp"
#include "Core/Log.hpp"
#include "Core/Profiler.hpp"
// Vendor
#include <SDL3/SDL_events.h>

namespace hlx {
struct KeyboardState {
  bool keys[256];
};

struct MouseState {
  i16 x{0};
  i16 y{0};
  u8 buttons[BUTTON_MAX_BUTTONS];
};

struct InputState {
  KeyboardState keyboard_current_state;
  KeyboardState keyboard_previous_state;
  MouseState mouse_current_state;
  MouseState mouse_previous_state;
};

static InputService *s_input_service{nullptr};
static InputState state{};

InputService *InputService::instance() { return s_input_service; }

void InputService::init(void *config) {
  if (s_input_service) {
    HELIX_SERVICE_RECREATE_MSG(InputService);
    return;
  }

  s_input_service = this;
  HELIX_SERVICE_INIT_MSG(InputService);
}

void InputService::shutdown() {
  s_input_service = nullptr;
  HELIX_SERVICE_SHUTDOWN_MSG(InputService);
}

void InputService::update(f32 dt) {
  HELIX_PROFILER_FUNCTION_COLOR(tracy::Color::Red);
  if (!s_input_service) {
    HERROR("Attempting to update uninitialised InputService");
    return;
  }

  // Copy current to previous
  memcpy(&state.keyboard_previous_state, &state.keyboard_current_state,
         sizeof(KeyboardState));
  memcpy(&state.mouse_previous_state, &state.mouse_current_state,
         sizeof(MouseState));
}

void InputService::process_key(Keys key, bool pressed) {

  // Only handle if state changed
  if (state.keyboard_current_state.keys[key] != pressed) {
    state.keyboard_current_state.keys[key] = pressed;

    EventContext context;
    context.data.u16[0] = key;
    EventService::instance()->fire_event(
        pressed ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP, 0, context);
  }
}

void InputService::process_mouse_button(Buttons button, bool pressed) {
  if (state.mouse_current_state.buttons[button] != pressed) {
    state.mouse_current_state.buttons[button] = pressed;

    EventContext context;
    context.data.u16[0] = button;
    EventService::instance()->fire_event(pressed ? SDL_EVENT_MOUSE_BUTTON_DOWN
                                                 : SDL_EVENT_MOUSE_BUTTON_UP,
                                         0, context);
  }
}

void InputService::process_mouse_move(i16 x, i16 y) {
  if (state.mouse_current_state.x != x || state.mouse_current_state.y != y) {

    // Update internal state.
    state.mouse_current_state.x = x;
    state.mouse_current_state.y = y;

    // Fire the event.
    EventContext context;
    context.data.i16[0] = x;
    context.data.i16[1] = y;
    EventService::instance()->fire_event(SDL_EVENT_MOUSE_MOTION, 0, context);
  }
}

void InputService::process_mouse_wheel(i8 z_delta) {
  EventContext context;
  context.data.i8[0] = z_delta;
  EventService::instance()->fire_event(SDL_EVENT_MOUSE_WHEEL, 0, context);
}

bool InputService::is_key_down(Keys key) {
  if (!s_input_service) {
    return false;
  }
  return state.keyboard_current_state.keys[key] == true;
}

bool InputService::is_key_up(Keys key) {
  if (!s_input_service) {
    return true;
  }
  return state.keyboard_current_state.keys[key] == false;
}

bool InputService::was_key_down(Keys key) {
  if (!s_input_service) {
    return false;
  }
  return state.keyboard_previous_state.keys[key] == true;
}

bool InputService::was_key_up(Keys key) {
  if (!s_input_service) {
    return true;
  }
  return state.keyboard_previous_state.keys[key] == false;
}

// mouse input
bool InputService::is_mouse_button_down(Buttons button) {
  if (!s_input_service) {
    return false;
  }
  return state.mouse_current_state.buttons[button] == true;
}

bool InputService::is_mouse_button_up(Buttons button) {
  if (!s_input_service) {
    return true;
  }
  return state.mouse_current_state.buttons[button] == false;
}

bool InputService::was_mouse_button_down(Buttons button) {
  if (!s_input_service) {
    return false;
  }
  return state.mouse_previous_state.buttons[button] == true;
}

bool InputService::was_mouse_button_up(Buttons button) {
  if (!s_input_service) {
    return true;
  }
  return state.mouse_previous_state.buttons[button] == false;
}

void InputService::get_mouse_position(i32 *x, i32 *y) {
  if (!s_input_service) {
    *x = 0;
    *y = 0;
    return;
  }
  *x = state.mouse_current_state.x;
  *y = state.mouse_current_state.y;
}

void InputService::get_previous_frame_mouse_position(i32 *x, i32 *y) {
  if (!s_input_service) {
    *x = 0;
    *y = 0;
    return;
  }
  *x = state.mouse_previous_state.x;
  *y = state.mouse_previous_state.y;
}
} // namespace hlx
