#include "Platform/Platform.hpp"

namespace Helix {

struct Clock {
  void start();
  void stop();
  void update();

  f64 start_time{0.0};
  f64 elapsed_time{0.0};
};
} // namespace Helix
