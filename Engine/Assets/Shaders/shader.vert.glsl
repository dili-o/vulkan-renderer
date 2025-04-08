#version 450

layout(set = 1, binding = 0) uniform UniformBufferObject {
    mat4 model;
    mat4 view;
    mat4 proj;
} ubo;

struct Test{
  uint num;
};

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec2 inTexCoord;

layout(location = 0) out vec2 tex_coords;
layout(location = 1) out vec3 frag_pos;

void main() {
  gl_Position = ubo.proj * ubo.view * ubo.model * vec4(inPosition, 1.0);
  tex_coords = inTexCoord;
  frag_pos = vec3(ubo.model * vec4(inPosition, 1.0f));
}
