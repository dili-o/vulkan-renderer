#pragma once

#include "Core/Defines.hpp"

namespace hlx {
//
// TODO: Implement critical sections
struct HMutex {
  bool create();

  void destroy();

  bool lock();

  bool unlock();

  void *internal_data;
};

} // namespace hlx
