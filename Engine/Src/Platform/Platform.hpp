#pragma once
#include "Core/Defines.hpp"

namespace hlx {
struct PlatformConfiguration {
  u32 width;
  u32 height;

  cstring name;
}; // struct WindowConfiguration

struct VkGpuDevice;

struct HLX_API Platform {

  static void init(const PlatformConfiguration& config);
  static void shutdown();

  static void* get_platform_handle();

  static bool is_suspended();

  static bool create_vulkan_surface(VkGpuDevice *device);
  static const char *const *get_vulkan_extension_names(u32 *count);

  static void handle_os_messages();
  static f64 get_absolute_time_s();
  static f64 get_absolute_time_ms();

  static void get_mouse_position(f32 *mouseX, f32 *mouseY);
  static void get_window_size(i32 *width, i32 *height);

  static void sleep(u64 ms);

  static void set_window_relative_mouse_mode(bool enabled);

  static void set_title(cstring title);

  static bool toggle_fullscreen();

  static i32 get_logical_processor_count();
  static u64 get_current_processor_id();
  static u64 get_current_thread_id();
};
} // namespace hlx
