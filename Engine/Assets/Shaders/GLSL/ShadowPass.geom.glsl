#version 450

#extension GL_GOOGLE_include_directive : require
#include "Globals.glsl"

layout(triangles, invocations = 4) in;
layout(triangle_strip, max_vertices = 3) out;

void main() {
  for (int i = 0; i < 3; ++i) {
    gl_Position = 
        ubo.lightViewProjs[gl_InvocationID] * gl_in[i].gl_Position;
    gl_Layer = gl_InvocationID;
    EmitVertex();
  }
  EndPrimitive();
}
