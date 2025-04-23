#version 450

#include "globals.h"

layout(location = 0) in vec2 tex_coords;
layout(location = 1) in vec3 frag_pos;

layout(location = 0) out vec4 outColor;

layout(push_constant) uniform constants
{
  uint albedo_index;
};

uint hash(uint a)
{
   a = (a+0x7ed55d16) + (a<<12);
   a = (a^0xc761c23c) ^ (a>>19);
   a = (a+0x165667b1) + (a<<5);
   a = (a+0xd3a2646c) ^ (a<<9);
   a = (a+0xfd7046c5) + (a<<3);
   a = (a^0xb55a4f09) ^ (a>>16);
   return a;
}

#define RANDOM 0

void main() {
#if RANDOM
  uint mhash = hash(uint(gl_PrimitiveID));
  vec3 color = vec3(float(mhash & 255), float((mhash >> 8) & 255), float((mhash >> 16) & 255)) / 255.0;
  color *= 1.25f;
  outColor = vec4(color, 1.0f) ;
#else
  vec4 diffuse = texture(global_samplers[nonuniformEXT(albedo_index)], tex_coords); 
  outColor = diffuse * 1.5f;
#endif // RANDOM
}
