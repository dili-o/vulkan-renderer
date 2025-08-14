#ifdef _GLSL
#extension GL_EXT_buffer_reference : require
#extension GL_ARB_shader_draw_parameters : enable
#endif

// TODO: Use pos.w and normal.w as the texCoord x and y
struct Vertex{
  vec4 pos;
  vec4 normal; 
  vec4 tangent;
  vec4 tex_coord; 
};

struct IndexedDrawCommand {
  uint index_count;
  uint instance_count;
  uint first_index;
  uint vertex_offset;
  uint first_instance;

  uint mesh_id;
};

struct PBRMaterial {
  uint albedo_texture_index;
  uint normal_texture_index;
  uint roughness_texture_index;
  uint occlusion_texture_index;
};

struct MeshDraw {
  uint primitive_count;
  uint index_buffer_offset;
  uint vertex_buffer_offset;
};

#ifdef _GLSL

// Buffer Device Addresses
layout(std430, buffer_reference) readonly buffer BoundingVolumes {
    vec4 bounding_spheres[];
};

layout(std430, buffer_reference) readonly buffer PBRMaterials {
    PBRMaterial data[];
};

layout(std430, buffer_reference) readonly buffer MeshDraws {
  MeshDraw data[];
};

layout(std430, buffer_reference) buffer IndexedDrawCommands {
  IndexedDrawCommand data[];
};

layout(std430, buffer_reference) readonly buffer Models {
    mat4 data[];
};

layout(std430, buffer_reference) buffer CountBuffer {
  uint visible_count;
};

#endif
