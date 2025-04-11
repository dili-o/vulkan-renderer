#version 450

#extension GL_EXT_nonuniform_qualifier : enable

layout(set = 0, binding = 0) uniform sampler2D global_samplers[];

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


void main() {
  vec3 light_pos = vec3(0.f, 5.f ,0.f);

  vec4 diffuse = texture(global_samplers[nonuniformEXT(albedo_index)], tex_coords); 
  outColor = diffuse * 1.5f;
  // uint mhash = hash(uint(gl_PrimitiveID ));
  // vec3 color = vec3(float(mhash & 255), float((mhash >> 8) & 255), float((mhash >> 16) & 255)) / 255.0;
  // outColor = vec4(color, 1.0f);
}
