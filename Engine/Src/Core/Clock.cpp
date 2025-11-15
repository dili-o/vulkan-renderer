#include "Clock.hpp"
#include "Platform/Platform.hpp"

namespace hlx {
void Clock::start() {
  start_time = Platform::get_absolute_time_s();
  elapsed_time = 0.0;
}

void Clock::stop() { start_time = 0.0; }

void Clock::update() {
  if (start_time != 0) {
    elapsed_time = Platform::get_absolute_time_s() - start_time;
  }
}

f64 Clock::get_elapsed_time_s() {
  update();
  return elapsed_time;
}

f64 Clock::get_elapsed_time_ms() {
  update();
  return elapsed_time * 1000.0;
}

} // namespace hlx
