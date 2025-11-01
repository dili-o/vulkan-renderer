#pragma once
namespace hlx {

struct Application {
  virtual void init() = 0;
  virtual void run() = 0;
  virtual void shutdown() = 0;
};
} // namespace hlx
