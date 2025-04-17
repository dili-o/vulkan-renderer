#pragma once

#include "Containers/Array.hpp"
#include "Containers/ResourcePool.hpp"
#include "Core/Defines.hpp"
#include "Renderer/GPUResourceTypes.hpp"
#include "Renderer/GPUResources.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace Helix {
struct Camera;

enum RendererBackendType {
  RENDERER_BACKEND_TYPE_VULKAN,
  RENDERER_BACKEND_TYPE_OPENGL,
  RENDERER_BACKEND_TYPE_DIRECTX
};

namespace VsyncMode {
enum Enum { On, Off, Adaptive, Fast };
}

struct Platform;

struct RendererConfig {
  cstring application_name{nullptr};
  Platform *platform{nullptr};
  RendererBackendType backend_type{RENDERER_BACKEND_TYPE_VULKAN};
  VsyncMode::Enum vsync_mode{VsyncMode::On};
  u32 max_frames_in_flight;
};

struct PBRMaterial {
  TextureHandle albedo_texture_handle{};
  TextureHandle normal_texture_handle{};
  TextureHandle roughness_texture_handle{};
  TextureHandle occlusion_texture_handle{};
};

struct MeshDraw {
  BufferHandle internal_index_buffer;
  u32 material_index;
  u32 primitive_count;
};

struct Mesh {
  Array<MeshDraw> draws;
  BufferHandle internal_vertex_buffer;
  // Transform transform;
};

struct Game;

struct RenderPacket {
  f32 delta_time;
  u32 current_frame;
  Camera *camera{nullptr};
  ResourceHandle scene_data_buffer;
  Mesh *meshes{nullptr};
  u32 mesh_count{0};
  Game *game{nullptr};
};

struct Vertex {
  glm::vec3 pos;
  glm::vec2 tex_coord;

  bool operator==(const Vertex &other) const {
    return pos == other.pos && tex_coord == other.tex_coord;
  }
};

struct UniformBufferObject {
  glm::mat4 model;
  glm::mat4 view;
  glm::mat4 proj;
};

} // namespace Helix
