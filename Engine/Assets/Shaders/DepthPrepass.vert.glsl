#version 450

#extension GL_GOOGLE_include_directive : require
#include "Mesh.glsl"

layout(set = 0, binding = 0) uniform UniformBufferObject {
    mat4 view;
    mat4 proj;
    mat4 viewProj;
} ubo;

layout(push_constant) uniform constants
{
  Models models;
  IndexedDrawCommands draw_commands;
};

layout(location = 0) in vec4 inPosition;
layout(location = 1) in vec4 inNormal;
layout(location = 2) in vec4 inTangent;
layout(location = 3) in vec4 inTexCoord;

layout(location = 0) flat out uint outMeshID;

void main() {
  uint mesh_id = draw_commands.data[gl_DrawIDARB].mesh_id;

  gl_Position = ubo.proj * ubo.view * models.data[mesh_id] * vec4(inPosition.xyz, 1.0);

  outMeshID = mesh_id;
}

