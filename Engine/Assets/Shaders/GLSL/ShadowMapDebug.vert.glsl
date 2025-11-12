#version 450

#extension GL_GOOGLE_include_directive : require
#include "Globals.glsl"

layout (location = 0) out vec2 vTexCoord;
layout (location = 1) flat out uint texId;

void main() {
  vTexCoord.xy = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
  gl_Position = vec4(vTexCoord.xy * 2.0f - 1.0f, 0.0f, 1.0f);
  gl_Position.y = -gl_Position.y;

  texId = gl_InstanceIndex;
}
