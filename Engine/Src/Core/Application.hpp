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

  HELIX_DECLARE_SERVICE(Application)

  void run();

  Platform *platform{nullptr};
  Game *game;
  Clock clock;
  f64 last_time{0.0};
};
} // namespace Helix
