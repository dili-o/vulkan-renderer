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

void main() {
  vec3 light_pos = vec3(0.f, 5.f ,0.f);

  vec4 diffuse = texture(global_samplers[nonuniformEXT(albedo_index)], tex_coords); 
  outColor = diffuse * 1.5f;
}
