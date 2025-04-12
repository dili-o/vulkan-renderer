#version 450

#include "globals.h"

layout (location = 0) in vec2 Frag_UV;
layout (location = 1) in vec4 Frag_Color;
layout (location = 2) flat in uint texture_id;

layout (location = 0) out vec4 Out_Color;

void main() {
  Out_Color = Frag_Color * texture(global_samplers[nonuniformEXT(texture_id)], Frag_UV.st);
}
