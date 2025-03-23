#include "Sandbox.hpp"
#include "Core/Event.hpp"
#include "Core/Input.hpp"
#include "SDL3/SDL_events.h"

namespace Helix {

static bool camera_move_event(u16 code, void *sender, void *listener,
                              EventContext context) {
  Camera *cam = (Camera *)listener;
  return cam->on_key_event(code, sender, listener, context);
}

static bool camera_mouse_event(u16 code, void *sender, void *listener,
                               EventContext context) {
  Camera *cam = (Camera *)listener;
  return cam->on_mouse_event(code, sender, listener, context);
}

static bool camera_mouse_button_event(u16 code, void *sender, void *listener,
                                      EventContext context) {
  Camera *cam = (Camera *)listener;
  return cam->on_mouse_button_event(code, sender, listener, context);
}

void Sandbox::init() {

  camera.init();
  EventService *event_service = EventService::instance();

  event_service->register_event(SDL_EVENT_KEY_DOWN, &camera, camera_move_event);
  event_service->register_event(SDL_EVENT_KEY_UP, &camera, camera_move_event);
  event_service->register_event(SDL_EVENT_MOUSE_MOTION, &camera,
                                camera_mouse_event);
  event_service->register_event(SDL_EVENT_MOUSE_BUTTON_DOWN, &camera,
                                camera_mouse_button_event);
  event_service->register_event(SDL_EVENT_MOUSE_BUTTON_UP, &camera,
                                camera_mouse_button_event);

  HINFO("Game Initialised");
}
void Sandbox::shutdown() { HINFO("Game Shutdown"); }

void Sandbox::update(f32 dt) {
  if (InputService::instance()->is_key_down(SDL_SCANCODE_C)) {
    HDEBUG("c held");
  }
}
void Sandbox::render(f32 dt) {}
void Sandbox::resize(u32 width, u32 height) {}

} // namespace Helix
