#include "Core/Application.hpp"
#include "Core/Defines.hpp"

namespace Helix {
struct Sandbox : public Application {
  virtual void init() override;
  virtual void run() override;
  virtual void shutdown() override;

  f32 get_delta_time() { return delta_time; }
  void render_frame();
  f64 last_time{0.0};
  f32 delta_time{0.f};
  bool limit_frames{true};
};
} // namespace Helix
