#pragma once
namespace Helix {

struct Application {
  virtual void init() = 0;
  virtual void run() = 0;
  virtual void shutdown() = 0;
};
} // namespace Helix
