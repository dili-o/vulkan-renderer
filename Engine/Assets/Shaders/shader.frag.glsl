#version 450

layout(set = 0, binding = 1) uniform TestObject {
  float multiplier;
} testObj;

layout(location = 0) in vec3 fragColor;

layout(location = 0) out vec4 outColor;

void main() {
    outColor = vec4(fragColor * testObj.multiplier, 1.0);
}
