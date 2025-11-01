#pragma once
#include "Core/Defines.hpp"

namespace hlx {

struct HLX_API Clock {
public:
  void start();
  void stop();

  f64 get_elapsed_time_s();
  f64 get_elapsed_time_ms();

private:
  void update();

private:
  f64 start_time{0.0};
  f64 elapsed_time{0.0};
};
} // namespace hlx
