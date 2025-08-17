#include "Platform.hpp"
#include "Renderer/ImguiFrontend.hpp"
#include "SDL3/SDL_video.h"

#if HELIX_PLATFORM_WINDOWS
#include "Core/Event.hpp"
#include "Core/Input.hpp"
#include "Core/Log.hpp"
#include "Core/Profiler.hpp"
#include "Renderer/RendererFrontEnd.hpp"
#include <SDL3/SDL.h>
#include <windows.h>

namespace Helix {

static SDL_Window *window{nullptr};

static Platform *s_platform_service{nullptr};

Platform *Platform::instance() { return s_platform_service; }

void Platform::init(void *configuration_) {

  if (s_platform_service) {
    HELIX_SERVICE_RECREATE_MSG(PlatformService);
    return;
  }

  // Init SDL
  if (!SDL_Init(SDL_INIT_VIDEO)) {
    HCRITICAL("SDL could not initialize! SDL error: {}", SDL_GetError());
  }

  // Create window
  PlatformConfiguration *config = (PlatformConfiguration *)configuration_;
  SDL_WindowFlags window_flags =
      (SDL_WindowFlags)(SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE |
                        SDL_WINDOW_HIGH_PIXEL_DENSITY);

  window = SDL_CreateWindow(config->name, config->width, config->height,
                            window_flags);
  if (!window) {
    HCRITICAL("SDL window could not be created! SDL error: {}", SDL_GetError());
  }
  HELIX_SERVICE_INIT_MSG(PlatformService);

  platform_handle = window;
  requested_exit = false;
  width = config->width;
  height = config->height;
  name = config->name;

  s_platform_service = this;
}

void Platform::handle_os_messages() {
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
      EventService::instance()->fire_event(SDL_EVENT_QUIT, 0, context);
    } break;
    case SDL_EVENT_WINDOW_RESIZED: {
      SDL_GetWindowSize(window, &width, &height);
      EventContext context{};
      context.data.i32[0] = width;
      context.data.i32[1] = height;
      EventService::instance()->fire_event(SDL_EVENT_WINDOW_RESIZED, 0,
                                           context);

      RendererFrontEnd::instance()->on_resize(width, height);
    } break;
    case SDL_EVENT_KEY_UP:
    case SDL_EVENT_KEY_DOWN: {
      Keys key = (Keys)e.key.scancode;
      bool pressed = e.type == SDL_EVENT_KEY_DOWN;
      InputService::instance()->process_key(key, pressed);
    } break;
    case SDL_EVENT_MOUSE_MOTION: {
      i32 x_pos = e.motion.xrel;
      i32 y_pos = e.motion.yrel;
      InputService::instance()->process_mouse_move(x_pos, y_pos);
    } break;
    case SDL_EVENT_MOUSE_WHEEL: {
      i32 z_delta = e.wheel.y;
      if (z_delta != 0) {
        z_delta = z_delta < 0 ? -1 : 1;
      }
      InputService::instance()->process_mouse_wheel(z_delta);
    } break;
    case SDL_EVENT_MOUSE_BUTTON_UP:
    case SDL_EVENT_MOUSE_BUTTON_DOWN: {
      Buttons button = (Buttons)e.button.button;
      bool pressed = e.type == SDL_EVENT_MOUSE_BUTTON_DOWN;
      InputService::instance()->process_mouse_button(button, pressed);
    } break;
    }
  }
}

f64 Platform::get_absolute_time_s() {
  return (f64)SDL_GetPerformanceCounter() / (f64)SDL_GetPerformanceFrequency();
}

f64 Platform::get_absolute_time_ms() { return get_absolute_time_s() * 1000.0; }

void Platform::get_mouse_position(f32 *mouseX, f32 *mouseY) {
  SDL_GetMouseState(mouseX, mouseY);
}

void Platform::get_window_size(i32 *width, i32 *height) {
  SDL_GetWindowSize(window, width, height);
}

u64 Platform::get_current_processor_id() {
  return (u64)GetCurrentProcessorNumber();
}

u64 Platform::get_current_thread_id() { return (u64)GetCurrentThreadId(); }

void Platform::sleep(u64 ms) { SDL_Delay(ms); }

void Platform::set_title(cstring title) { SDL_SetWindowTitle(window, title); }

bool Platform::toggle_fullscreen() {
  is_fullscreen = !is_fullscreen;
  return SDL_SetWindowFullscreen(window, is_fullscreen);
}

i32 Platform::get_logical_processor_count() {
  SYSTEM_INFO sysinfo;
  GetSystemInfo(&sysinfo);
  return sysinfo.dwNumberOfProcessors;
}

void Platform::shutdown() {
  SDL_DestroyWindow(window);
  window = nullptr;
  s_platform_service = nullptr;
  SDL_Quit();

  HELIX_SERVICE_SHUTDOWN_MSG(PlatformService);
}
} // namespace Helix
#endif
