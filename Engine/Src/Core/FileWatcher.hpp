#pragma once

#include "Core/Defines.hpp"
#include "Core/Service.hpp"

namespace Helix {

struct FileWatcherService : public Service {
  virtual void init(void *config = nullptr) override;
  virtual void shutdown() override;

  HELIX_DECLARE_SERVICE(FileWatcherService)

  bool set_watch_dir(cstring dir);

  cstring watch_dir = nullptr;
};
} // namespace Helix
