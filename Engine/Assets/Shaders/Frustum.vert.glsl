#version 450

#extension GL_GOOGLE_include_directive : require
#include "Globals.glsl"

layout(push_constant) uniform constants
{
  mat4 invViewProj;
  vec4 color;
};

layout(location = 0) out vec4 outColor;

vec3 ndc_to_world_space(mat4 inv_proj_view, float x, float y,
                             float z) {
  vec4 ndc_point = vec4(x, y, z, 1.0f);
  vec4 world_point = inv_proj_view * ndc_point;
  return vec3(world_point) / world_point.w;
}

vec4 positions [] = {
  vec4(-1.f, 1.f, 0.f, 1.f),  // Near Top Left
  vec4(1.f, 1.f, 0.f, 1.f),   // Near Top Right

  vec4(1.f, 1.f, 0.f, 1.f),   // Near Top Right
  vec4(1.f, -1.f, 0.f, 1.f),  // Near Bottom Right

  vec4(1.f, -1.f, 0.f, 1.f),  // Near Bottom Right
  vec4(-1.f, -1.f, 0.f, 1.f), // Near Bottom Left
                               
  vec4(-1.f, -1.f, 0.f, 1.f), // Near Bottom Left
  vec4(-1.f, 1.f, 0.f, 1.f),  // Near Top Left
  //
  //
  vec4(-1.f, 1.f, 0.f, 1.f),  // Near Top Left
  vec4(-1.f, 1.f, 1.f, 1.f),   // Far Top Left

  vec4(1.f, 1.f, 0.f, 1.f),   // Near Top Right
  vec4(1.f, 1.f, 1.f, 1.f),    // Far Top Right

  vec4(1.f, -1.f, 0.f, 1.f),  // Near Bottom Right
  vec4(1.f, -1.f, 1.f, 1.f),   // Far Bottom Right
                               
  vec4(-1.f, -1.f, 0.f, 1.f), // Near Bottom Left
  vec4(-1.f, -1.f, 1.f, 1.f),  // Far Bottom Left
  //
  //
  vec4(-1.f, 1.f, 1.f, 1.f),  // Far Top Left
  vec4(1.f, 1.f, 1.f, 1.f),   // Far Top Right

  vec4(1.f, 1.f, 1.f, 1.f),   // Far Top Right
  vec4(1.f, -1.f, 1.f, 1.f),  // Far Bottom Right

  vec4(1.f, -1.f, 1.f, 1.f),  // Far Bottom Right
  vec4(-1.f, -1.f, 1.f, 1.f), // Far Bottom Left
                               
  vec4(-1.f, -1.f, 1.f, 1.f), // Far Bottom Left
  vec4(-1.f, 1.f, 1.f, 1.f),  // Far Top Left

};

void main() {
  vec4 position = positions[gl_VertexIndex];
  gl_Position = ubo.viewProj * vec4(ndc_to_world_space(invViewProj, position.x, position.y, position.z), 1.f);
  outColor = color;
}


