
#extension GL_EXT_nonuniform_qualifier : enable

layout(set = 0, binding = 0) uniform sampler2D globalSamplers[];
layout(set = 0, binding = 0) uniform usampler2D globalSamplersU32[];

layout(set = 1, binding = 0) uniform UniformBufferObject {
  mat4 viewProj;
  mat4 lightViewProj;
  mat4 lightViewProjs[4];
  vec4 lightDirection_shadowMap;
  vec4 cascadeSplits;
  uint cascadeCount;
} ubo;

