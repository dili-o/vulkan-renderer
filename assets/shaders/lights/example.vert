#version 450 
layout ( std140, binding = 0 ) uniform LocalConstants {
	mat4 model;
	mat4 view_projection;
	mat4 model_inverse;
	vec4 eye;
	vec4 light;
};
layout(location=0) in vec3 position;
layout(location=1) in vec4 tangent;
layout(location=2) in vec3 normal;
layout(location=3) in vec2 texCoord0;

layout (location = 0) out vec2 vTexcoord0;
layout (location = 1) out vec3 vNormal;
layout (location = 2) out vec4 vTangent;
layout (location = 3) out vec4 vPosition;

void main() {
	gl_Position = view_projection * model * vec4(position, 1);
	vPosition = model * vec4(position, 1.0);
	vTexcoord0 = texCoord0;
	vNormal = mat3(model_inverse) * normal;
	vTangent = tangent;
}