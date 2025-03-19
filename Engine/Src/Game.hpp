#pragma once

#include <Core/Defines.hpp>
#include <Core/Log.hpp>

namespace Helix {
struct Game {
  virtual void init() {};
  virtual void shutdown() {};
  virtual void update(f32 dt) {};
  virtual void render(f32 dt) {};
  virtual void resize(u32 width, u32 height) {};
};
} // namespace Helix
