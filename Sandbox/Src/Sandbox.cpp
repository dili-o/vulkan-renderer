#include "Sandbox.hpp"
#include "Core/Input.hpp"

namespace Helix {
void Sandbox::init() { HINFO("Game Initialised"); }
void Sandbox::shutdown() { HINFO("Game Shutdown"); }
void Sandbox::update(f32 dt) {
  if (InputService::instance()->is_key_down(SDL_SCANCODE_C)) {
    HDEBUG("c held");
  }
}
void Sandbox::render(f32 dt) {}
void Sandbox::resize(u32 width, u32 height) {}

} // namespace Helix
