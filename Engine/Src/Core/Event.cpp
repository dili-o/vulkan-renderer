#include "Event.hpp"
#include "Containers/Array.hpp"

namespace hlx {
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
static bool is_initialized{false};

void EventSys::init() {
  if (is_initialized) {
    HELIX_SERVICE_RECREATE_MSG(EventSys);
    return;
  }

  memset(&state, 0, sizeof(state));

  HELIX_SERVICE_INIT_MSG(EventSys);
  is_initialized = true;
}

void EventSys::shutdown() {
  if (!is_initialized) {
    return;
  }

  for (u32 i = 0; i < MAX_MESSAGE_CODES; ++i) {
    if (state.registered[i].events.size != 0) {
      state.registered[i].events.shutdown();
    }
  }
  HELIX_SERVICE_SHUTDOWN_MSG(EventSys);
}

bool EventSys::register_event(u16 code, void *listener,
                                  PFN_on_event on_event) {
  HASSERT(is_initialized);

  Array<RegisteredEvent> &events_array = state.registered[code].events;

  if (events_array.size == 0) {
    events_array.init(MemorySys::system_allocator(), 1);
  }

  u32 registered_count = events_array.size;
  for (u32 i = 0; i < registered_count; ++i) {
    if (events_array[i].listener == listener && listener != nullptr) {
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

bool EventSys::unregister_event(u16 code, void *listener,
                                    PFN_on_event on_event) {
  HASSERT(is_initialized);

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

bool EventSys::fire_event(u16 code, void *sender, EventContext context) {
  HASSERT(is_initialized);

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

} // namespace hlx
