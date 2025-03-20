#pragma once

#include "Containers/ResourcePool.hpp"
#include "Core/Defines.hpp"

#include "glm/glm.hpp"

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

struct BufferResource {
  ResourceHandle handle;
  ResourceHandle internal_handle;
};

struct RenderPacket {
  f32 delta_time;
};

struct Vertex {
  glm::vec2 pos;
  glm::vec3 color;
};

} // namespace Helix
