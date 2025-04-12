#version 450

layout( location = 0 ) in vec2 Position;
layout( location = 1 ) in vec2 UV;
layout( location = 2 ) in vec4 Color_UNORM; // NOTE: This should be changed to a uint

layout( location = 0 ) out vec2 Frag_UV;
layout( location = 1 ) out vec4 Frag_Color;
layout (location = 2) flat out uint texture_id;

layout(push_constant) uniform constants
{
  vec4 scale_translate;
};

void main() {
  Frag_UV = UV;
  Frag_Color = Color_UNORM;
  texture_id = gl_InstanceIndex;
  vec2 scale = scale_translate.xy;
  vec2 translate = scale_translate.zw;
  gl_Position = vec4(Position * scale + translate, 0, 1);
}

