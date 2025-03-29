#version 450

layout(set = 0, binding = 0) uniform sampler2D tex_sampler;

layout(location = 0) in vec2 tex_coords;
layout(location = 1) in vec3 frag_pos;

layout(location = 0) out vec4 outColor;

void main() {
  vec3 light_pos = vec3(0, 5 ,0);

  float distance = length(light_pos - frag_pos);

  float attenuation = 1.0 - clamp(distance / 15, 0.0, 1.0);

  vec4 diffuse = texture(tex_sampler, tex_coords); 
  outColor = diffuse * attenuation;
}
