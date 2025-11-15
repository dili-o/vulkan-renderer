#pragma once

#include "Core/Defines.hpp"

namespace hlx {

struct FileWatcher {
  void init();
  void shutdown();

  bool set_watch_dir(cstring dir);

  cstring watch_dir = nullptr;
};
} // namespace hlx
