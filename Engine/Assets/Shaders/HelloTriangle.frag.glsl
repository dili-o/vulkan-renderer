#version 450

#extension GL_GOOGLE_include_directive : require
#include "Globals.glsl"

layout(location = 0) in vec4 inWorldPos_ViewZ;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;
layout(location = 3) flat in uint texId;

layout(location = 0) out vec4 outColor;

layout(set = 1, binding = 0) uniform UniformBufferObject {
  mat4 viewProj;
  mat4 lightViewProj;
  mat4 lightViewProjs[4];
  vec4 lightDirection_shadowMap;
  vec4 cascadeSplits;
} ubo;

layout(push_constant) uniform constants {
  mat4 model;
  mat4 view;
};

float ShadowCalculation(vec4 fragPosLightSpace, vec3 normal, vec3 lightDir, uint cascadeIndex) {
  uint shadowMap = uint(ubo.lightDirection_shadowMap.w) + cascadeIndex;

  // perform perspective divide
  vec3 projCoords = fragPosLightSpace.xyz / fragPosLightSpace.w;
  // transform to [0,1] range
  projCoords.xy = projCoords.xy * 0.5f + 0.5f;
  if(projCoords.z > 1.f)
    return 0.f;

  float closestDepth = texture(globalSamplers[nonuniformEXT(shadowMap)], projCoords.xy).r; 
  float currentDepth = projCoords.z;
  float bias = max(0.0005f * (1.f - dot(normal, lightDir)) * (1.f + cascadeIndex), 0.0005f);
  float shadow = 0.f;
  vec2 texelSize = 1.f / textureSize(globalSamplers[nonuniformEXT(shadowMap)], 0);
  for(int x = -1; x <= 1; ++x) {
    for(int y = -1; y <= 1; ++y) {
      float pcfDepth = texture(globalSamplers[nonuniformEXT(shadowMap)],
                               projCoords.xy + vec2(x, y) * texelSize).r; 
      shadow += currentDepth - bias > pcfDepth  ? 1.f : 0.f;        
    }    
  }
  shadow /= 9.f;
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

  uint cascadeIndex = 0;
	for(uint i = 0; i < 4 - 1; ++i) {
		if(inWorldPos_ViewZ.w < ubo.cascadeSplits[i]) {	
			cascadeIndex = i + 1;
    }
  }

  float shadow = ShadowCalculation(ubo.lightViewProjs[cascadeIndex] * vec4(inWorldPos_ViewZ.xyz, 1.f), normal, lightDir, cascadeIndex);
  vec3 lighting = (ambient + (1.0 - shadow) * diffuse) * color;    
  
  outColor = vec4(lighting, 1.f);
}
