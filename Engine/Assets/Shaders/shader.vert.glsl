#version 450

layout(set = 1, binding = 0) uniform UniformBufferObject {
    mat4 view;
    mat4 proj;
} ubo;

layout(push_constant) uniform constants
{
  mat4 model;
  uint albedo_index;
};


layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec2 inTexCoord;

layout(location = 0) out vec2 tex_coords;
layout(location = 1) out vec3 frag_pos;

void main() {
  gl_Position = ubo.proj * ubo.view * model * vec4(inPosition, 1.0);
  tex_coords = inTexCoord;
  frag_pos = vec3(model * vec4(inPosition, 1.0f));
}
