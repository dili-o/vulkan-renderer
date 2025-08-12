#ifdef _GLSL
#extension GL_EXT_buffer_reference : require
#endif

struct IndexedDrawCommand{
  uint indexCount;
  uint instanceCount;
  uint firstIndex;
  uint vertexOffset;
  uint firstInstance;

  uint objectID;
  uint albedoTextureIndex;
};

// TODO: Use pos.w and normal.w as the texCoord x and y
struct Vertex{
  vec4 pos;
  vec4 normal; 
  vec4 tangent;
  vec4 texCoord; 
};

struct PBRMaterial {
  uint albedoTextureIndex;
  uint normalTextureIndex;
  uint roughnessTextureIndex;
  uint occlusionTextureIndex;
};

#ifdef _GLSL
// Buffer Device Addresses
layout(std430, buffer_reference) readonly buffer Vertices{
    Vertex vertices[];
};

layout(std430, buffer_reference) readonly buffer PBRMaterials{
    PBRMaterial materials[];
};

layout(std430, buffer_reference) buffer MeshData{
  IndexedDrawCommand drawCommands[];
};
#endif
