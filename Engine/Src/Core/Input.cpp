#include "Input.hpp"
#include "Core/Assert.hpp"
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

static bool is_initialized{false};
static InputState state{};

void InputSys::init() {
  if (is_initialized) {
    HELIX_SERVICE_RECREATE_MSG(InputSys);
    return;
  }

  HELIX_SERVICE_INIT_MSG(InputSys);
  is_initialized = true;
}

void InputSys::shutdown() {
  HELIX_SERVICE_SHUTDOWN_MSG(InputSys);
  is_initialized = false;
}

void InputSys::update(f32 dt) {
  HASSERT(is_initialized);
  HELIX_PROFILER_FUNCTION_COLOR(tracy::Color::Red);

  // Copy current to previous
  memcpy(&state.keyboard_previous_state, &state.keyboard_current_state,
         sizeof(KeyboardState));
  memcpy(&state.mouse_previous_state, &state.mouse_current_state,
         sizeof(MouseState));
}

void InputSys::process_key(Keys key, bool pressed) {
  HASSERT(is_initialized);
  // Only handle if state changed
  if (state.keyboard_current_state.keys[key] != pressed) {
    state.keyboard_current_state.keys[key] = pressed;

    EventContext context;
    context.data.u16[0] = key;
    EventSys::fire_event(
        pressed ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP, 0, context);
  }
}

void InputSys::process_mouse_button(Buttons button, bool pressed) {
  HASSERT(is_initialized);
  if (state.mouse_current_state.buttons[button] != pressed) {
    state.mouse_current_state.buttons[button] = pressed;

    EventContext context;
    context.data.u16[0] = button;
    EventSys::fire_event(pressed ? SDL_EVENT_MOUSE_BUTTON_DOWN
                                                 : SDL_EVENT_MOUSE_BUTTON_UP,
                                         0, context);
  }
}

void InputSys::process_mouse_move(i16 x, i16 y) {
  HASSERT(is_initialized);
  if (state.mouse_current_state.x != x || state.mouse_current_state.y != y) {

    // Update internal state.
    state.mouse_current_state.x = x;
    state.mouse_current_state.y = y;

    // Fire the event.
    EventContext context;
    context.data.i16[0] = x;
    context.data.i16[1] = y;
    EventSys::fire_event(SDL_EVENT_MOUSE_MOTION, 0, context);
  }
}

void InputSys::process_mouse_wheel(i8 z_delta) {
  HASSERT(is_initialized);
  EventContext context;
  context.data.i8[0] = z_delta;
  EventSys::fire_event(SDL_EVENT_MOUSE_WHEEL, 0, context);
}

bool InputSys::is_key_down(Keys key) {
  HASSERT(is_initialized);
  return state.keyboard_current_state.keys[key] == true;
}

bool InputSys::is_key_up(Keys key) {
  HASSERT(is_initialized);
  return state.keyboard_current_state.keys[key] == false;
}

bool InputSys::was_key_down(Keys key) {
  HASSERT(is_initialized);
  return state.keyboard_previous_state.keys[key] == true;
}

bool InputSys::was_key_up(Keys key) {
  HASSERT(is_initialized);
  return state.keyboard_previous_state.keys[key] == false;
}

// mouse input
bool InputSys::is_mouse_button_down(Buttons button) {
  HASSERT(is_initialized);
  return state.mouse_current_state.buttons[button] == true;
}

bool InputSys::is_mouse_button_up(Buttons button) {
  HASSERT(is_initialized);
  return state.mouse_current_state.buttons[button] == false;
}

bool InputSys::was_mouse_button_down(Buttons button) {
  HASSERT(is_initialized);
  return state.mouse_previous_state.buttons[button] == true;
}

bool InputSys::was_mouse_button_up(Buttons button) {
  HASSERT(is_initialized);
  return state.mouse_previous_state.buttons[button] == false;
}

void InputSys::get_mouse_position(i32 *x, i32 *y) {
  HASSERT(is_initialized);
  *x = state.mouse_current_state.x;
  *y = state.mouse_current_state.y;
}

void InputSys::get_previous_frame_mouse_position(i32 *x, i32 *y) {
  HASSERT(is_initialized);
  *x = state.mouse_previous_state.x;
  *y = state.mouse_previous_state.y;
}
} // namespace hlx
