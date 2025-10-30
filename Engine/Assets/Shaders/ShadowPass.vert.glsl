#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;

layout(set = 0, binding = 0) uniform UniformBufferObject {
    mat4 viewProj;
    mat4 lightViewProj;
} ubo;

void main() {
  gl_Position = ubo.lightViewProj * vec4(inPosition.xyz, 1.f);
}

