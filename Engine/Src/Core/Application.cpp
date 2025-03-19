#include "Application.hpp"
#include "Core/Event.hpp"
#include "Core/Input.hpp"
#include "Core/Log.hpp"
#include "Game.hpp"
#include "Platform/Platform.hpp"
#include "Renderer/RendererFrontEnd.hpp"
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_keyboard.h>
#include <SDL3/SDL_keycode.h>
#include <SDL3/SDL_scancode.h>

namespace Helix {

bool application_on_event(u16 event_code, void *sender, void *listener,
                          EventContext context);

bool application_on_key(u16 event_code, void *sender, void *listener,
                        EventContext context);

static Application *s_application_service{nullptr};
Application *Application::instance() { return s_application_service; }

void Application::init(void *config) {
  if (s_application_service) {
    HELIX_SERVICE_RECREATE_MSG(ApplicationService);
    return;
  }

  platform = Platform::instance();
  if (!platform) {
    HCRITICAL("Failed to create a platform service!");
  }

  EventService *event_service = EventService::instance();
  event_service->register_event(SDL_EVENT_QUIT, 0, application_on_event);
  event_service->register_event(SDL_EVENT_KEY_DOWN, 0, application_on_key);
  event_service->register_event(SDL_EVENT_KEY_UP, 0, application_on_key);

  HELIX_SERVICE_INIT_MSG(ApplicationService);
  // Initialize the Game
  game = (Game *)config;
  game->init();
  game->resize(platform->width, platform->height);
}

void Application::run() {
  clock.start();
  clock.update();
  last_time = clock.elapsed_time;
  f64 running_time = 0.0;
  u8 frame_count = 0;
  f64 target_frame_seconds = 1.0 / 60.0;

  while (!platform->requested_exit) {
    platform->handle_os_messages();

    if (!platform->is_suspended) {
      clock.update();
      f64 current_time = clock.elapsed_time;
      f64 delta_time = current_time - last_time;
      f64 frame_start_time = platform->get_absolute_time();

      game->update(delta_time);

      game->render(delta_time);

      RenderPacket packet{(f32)delta_time};
      RendererFrontEnd::instance()->draw_frame(&packet);

      f64 frame_end_time = platform->get_absolute_time();
      f64 frame_elapsed_time = frame_end_time - frame_start_time;
      running_time += frame_elapsed_time;
      f64 remaining_seconds = target_frame_seconds - frame_elapsed_time;

      if (remaining_seconds > 0) {
        u64 remaining_ms = (remaining_seconds * 1000);

        // If there is time left, give it back to the OS.
        bool limit_frames = false;
        if (remaining_ms > 0 && limit_frames) {
          platform->sleep(remaining_ms - 1);
        }

        frame_count++;
      }
      // HDEBUG("Delta: {}", delta_time);

      InputService::instance()->update(delta_time);

      last_time = current_time;
    }
  }
}

void Application::shutdown() {

  EventService *event_service = EventService::instance();
  event_service->unregister_event(SDL_EVENT_QUIT, 0, application_on_event);
  event_service->unregister_event(SDL_EVENT_KEY_DOWN, 0, application_on_key);
  event_service->unregister_event(SDL_EVENT_KEY_UP, 0, application_on_key);
  s_application_service = nullptr;
  HELIX_SERVICE_SHUTDOWN_MSG(ApplicationService);
}

bool application_on_event(u16 event_code, void *sender, void *listener,
                          EventContext context) {
  switch (event_code) {
  case SDL_EVENT_QUIT: {
    Platform::instance()->requested_exit = true;
    return true;
  } break;
  }
  return false;
}

bool application_on_key(u16 event_code, void *sender, void *listener,
                        EventContext context) {
  switch (event_code) {
  case SDL_EVENT_KEY_DOWN: {
    u16 key_code = context.data.u16[0];
    if (key_code == SDL_SCANCODE_ESCAPE) {
      EventContext context{};
      EventService::instance()->fire_event(SDL_EVENT_QUIT, 0, context);

      return true;
    } else if (key_code == SDL_SCANCODE_A) {
      HWARN("Explicit A was pressed");
    } else {
      char k = (char)SDL_GetKeyFromScancode((SDL_Scancode)key_code,
                                            SDL_KMOD_NONE, false);
      HDEBUG("Key {} was pressed", k);
    }
  } break;
  case SDL_EVENT_KEY_UP: {
    u16 key_code = context.data.u16[0];
    if (key_code == SDL_SCANCODE_B) {
      HWARN("Explicit B was released");
    } else {
      char k = (char)SDL_GetKeyFromScancode((SDL_Scancode)key_code,
                                            SDL_KMOD_NONE, false);
      HDEBUG("Key {} was released", k);
    }
  } break;
  }
  return false;
}
} // namespace Helix
