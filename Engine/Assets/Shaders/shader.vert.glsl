#version 450

layout(set = 1, binding = 0) uniform UniformBufferObject {
    mat4 view;
    mat4 proj;
} ubo;

layout(push_constant) uniform constants
{
  mat4 model; // 64B
  uint albedo_index; // 4B
  uint normal_index; // 4B
};

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec4 inTangent;
layout(location = 3) in vec2 inTexCoord;

layout(location = 0) out vec2 tex_coords;
layout(location = 1) out vec3 frag_pos;
layout(location = 2) out mat3 TBN;

void main() {
  gl_Position = ubo.proj * ubo.view * model * vec4(inPosition, 1.0);

  tex_coords = inTexCoord;
  frag_pos = vec3(model * vec4(inPosition, 1.0f));

  mat3 normal_matrix = transpose(inverse(mat3(model)));
  vec3 T = normalize(normal_matrix * inTangent.xyz);
  vec3 N = normalize(normal_matrix * inNormal);
  // re-orthogonalize T with respect to N
  T = normalize(T - dot(T, N) * N);
  vec3 B = cross(N, T) * inTangent.w;

  TBN = mat3(T, B, N);
}
