#pragma once

#include "Defines.hpp"
#include "Service.hpp"
#include <SDL3/SDL_scancode.h>

namespace Helix {
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

struct HLX_API InputService : public Service {
  virtual void init(void *config = nullptr) override;
  virtual void shutdown() override;
  HELIX_DECLARE_SERVICE(InputService)

  void update(f32 dt);

  // Keyboard Input
  bool is_key_up(Keys key);
  bool is_key_down(Keys key);
  bool was_key_up(Keys key);
  bool was_key_down(Keys key);
  // Called by the Platform
  void process_key(Keys key, bool pressed);

  // Mouse Input
  bool is_mouse_button_up(Buttons button);
  bool is_mouse_button_down(Buttons button);
  bool was_mouse_button_up(Buttons button);
  bool was_mouse_button_down(Buttons button);
  void get_mouse_position(i32 *x, i32 *y);
  void get_previous_frame_mouse_position(i32 *x, i32 *y);
  // Called by the Platform
  void process_mouse_button(Buttons button, bool pressed);
  void process_mouse_move(i16 x, i16 y);
  void process_mouse_wheel(i8 z_delta);
};
} // namespace Helix
