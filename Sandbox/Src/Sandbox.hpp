#pragma once

#include "Game.hpp"

namespace Helix {
struct Sandbox : public Game {

  virtual void init() override;
  virtual void shutdown() override;
  virtual void update(f32 dt) override;
  virtual void render_frame(f32 dt) override;
  virtual void resize(u32 width, u32 height) override;
};
} // namespace Helix
