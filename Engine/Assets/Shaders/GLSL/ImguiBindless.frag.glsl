#version 450

#extension GL_GOOGLE_include_directive : require
#include "Globals.glsl"

layout (location = 0) in vec2 inFragUV;
layout (location = 1) in vec4 inFragColor;
layout (location = 2) flat in uint inTextureId;

layout (location = 0) out vec4 outColor;

void main() {
  outColor = inFragColor * texture(globalSamplers[nonuniformEXT(inTextureId)], inFragUV.st);
}
