#include "Camera.hpp"
#include "Core/Application.hpp"

namespace hlx {
struct Sandbox : public Application {
  virtual void init() override;
  virtual void run() override;
  virtual void shutdown() override;

  void render_frame();

  f64 last_time{0.0};
  f32 delta_time{0.f};
  bool limit_frames{true};
  Camera camera;
};
} // namespace hlx
