#pragma once

#include "Containers/ResourcePool.hpp"
#include "Core/Defines.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace Helix {
struct Camera;

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
  Camera *camera;
};

struct Vertex {
  glm::vec2 pos;
  glm::vec3 color;
};

struct UniformBufferObject {
  glm::mat4 model;
  glm::mat4 view;
  glm::mat4 proj;
};

} // namespace Helix
