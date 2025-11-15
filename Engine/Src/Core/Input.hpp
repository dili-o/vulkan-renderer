#pragma once

#include "Defines.hpp"
#include <SDL3/SDL_scancode.h>

namespace hlx {
#pragma region KeyCodes
enum Buttons {
  BUTTON_PADDING,
  BUTTON_LEFT,
  BUTTON_MIDDLE,
  BUTTON_RIGHT,
  BUTTON_SIDE_1,
  BUTTON_SIDE_2,
  BUTTON_PADDING2,
  BUTTON_MAX_BUTTONS
};

#define DEFINE_KEY(name, code) KEY_##name = code

using Keys = SDL_Scancode;

#pragma endregion KeyCodes

struct HLX_API InputSys {
  static void init();
  static void shutdown();

  static void update(f32 dt);

  // Keyboard Input
  static bool is_key_up(Keys key);
  static bool is_key_down(Keys key);
  static bool was_key_up(Keys key);
  static bool was_key_down(Keys key);
  // Called by the Platform
  static void process_key(Keys key, bool pressed);

  // Mouse Input
  static bool is_mouse_button_up(Buttons button);
  static bool is_mouse_button_down(Buttons button);
  static bool was_mouse_button_up(Buttons button);
  static bool was_mouse_button_down(Buttons button);
  static void get_mouse_position(i32 *x, i32 *y);
  static void get_previous_frame_mouse_position(i32 *x, i32 *y);
  // Called by the Platform
  static void process_mouse_button(Buttons button, bool pressed);
  static void process_mouse_move(i16 x, i16 y);
  static void process_mouse_wheel(i8 z_delta);
};
} // namespace hlx
