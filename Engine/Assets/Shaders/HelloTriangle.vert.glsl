#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;

layout(location = 0) out vec4 outFragPosLightSpace;
layout(location = 1) out vec3 outNormal;
layout(location = 2) out vec2 outTexCoord;
layout(location = 3) flat out uint texId;

layout(set = 1, binding = 0) uniform UniformBufferObject {
  mat4 viewProj;
  mat4 lightViewProj;
  vec4 lightDirection_shadowMap;
} ubo;

layout(push_constant) uniform constants {
  mat4 model; 
};

void main() {
  outFragPosLightSpace = ubo.lightViewProj * model * vec4(inPosition, 1.f);
  outNormal = inNormal;
  outTexCoord = inTexCoord;
  texId = gl_InstanceIndex;
  gl_Position = ubo.viewProj * model * vec4(inPosition.xyz, 1.f);
}
