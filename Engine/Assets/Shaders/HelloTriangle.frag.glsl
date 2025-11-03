#version 450

#extension GL_GOOGLE_include_directive : require
#include "Globals.glsl"

layout(location = 0) in vec4 inFragPosLightSpace;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;
layout(location = 3) flat in uint texId;

layout(location = 0) out vec4 outColor;

layout(set = 1, binding = 0) uniform UniformBufferObject {
  mat4 viewProj;
  mat4 lightViewProj;
  vec4 lightDirection_shadowMap;
} ubo;

layout(push_constant) uniform constants {
  mat4 model;
};

float ShadowCalculation(vec4 fragPosLightSpace)
{
    // perform perspective divide
    vec3 projCoords = fragPosLightSpace.xyz / fragPosLightSpace.w;
    // transform to [0,1] range
    projCoords.xy = projCoords.xy * 0.5f + 0.5f;
    // get closest depth value from light's perspective (using [0,1] range fragPosLight as coords)
    float closestDepth = texture(globalSamplers[nonuniformEXT(uint(ubo.lightDirection_shadowMap.w))], projCoords.st).r; 
    // get depth of current fragment from light's perspective
    float currentDepth = projCoords.z;
    // check whether current frag pos is in shadow
    float shadow = currentDepth - 0.005 > closestDepth ? 1.0 : 0.0;

    return shadow;
}

void main() {
  vec3 color = texture(globalSamplers[nonuniformEXT(texId)], inTexCoord.st).rgb;
  vec3 normal = normalize(inNormal);
  vec3 lightColor = vec3(0.3f);
  // ambient
  vec3 ambient = 0.3f * lightColor;
  // diffuse
  vec3 lightDir = normalize(-ubo.lightDirection_shadowMap.xyz);  
  float diff = max(dot(normal, lightDir), 0.f);
  vec3 diffuse = diff * lightColor;

  float shadow = ShadowCalculation(inFragPosLightSpace);
  vec3 lighting = (ambient + (1.0 - shadow) * diffuse) * color;    
  
  outColor = vec4(lighting, 1.f);
}
