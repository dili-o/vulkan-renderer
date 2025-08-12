#version 450

layout(set = 0, binding = 0) uniform UniformBufferObject {
    mat4 view;
    mat4 proj;
} ubo;

layout(push_constant) uniform constants
{
  mat4 model; // 64B
  uint objectID;
};

layout(location = 0) in vec4 inPosition;
layout(location = 1) in vec4 inNormal;
layout(location = 2) in vec4 inTangent;
layout(location = 3) in vec4 inTexCoord;

layout(location = 0) flat out uint outObjectID;

void main() {
  gl_Position = ubo.proj * ubo.view * model * vec4(inPosition.xyz, 1.0);
  outObjectID = objectID;
}

