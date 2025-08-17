#pragma once
#include "Core/Defines.hpp"
#include "Core/Service.hpp"

namespace Helix {
struct PlatformConfiguration {

  u32 width;
  u32 height;

  cstring name;
}; // struct WindowConfiguration

struct Platform : public Service {

  virtual void init(void *configuration) override;
  virtual void shutdown() override;

  HELIX_DECLARE_SERVICE(Platform)

  void handle_os_messages();
  f64 get_absolute_time_s();
  f64 get_absolute_time_ms();

  void get_mouse_position(f32 *mouseX, f32 *mouseY);
  void get_window_size(i32 *width, i32 *height);

  void sleep(u64 ms);

  void set_title(cstring title);

  bool toggle_fullscreen();

  static i32 get_logical_processor_count();
  static u64 get_current_processor_id();
  u64 get_current_thread_id();

  void *platform_handle = nullptr;
  bool requested_exit{true};
  bool is_suspended{false};
  bool is_fullscreen{false};
  i32 width{0};
  i32 height{0};
  cstring name{nullptr};
};
} // namespace Helix
