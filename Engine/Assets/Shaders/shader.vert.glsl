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
  mat4 model; // 64B
  uint albedoIndex; // 4B
  uint normalIndex; // 4B
};


//layout(location = 0) out vec2 outFragPos;
//layout(location = 1) out vec3 outTexCoords;
//layout(location = 2) out mat3 outTBN;

// TODO: Indirect draw calls, use push constants to push the buffer address for the mesh matrices
// Vertex shader pushes the mesh info index to the fragment shader

void main() {
  gl_Position = ubo.viewProj * model * vec4(inPosition.xyz, 1.0);

  //outTexCoords = inTexCoord.xy;
  //outFragPos = vec3(model * vec4(inPosition.xyz, 1.0f));

  //mat3 normalMatrix = transpose(inverse(mat3(model)));
  //vec3 T = normalize(normalMatrix * inTangent.xyz);
  //vec3 N = normalize(normalMatrix * inNormal.xyz);
  //// re-orthogonalize T with respect to N
  //T = normalize(T - dot(T, N) * N);
  //vec3 B = cross(N, T) * inTangent.w;

  //outTBN = mat3(T, B, N);
}
