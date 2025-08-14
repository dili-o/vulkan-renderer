#version 450

#extension GL_GOOGLE_include_directive : require
#include "Mesh.glsl"

layout(location = 0) in vec4 inPosition;
layout(location = 1) in vec4 inNormal;
layout(location = 2) in vec4 inTangent;
layout(location = 3) in vec4 inTexCoord;

layout(set = 1, binding = 0) uniform UniformBufferObject {
    mat4 view;
    mat4 proj;
    mat4 viewProj;
} ubo;

layout(push_constant) uniform constants
{
  Models models;
  IndexedDrawCommands draw_commands;
  PBRMaterials pbr_materials;
  
  uint visibilityBufferIndex;
  float mouseX;
  float mouseY;
  uint padding_;
};


layout(location = 0) out vec3 outFragPos;
layout(location = 1) out vec2 outTexCoords;
layout(location = 2) flat out uint outAlbedoTexture;
layout(location = 3) flat out uint outNormalTexture;
layout(location = 4) flat out uint outMeshID;
layout(location = 5) out mat3 outTBN;

// TODO: Indirect draw calls, use push constants to push the buffer address for the mesh matrices
// Vertex shader pushes the mesh info index to the fragment shader

void main() {
  uint mesh_id = draw_commands.data[gl_DrawIDARB].mesh_id;

  gl_Position = ubo.proj * ubo.view * models.data[mesh_id] * vec4(inPosition.xyz, 1.0);

  outTexCoords = inTexCoord.xy;
  outFragPos = vec3(models.data[mesh_id] * vec4(inPosition.xyz, 1.0f));

  mat3 normalMatrix = transpose(inverse(mat3(models.data[mesh_id])));
  vec3 T = normalize(normalMatrix * inTangent.xyz);
  vec3 N = normalize(normalMatrix * inNormal.xyz);
  // re-orthogonalize T with respect to N
  T = normalize(T - dot(T, N) * N);
  vec3 B = cross(N, T) * inTangent.w;

  outTBN = mat3(T, B, N);

  outAlbedoTexture = pbr_materials.data[mesh_id].albedo_texture_index;
  outNormalTexture = pbr_materials.data[mesh_id].normal_texture_index;
  outMeshID = mesh_id;
}
