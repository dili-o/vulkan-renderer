#ifdef _GLSL
#extension GL_EXT_buffer_reference : require
#endif

struct IndexedDrawCommand{
  uint indexCount;
  uint instanceCount;
  uint firstIndex;
  int  vertexOffset;
  uint firstInstance;
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
layout(std430, buffer_reference) readonly buffer Vertices{
    Vertex vertices[];
};

layout(std430, buffer_reference) readonly buffer PBRMaterials{
    PBRMaterial materials[];
};
#endif
