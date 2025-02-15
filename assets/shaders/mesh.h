
#ifndef HELIX_GLSL_MESH_H
#define HELIX_GLSL_MESH_H

uint DrawFlags_AlphaMask = 1 << 0;
uint DrawFlags_DoubleSided = 1 << 1;
uint DrawFlags_Transparent = 1 << 2;
uint DrawFlags_Phong = 1 << 3;
uint DrawFlags_HasNormals = 1 << 4;
uint DrawFlags_TexCoords = 1 << 5;
uint DrawFlags_HasTangents = 1 << 6;
uint DrawFlags_HasJoints = 1 << 7;
uint DrawFlags_HasWeights = 1 << 8;
uint DrawFlags_AlphaDither = 1 << 9;

struct MeshInstanceDraw {
  mat4 model;
  mat4 model_inverse;

  uint mesh_draw_index;
  uint pad000;
  uint pad001;
  uint pad002;
};

struct MaterialData {
  uvec4 textures;  // base_color , roughness, normal, occlusion

  vec4 base_color_factor;

  vec4 roughness_metallic_occlusion_factor;

  uint flags;
  float alpha_cutoff;
  uint padding[2];
};

struct MeshData {
  uint vertex_offset;
  uint meshlet_offset;
  uint meshlet_count;
  uint padding0_;
};

#if NVIDIA
struct MeshDrawCommand {
  uint drawId;
  // VkDrawIndexedIndirectCommand
  uint indexCount;
  uint instanceCount;
  uint firstIndex;

  uint vertexOffset;
  uint firstInstance;
  // VkDrawMeshTasksIndirectCommandNV
  uint taskCount;
  uint firstTask;
};
#else
struct MeshDrawCommand {
  uint drawId;
  uint firstTask;
  // VkDrawIndexedIndirectCommand
  uint indexCount;
  uint instanceCount;

  uint firstIndex;
  uint vertexOffset;
  uint firstInstance;
  // VkDrawMeshTasksIndirectCommandEXT
  uint x;

  uint y;
  uint z;
  uint padding[2];
};

#endif  // NVIDIA

// layout ( std430, set = MATERIAL_SET, binding = 2 ) readonly buffer MeshDraws
// {
//
//     MeshDraw    mesh_draws[];
// };

layout(std430, set = MATERIAL_SET,
       binding = 2) readonly buffer MaterialDataBuffer {
  MaterialData material_data[];
};

layout(std430, set = MATERIAL_SET, binding = 3) readonly buffer MeshDataBuffer {
  MeshData mesh_data[];
};

layout(std430, set = MATERIAL_SET,
       binding = 10) readonly buffer MeshInstanceDraws {
  MeshInstanceDraw mesh_instance_draws[];
};

layout(std430, set = MATERIAL_SET, binding = 12) readonly buffer MeshBounds {
  vec4 mesh_bounds[];
};

// 2D Polyhedral Bounds of a Clipped, Perspective-Projected 3D Sphere. Michael
// Mara, Morgan McGuire. 2013
bool project_sphere(vec3 C, float r, float znear, float P00, float P11,
                    out vec4 aabb) {
  // Check if the sphere intersects with the near plane
  if (-C.z - r < znear) return false;

  vec2 cx = vec2(C.x, -C.z);
  vec2 vx = vec2(sqrt(dot(cx, cx) - r * r), r);
  vec2 minx = mat2(vx.x, vx.y, -vx.y, vx.x) * cx;
  vec2 maxx = mat2(vx.x, -vx.y, vx.y, vx.x) * cx;

  vec2 cy = -C.yz;
  vec2 vy = vec2(sqrt(dot(cy, cy) - r * r), r);
  vec2 miny = mat2(vy.x, vy.y, -vy.y, vy.x) * cy;
  vec2 maxy = mat2(vy.x, -vy.y, vy.y, vy.x) * cy;

  aabb = vec4(minx.x / minx.y * P00, miny.x / miny.y * P11,
              maxx.x / maxx.y * P00, maxy.x / maxy.y * P11);
  aabb = aabb.xwzy * vec4(0.5f, -0.5f, 0.5f, -0.5f) +
         vec4(0.5f);  // clip space -> uv space

  return true;
}

#if defined(TASK) || defined(MESH)

#extension GL_EXT_shader_explicit_arithmetic_types : require
#extension GL_EXT_shader_16bit_storage : require
#extension GL_EXT_shader_8bit_storage : require
#extension GL_KHR_shader_subgroup_ballot : require

#if NVIDIA
#extension GL_NV_mesh_shader : require
#else
#extension GL_EXT_mesh_shader : require
#endif  // NVIDIA

// Common data
struct VertexExtraData {
  uint8_t nx, ny, nz, nw;  // normal
  uint8_t tx, ty, tz, tw;  // tangent
  float16_t tu, tv;        // tex coords
  float padding;
};

struct VertexPosition {
  vec3 v;
  float padding;
};

struct Meshlet {
  // vec3 keeps Meshlet aligned to 16 bytes which is important because C++ has
  // an alignas() directive
  vec3 center;
  float radius;

  int8_t cone_axis[3];
  int8_t cone_cutoff;

  uint dataOffset;
  uint mesh_index;
  uint8_t vertexCount;
  uint8_t triangleCount;
};

layout(set = MATERIAL_SET, binding = 1) readonly buffer Meshlets {
  Meshlet meshlets[];
};

layout(set = MATERIAL_SET, binding = 4) readonly buffer MeshletData {
  uint meshletData[];
};

layout(set = MATERIAL_SET, binding = 5) readonly buffer VertexPositions {
  VertexPosition vertex_positions[];
};

layout(set = MATERIAL_SET, binding = 6) readonly buffer VertexData {
  VertexExtraData vertex_data[];
};

layout(set = MATERIAL_SET, binding = 7) readonly buffer VisibleMeshInstances {
  MeshDrawCommand draw_commands[];
};

layout(set = MATERIAL_SET, binding = 8) buffer VisibleMeshCount {
  uint opaque_mesh_visible_count;
  uint opaque_mesh_culled_count;
  uint transparent_mesh_visible_count;
  uint transparent_mesh_culled_count;

  uint total_count;
  uint total_opaque_mesh_count;
  uint depth_pyramid_texture_index;
  uint late_flag;
};

layout(set = MATERIAL_SET, binding = 11) uniform DirectionalLightData {
  DirectionalLight directional_light_data;
};

#endif  // TASK MESH

#endif  // HELIX_GLSL_MESH_H
