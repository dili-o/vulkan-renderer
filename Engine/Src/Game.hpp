#pragma once

#include <Core/Defines.hpp>
#include <Core/Log.hpp>
#include <Renderer/Camera.hpp>
#include <Renderer/Scene.hpp>

namespace Helix {

struct RenderPacket;

struct Game {
  virtual void init() {};
  virtual void shutdown() {};
  virtual void update(RenderPacket *packet) {};
  virtual void render_frame(f32 dt) {};
  virtual void resize(u32 width, u32 height) {};

  Camera camera;
  Scene scene;
};
} // namespace Helix
