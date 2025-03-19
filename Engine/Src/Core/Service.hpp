#pragma once

namespace Helix {
struct Service {
  virtual void init(void *configuration) {}
  virtual void shutdown() {}

#define HELIX_DECLARE_SERVICE(Type) static Type *instance();
};
} // namespace Helix
