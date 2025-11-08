#version 450

#extension GL_GOOGLE_include_directive : require
#include "Globals.glsl"

layout (location = 0) in vec2 vTexCoord;
layout (location = 1) flat in uint texId;

layout(location = 0) out vec4 outColor;

layout(set = 1, binding = 0) uniform UniformBufferObject {
  mat4 viewProj;
  mat4 lightViewProj;
  mat4 lightViewProjs[4];
  vec4 lightDirection_shadowMap;
  float cascadeSplits[4];
} ubo;

void main() {
  vec2 texCoord = vTexCoord;
  texCoord.y = 1.f - texCoord.y;
  float depthValue = texture(globalSamplers[nonuniformEXT(texId)], texCoord).r;
  outColor = vec4(vec3(depthValue), 1.f);
}
