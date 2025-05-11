#pragma once

#include "Core/Clock.hpp"
#include "Service.hpp"

namespace Helix {

// Forward declare
struct Platform;
struct Game;

struct Application : Service {
  virtual void init(void *config = nullptr) override;
  virtual void shutdown() override;

  f32 get_delta_time() { return delta_time; }

  HELIX_DECLARE_SERVICE(Application)

  void run();

  Platform *platform{nullptr};
  Game *game;
  Clock clock;
  f64 last_time{0.0};
  f32 delta_time{0.f};
  bool limit_frames{true};
};
} // namespace Helix
