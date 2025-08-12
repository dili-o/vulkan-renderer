#pragma once

#include "Containers/Array.hpp"
#include "Core/Defines.hpp"
#include "Renderer/GPUResourceTypes.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>

#define MAX_TEXTURES 1000
#define MAX_MATERIALS 4000

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

// TODO: Add name for debugging
struct PBRMaterial {
  TextureHandle albedo_texture_handle{};
  TextureHandle normal_texture_handle{};
  TextureHandle roughness_texture_handle{};
  TextureHandle occlusion_texture_handle{};
};

struct GPUPBRMaterial {
  u32 albedo_texture_index;
  u32 normal_texture_index;
  u32 roughness_texture_index;
  u32 occlusion_texture_index;
};

struct Transform {
  glm::vec3 position{0.f};
  glm::quat rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
  glm::vec3 scale{1.f};

  glm::mat4 get_mat4() {
    return glm::translate(glm::mat4(1.f), position) * glm::toMat4(rotation) *
           glm::scale(glm::mat4(1.f), scale);
  }

  void set_transform(glm::mat4 &matrix) {
    position = glm::vec3(matrix[3]);

    scale.x = glm::length(glm::vec3(matrix[0]));
    scale.y = glm::length(glm::vec3(matrix[1]));
    scale.z = glm::length(glm::vec3(matrix[2]));

    glm::mat3 rotation_matrix;
    rotation_matrix[0] = glm::vec3(matrix[0]) / scale.x;
    rotation_matrix[1] = glm::vec3(matrix[1]) / scale.y;
    rotation_matrix[2] = glm::vec3(matrix[2]) / scale.z;
    rotation = glm::quat_cast(rotation_matrix);
  }
};

template <typename T> struct UnifiedBuffer {
  BufferHandle handle;
  size_t current_size{0};
  size_t capacity{0};

  size_t size_in_bytes() const { return current_size * sizeof(T); }
};

struct MeshDraw {
  u64 vertex_buffer_offset;
  u64 index_buffer_offset;
  u32 material_index;
  u32 primitive_count;
  Transform transform;
};

struct Mesh {
  Array<MeshDraw> draws;
};

struct Game;

struct RenderPacket {
  f32 delta_time;
  u32 current_frame;
  Camera *camera{nullptr};
  glm::mat4 inv_previous_view_proj;
  bool freeze_camera = false;
  BufferHandle scene_data_buffer;
  Mesh *meshes{nullptr};
  u32 mesh_count{0};
  Game *game{nullptr};
};

struct alignas(16) Vertex {
  glm::vec4 pos;
  glm::vec4 normal;
  glm::vec4 tangent;
  glm::vec4 tex_coord;

  bool operator==(const Vertex &other) const {
    return pos == other.pos && normal == other.normal &&
           tangent == other.tangent && tex_coord == other.tex_coord;
  }
};

struct UniformBufferObject {
  glm::mat4 view;
  glm::mat4 proj;
  glm::mat4 view_proj;
};

} // namespace Helix
