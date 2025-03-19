#include "Platform.hpp"

#if HELIX_PLATFORM_WINDOWS
#include "Core/Event.hpp"
#include "Core/Input.hpp"
#include "Core/Log.hpp"
#include <SDL3/SDL.h>

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
  SDL_Event e;
  SDL_zero(e);

  while (SDL_PollEvent(&e)) {
    switch (e.type) {
    case SDL_EVENT_QUIT: {
      EventContext context{};
      EventService::instance()->fire_event(SDL_EVENT_QUIT, 0, context);
    } break;
    case SDL_EVENT_KEY_UP:
    case SDL_EVENT_KEY_DOWN: {
      Keys key = (Keys)e.key.scancode;
      bool pressed = e.type == SDL_EVENT_KEY_DOWN;
      InputService::instance()->process_key(key, pressed);
    } break;
    case SDL_EVENT_MOUSE_MOTION: {
      i32 x_pos = e.motion.x;
      i32 y_pos = e.motion.y;
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

f64 Platform::get_absolute_time() {
  return (f64)SDL_GetPerformanceCounter() / (f64)SDL_GetPerformanceFrequency();
}

void Platform::sleep(u64 ms) { SDL_Delay(ms); }

void Platform::shutdown() {
  SDL_DestroyWindow(window);
  window = nullptr;
  s_platform_service = nullptr;
  SDL_Quit();

  HELIX_SERVICE_SHUTDOWN_MSG(PlatformService);
}
} // namespace Helix
#endif
