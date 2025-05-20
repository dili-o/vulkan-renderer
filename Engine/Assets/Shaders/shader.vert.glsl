#version 450

#include "mesh.glsl"

layout(set = 1, binding = 0) uniform UniformBufferObject {
    mat4 view;
    mat4 proj;
} ubo;

layout(push_constant) uniform constants
{
  mat4 model; // 64B
  Vertices vertex_buffer;
  uint albedo_index; // 4B
  uint normal_index; // 4B
};

layout(location = 0) out vec2 tex_coords;
layout(location = 1) out vec3 frag_pos;
layout(location = 2) out mat3 TBN;

// TODO: Indirect draw calls, use push constants to push the buffer address for the mesh matrices
// Vertex shader pushes the mesh info index to the fragment shader


void main() {
  restrict Vertex vertex = vertex_buffer.vertices[gl_VertexIndex];
  gl_Position = ubo.proj * ubo.view * model * vec4(vertex.pos.xyz, 1.0);

  tex_coords = vertex.tex_coord.xy;
  frag_pos = vec3(model * vec4(vertex.pos.xyz, 1.0f));

  mat3 normal_matrix = transpose(inverse(mat3(model)));
  vec3 T = normalize(normal_matrix * vertex.tangent.xyz);
  vec3 N = normalize(normal_matrix * vertex.normal.xyz);
  // re-orthogonalize T with respect to N
  T = normalize(T - dot(T, N) * N);
  vec3 B = cross(N, T) * vertex.tangent.w;

  TBN = mat3(T, B, N);
}
