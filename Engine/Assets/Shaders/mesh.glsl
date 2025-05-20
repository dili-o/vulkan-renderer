#extension GL_EXT_buffer_reference : require

struct IndexedDrawCommand{
  uint indexCount;
  uint instanceCount;
  uint firstIndex;
  int  vertexOffset;
  uint firstInstance;
};

struct Vertex
{
  vec4 pos;
  vec4 normal; 
  vec4 tangent;
  vec4 tex_coord; 
};

layout(std430, buffer_reference) readonly buffer Vertices
{
    Vertex vertices[];
};

