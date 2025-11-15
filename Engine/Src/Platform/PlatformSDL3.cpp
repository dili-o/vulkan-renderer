#include "Core/Assert.hpp"
#include "Platform.hpp"

#ifdef HELIX_PLATFORM_SDL3
#include "Core/Event.hpp"
#include "Core/Input.hpp"
#include "Core/Log.hpp"
#include "Core/Profiler.hpp"
#include "Renderer/ImguiFrontend.hpp"
#include "Renderer/RendererFrontEnd.hpp"
#include "Renderer/Vulkan/VkGpuDevice.hpp"
// Vendor
#include <SDL3/SDL.h>
#include <SDL3/SDL_cpuinfo.h>
#include <SDL3/SDL_video.h>
#include <SDL3/SDL_vulkan.h>

namespace hlx {

struct PlatformState {
  bool is_suspended{false};
  bool is_fullscreen{false};
  i32 width{0};
  i32 height{0};
  cstring name{nullptr};
};
static PlatformState platform_state{};
static SDL_Window *window{nullptr};
static bool is_initialized{false};

void Platform::init(const PlatformConfiguration &config) {
  if (is_initialized) {
    HELIX_SERVICE_RECREATE_MSG(Platform);
    return;
  }

  // Init SDL
  if (!SDL_Init(SDL_INIT_VIDEO)) {
    HCRITICAL("SDL could not initialize! SDL error: {}", SDL_GetError());
  }

  // Create window
  SDL_WindowFlags window_flags =
      (SDL_WindowFlags)(SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE |
                        SDL_WINDOW_HIGH_PIXEL_DENSITY);

  window =
      SDL_CreateWindow(config.name, config.width, config.height, window_flags);
  if (!window) {
    HCRITICAL("SDL window could not be created! SDL error: {}", SDL_GetError());
  }
  HELIX_SERVICE_INIT_MSG(Platform);

  platform_state.width = config.width;
  platform_state.height = config.height;
  platform_state.name = config.name;

  is_initialized = true;
}

void *Platform::get_platform_handle() { return window; }

bool Platform::is_suspended() { return platform_state.is_suspended; }

void Platform::handle_os_messages() {
  HASSERT(is_initialized);
  HELIX_PROFILER_FUNCTION();
  SDL_Event e;
  SDL_zero(e);

  while (SDL_PollEvent(&e)) {
    // Don't fire events if they should only be handled by the UI
    if (ImguiFrontend::instance()->handle_events(&e))
      continue;

    switch (e.type) {
    case SDL_EVENT_QUIT: {
      EventContext context{};
      EventSys::fire_event(SDL_EVENT_QUIT, 0, context);
    } break;
    case SDL_EVENT_WINDOW_ENTER_FULLSCREEN:
    case SDL_EVENT_WINDOW_LEAVE_FULLSCREEN:
    case SDL_EVENT_WINDOW_RESIZED: {
      HINFO("Fullscreen");
      SDL_GetWindowSize(window, &platform_state.width, &platform_state.height);
      EventContext context{};
      context.data.i32[0] = platform_state.width;
      context.data.i32[1] = platform_state.height;
      RendererFrontEnd::instance()->on_resize(platform_state.width,
                                              platform_state.height);
      EventSys::fire_event(SDL_EVENT_WINDOW_RESIZED, 0, context);
    } break;
    case SDL_EVENT_KEY_UP:
    case SDL_EVENT_KEY_DOWN: {
      Keys key = (Keys)e.key.scancode;
      bool pressed = e.type == SDL_EVENT_KEY_DOWN;
      InputSys::process_key(key, pressed);
    } break;
    case SDL_EVENT_MOUSE_MOTION: {
      i32 x_pos = e.motion.xrel;
      i32 y_pos = e.motion.yrel;
      InputSys::process_mouse_move(x_pos, y_pos);
    } break;
    case SDL_EVENT_MOUSE_WHEEL: {
      i32 z_delta = e.wheel.y;
      if (z_delta != 0) {
        z_delta = z_delta < 0 ? -1 : 1;
      }
      InputSys::process_mouse_wheel(z_delta);
    } break;
    case SDL_EVENT_MOUSE_BUTTON_UP:
    case SDL_EVENT_MOUSE_BUTTON_DOWN: {
      Buttons button = (Buttons)e.button.button;
      bool pressed = e.type == SDL_EVENT_MOUSE_BUTTON_DOWN;
      InputSys::process_mouse_button(button, pressed);
    } break;
    }
  }
}

void Platform::set_window_relative_mouse_mode(bool enabled) {
  SDL_SetWindowRelativeMouseMode(window, enabled);
}

f64 Platform::get_absolute_time_s() {
  return (f64)SDL_GetPerformanceCounter() / (f64)SDL_GetPerformanceFrequency();
}

f64 Platform::get_absolute_time_ms() { return get_absolute_time_s() * 1000.0; }

void Platform::get_mouse_position(f32 *mouseX, f32 *mouseY) {
  SDL_GetMouseState(mouseX, mouseY);
}

void Platform::get_window_size(i32 *width_, i32 *height_) {
  *width_ = platform_state.width;
  *height_ = platform_state.height;
}

void Platform::sleep(u64 ms) { SDL_Delay(ms); }

void Platform::set_title(cstring title) { SDL_SetWindowTitle(window, title); }

bool Platform::toggle_fullscreen() {
  platform_state.is_fullscreen = !platform_state.is_fullscreen;
  return SDL_SetWindowFullscreen(window, platform_state.is_fullscreen);
}

i32 Platform::get_logical_processor_count() {
  return SDL_GetNumLogicalCPUCores();
}

void Platform::shutdown() {
  if (!is_initialized) {
    return;
  }

  SDL_DestroyWindow(window);
  window = nullptr;
  is_initialized = false;
  SDL_Quit();

  HELIX_SERVICE_SHUTDOWN_MSG(Platform);
}

bool Platform::create_vulkan_surface(VkGpuDevice *device) {
  return SDL_Vulkan_CreateSurface(window, device->vk_instance,
                                  device->vk_allocation_callbacks,
                                  &device->vk_surface);
}

const char *const *Platform::get_vulkan_extension_names(u32 *count) {
  return SDL_Vulkan_GetInstanceExtensions(count);
}

} // namespace hlx
#endif
