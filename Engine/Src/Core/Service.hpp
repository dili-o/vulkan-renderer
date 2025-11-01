#pragma once

#include "Core/Defines.hpp"
namespace hlx {
struct HLX_API Service {
  virtual void init(void *configuration) {}
  virtual void shutdown() {}

#define HELIX_DECLARE_SERVICE(Type) static Type *instance();
};
} // namespace hlx
