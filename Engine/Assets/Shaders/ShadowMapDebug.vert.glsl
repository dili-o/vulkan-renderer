#version 450

layout (location = 0) out vec2 vTexCoord;
layout (location = 1) flat out uint texId;

layout(set = 1, binding = 0) uniform UniformBufferObject {
  mat4 viewProj;
  mat4 lightViewProj;
  mat4 lightViewProjs[4];
  vec4 lightDirection_shadowMap;
  float cascadeSplits[4];
} ubo;

void main() {
  vTexCoord.xy = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
  gl_Position = vec4(vTexCoord.xy * 2.0f - 1.0f, 0.0f, 1.0f);
  gl_Position.y = -gl_Position.y;

  texId = gl_InstanceIndex;
}
