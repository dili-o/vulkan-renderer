#version 450

#extension GL_ARB_shader_viewport_layer_array : enable

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;

layout(location = 0) out vec4 outWorldPos_ViewZ;
layout(location = 1) out vec3 outNormal;
layout(location = 2) out vec2 outTexCoord;
layout(location = 3) flat out uint texId;

layout(set = 1, binding = 0) uniform UniformBufferObject {
  mat4 viewProj;
  mat4 lightViewProj;
  mat4 lightViewProjs[4];
  vec4 lightDirection_shadowMap;
  float cascadeSplits[4];
} ubo;

layout(push_constant) uniform constants {
  mat4 model; 
  mat4 view;
};

void main() {
  vec4 viewPos = view * model * vec4(inPosition, 1.f);
  outWorldPos_ViewZ = model * vec4(inPosition, 1.f);
  outWorldPos_ViewZ.w = viewPos.z;

  outNormal = inNormal;
  outTexCoord = inTexCoord;
  texId = gl_InstanceIndex;
  gl_Position = ubo.viewProj * model * vec4(inPosition.xyz, 1.f);
  gl_Layer = 1;
}
