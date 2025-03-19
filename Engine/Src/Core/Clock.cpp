#include "Clock.hpp"
#include "Platform/Platform.hpp"

namespace Helix {
void Clock::start() {
  start_time = Platform::instance()->get_absolute_time();
  elapsed_time = 0.0;
}

void Clock::stop() { start_time = 0.0; }

void Clock::update() {
  if (start_time != 0) {
    elapsed_time = Platform::instance()->get_absolute_time() - start_time;
  }
}
} // namespace Helix
