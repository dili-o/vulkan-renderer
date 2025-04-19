#pragma once

#include "Core/Defines.hpp"

namespace Helix {
//
// TODO: Implement critical sections
struct HMutex {
  bool create();

  void destroy();

  bool lock();

  bool unlock();

  void *internal_data;
};

} // namespace Helix
