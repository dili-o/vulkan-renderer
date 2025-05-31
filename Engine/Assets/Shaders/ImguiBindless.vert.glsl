#version 450

layout( location = 0 ) in vec2 inPosition;
layout( location = 1 ) in vec2 inUV;
layout( location = 2 ) in vec4 inColor_UNORM; // NOTE: This should be changed to a uint

layout( location = 0 ) out vec2 outFragUV;
layout( location = 1 ) out vec4 outFragColor;
layout (location = 2) flat out uint outTextureId;

layout(push_constant) uniform constants
{
  vec4 scaleTranslate;
};

void main() {
  outFragUV = inUV;
  outFragColor = inColor_UNORM;
  outTextureId = gl_InstanceIndex;
  vec2 scale = scaleTranslate.xy;
  vec2 translate = scaleTranslate.zw;
  gl_Position = vec4(inPosition * scale + translate, 0, 1);
}

