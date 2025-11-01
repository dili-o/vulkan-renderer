#pragma once

#include "Defines.hpp"
#include "Service.hpp"

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_keyboard.h>
#include <SDL3/SDL_keycode.h>
#include <SDL3/SDL_scancode.h>

namespace hlx {
struct EventContext {
  // 128 bytes
  union {
    i64 i64[2];
    u64 u64[2];
    f64 f64[2];

    i32 i32[4];
    u32 u32[4];
    f32 f32[4];

    i16 i16[8];
    u16 u16[8];

    i8 i8[16];
    u8 u8[16];

    char c[16];
  } data;
};

// Should return true if handled.
typedef bool (*PFN_on_event)(u16 code, void *sender, void *listener_inst,
                             EventContext data);

struct HLX_API EventService : Service {
  virtual void init(void *config = nullptr) override;
  virtual void shutdown() override;

  HELIX_DECLARE_SERVICE(EventService)

  bool register_event(u16 code, void *listener, PFN_on_event on_event);

  bool unregister_event(u16 code, void *listener, PFN_on_event on_event);

  bool fire_event(u16 code, void *sender, EventContext context);
};
} // namespace hlx
