#pragma once
#include "Core/Defines.hpp"
#include "Core/Service.hpp"

namespace Helix {
struct PlatformConfiguration {

  u32 width;
  u32 height;

  cstring name;
}; // struct WindowConfiguration

struct Platform : public Service {

  virtual void init(void *configuration) override;
  virtual void shutdown() override;

  HELIX_DECLARE_SERVICE(Platform)

  void handle_os_messages();
  f64 get_absolute_time();
  void sleep(u64 ms);

  void set_title(cstring title);

  void *platform_handle = nullptr;
  bool requested_exit{true};
  bool is_suspended{false};
  i32 width{0};
  i32 height{0};
  cstring name{nullptr};
};
} // namespace Helix
