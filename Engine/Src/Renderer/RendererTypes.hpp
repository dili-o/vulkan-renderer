#pragma once

#include "Core/Defines.hpp"

namespace Helix {
enum RendererBackendType {
  RENDERER_BACKEND_TYPE_VULKAN,
  RENDERER_BACKEND_TYPE_OPENGL,
  RENDERER_BACKEND_TYPE_DIRECTX
};

struct Platform;

struct RendererConfig {
  cstring application_name{nullptr};
  Platform *platform{nullptr};
  RendererBackendType backend_type{RENDERER_BACKEND_TYPE_VULKAN};
};

struct RenderPacket {
  f32 delta_time;
};

} // namespace Helix
