#include "Application.hpp"
#include "Core/Event.hpp"
#include "Core/Input.hpp"
#include "Core/Job.hpp"
#include "Core/Log.hpp"
#include "Core/Profiler.hpp"
#include "Game.hpp"
#include "Platform/Platform.hpp"
#include "Renderer/RendererFrontEnd.hpp"

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

  s_application_service = this;
}

void Application::run() {
  clock.start();
  last_time = clock.get_elapsed_time_s();
  f64 target_frame_seconds_ms = 1000.0 / 60.0;

  while (!platform->requested_exit) {
    platform->handle_os_messages();

    if (!platform->is_suspended) {
      f64 current_time = clock.get_elapsed_time_s();
      delta_time = current_time - last_time;
      f64 frame_start_time_ms = platform->get_absolute_time_ms();

      InputService::instance()->update(delta_time);

      JobService::instance()->update();

      RenderPacket packet{(f32)delta_time};

      game->update(&packet);

      RendererFrontEnd::instance()->render_frame(&packet);

      HELIX_PROFILER_ZONE("Calculate remaining time and update frame count",
                          HELIX_PROFILER_COLOR_DEFAULT)
      f64 frame_end_time_ms = platform->get_absolute_time_ms();
      f64 frame_elapsed_time_ms = frame_end_time_ms - frame_start_time_ms;
      f64 remaining_time_ms = target_frame_seconds_ms - frame_elapsed_time_ms;

      if (remaining_time_ms > 0 && limit_frames) {
        // If there is time left, give it back to the OS.
        HELIX_PROFILER_ZONE("Application sleep", HELIX_PROFILER_COLOR_DEFAULT)
        platform->sleep(static_cast<u32>(remaining_time_ms - 1));
        HELIX_PROFILER_ZONE_END()
      }
      last_time = current_time;
      HELIX_PROFILER_ZONE_END()
    }
    HELIX_PROFILER_FRAME("Frame");
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
