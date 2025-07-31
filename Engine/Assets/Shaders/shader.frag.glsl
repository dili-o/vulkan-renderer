#version 450

#extension GL_GOOGLE_include_directive : require

#include "Globals.glsl"
#include "Mesh.glsl"

//layout(location = 0) in vec2 inFragPos;
//layout(location = 1) in vec3 inTexCoords;
//layout(location = 2) in mat3 inTBN;

layout(location = 0) out vec4 outColor;

layout(push_constant) uniform constants
{
  mat4 model; // 64B
  uint albedoIndex; // 4B
  uint normalIndex; // 4B
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

#define RANDOM 1

void main() {
#if RANDOM
  uint mhash = hash(uint(gl_PrimitiveID));
  vec3 color = vec3(float(mhash & 255), float((mhash >> 8) & 255), float((mhash >> 16) & 255)) / 255.0;
  color *= 1.25f;
  outColor = vec4(color, 1.0f) ;
#else
  vec4 albedo = texture(globalSamplers[nonuniformEXT(albedoIndex)], inTexCoords); 
  // ambient
  outColor = albedo;
  vec3 ambient = 0.25f * albedo.rgb;
  
  // diffuse 
  vec3 normal = texture(globalSamplers[nonuniformEXT(normalIndex)], inTexCoords).rgb;
  normal = normal * 2.f - 1.f;
  normal = normalize(inTBN * normal);

  vec3 lightDir = normalize(-vec3(1.f, -1.f, 0.f));  
  float diff = max(dot(normal, lightDir), 0.0);
  vec3 diffuse = diff * albedo.rgb;

  vec3 result = ambient + diffuse;

  // float gamma = 2.2;
  // result = pow(result, vec3(1.0/gamma)); 

  outColor = vec4(result, albedo.a);
#endif // RANDOM
}
