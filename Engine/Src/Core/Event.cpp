#include "Event.hpp"
#include "Containers/Array.hpp"

namespace Helix {
struct RegisteredEvent {
  void *listener{nullptr};
  PFN_on_event callback{nullptr};
};

struct EventCodeEntry {
  Array<RegisteredEvent> events;
};

#define MAX_MESSAGE_CODES 16384

struct EventSystemState {
  EventCodeEntry registered[MAX_MESSAGE_CODES];
};

static EventSystemState state;
static EventService *s_event_service{nullptr};

EventService *EventService::instance() { return s_event_service; }

void EventService::init(void *config) {
  if (s_event_service) {
    HELIX_SERVICE_RECREATE_MSG(EventService);
    return;
  }

  memset(&state, 0, sizeof(state));

  s_event_service = this;
  HELIX_SERVICE_INIT_MSG(EventService);
}

void EventService::shutdown() {
  for (u32 i = 0; i < MAX_MESSAGE_CODES; ++i) {
    if (state.registered[i].events.size != 0) {
      state.registered[i].events.shutdown();
    }
  }
  HELIX_SERVICE_SHUTDOWN_MSG(EventService);
}

bool EventService::register_event(u16 code, void *listener,
                                  PFN_on_event on_event) {
  if (!s_event_service) {
    return false;
  }

  Array<RegisteredEvent> &events_array = state.registered[code].events;

  if (events_array.size == 0) {
    events_array.init(&MemoryService::instance()->system_allocator, 1);
  }

  u32 registered_count = events_array.size;
  for (u32 i = 0; i < registered_count; ++i) {
    if (events_array[i].listener == listener) {
      HWARN("Attempting to register the same listener more than once");
      return false;
    }
  }

  RegisteredEvent event;
  event.listener = listener;
  event.callback = on_event;
  events_array.push(event);

  return true;
}

bool EventService::unregister_event(u16 code, void *listener,
                                    PFN_on_event on_event) {
  if (!s_event_service) {
    return false;
  }

  Array<RegisteredEvent> &events_array = state.registered[code].events;

  if (events_array.size == 0) {
    HWARN("Attempting to unregistered an event that isn't registered");
    return false;
  }

  u32 registered_count = events_array.size;
  for (u32 i = 0; i < registered_count; ++i) {
    RegisteredEvent &e = events_array[i];
    if (e.listener == listener && e.callback == on_event) {
      events_array.pop_at(i);
      return true;
    }
  }

  HWARN("Attempting to unregistered an event that isn't registered");
  return false;
}

bool EventService::fire_event(u16 code, void *sender, EventContext context) {
  if (!s_event_service) {
    return false;
  }

  Array<RegisteredEvent> &events_array = state.registered[code].events;

  if (events_array.size == 0) {
    // Nothing to registered
    return false;
  }

  u32 registered_count = events_array.size;
  for (u32 i = 0; i < registered_count; ++i) {
    RegisteredEvent &e = events_array[i];
    if (e.callback(code, sender, e.listener, context)) {
      // Event has been handled
      return true;
    }
  }

  return false;
}

} // namespace Helix
