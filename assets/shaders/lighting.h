
#ifndef HELIX_GLSL_LIGHTING_H
#define HELIX_GLSL_LIGHTING_H

struct PointLight {
  vec4 position;

  float range;
  float intensity;
  float padding_[2];
};

vec4 calculate_lighting_point(vec4 base_colour, vec3 rmo, vec3 normal,
                              vec3 emissive, vec3 vPosition, PointLight light) {
  vec3 V = normalize(eye.xyz - vPosition);
  vec3 L = normalize(light.position.xyz - vPosition);
  vec3 N = normal;
  vec3 H = normalize(L + V);

  float roughness = rmo.r;
  float metalness = rmo.g;
  float occlusion = rmo.b;

  float alpha = pow(roughness, 2.0);

  // https://www.khronos.org/registry/glTF/specs/2.0/glTF-2.0.html#specular-brdf
  float NdotH = clamp(dot(N, H), 0, 1);
  float alpha_squared = alpha * alpha;
  float d_denom = (NdotH * NdotH) * (alpha_squared - 1.0) + 1.0;
  float distribution =
      (alpha_squared * heaviside(NdotH)) / (PI * d_denom * d_denom);

  float NdotL = clamp(dot(N, L), 0, 1);
  float NdotV = clamp(dot(N, V), 0, 1);
  float HdotL = clamp(dot(H, L), 0, 1);
  float HdotV = clamp(dot(H, V), 0, 1);

  float distance = length(light.position.xyz - vPosition);
  float intensity = light.intensity *
                    max(min(1.0 - pow(distance / light.range, 4.0), 1.0), 0.0) /
                    pow(distance, 2.0);

  vec3 material_colour = vec3(0, 0, 0);
  if (NdotL > 0.0 || NdotV > 0.0) {
    float visibility =
        (heaviside(HdotL) /
         (abs(NdotL) +
          sqrt(alpha_squared + (1.0 - alpha_squared) * (NdotL * NdotL)))) *
        (heaviside(HdotV) /
         (abs(NdotV) +
          sqrt(alpha_squared + (1.0 - alpha_squared) * (NdotV * NdotV))));

    float specular_brdf = intensity * NdotL * visibility * distribution;

    vec3 diffuse_brdf = intensity * NdotL * (1 / PI) * base_colour.rgb;

    // NOTE(marco): f0 in the formula notation refers to the base colour here
    vec3 conductor_fresnel =
        specular_brdf *
        (base_colour.rgb + (1.0 - base_colour.rgb) * pow(1.0 - abs(HdotV), 5));

    // NOTE(marco): f0 in the formula notation refers to the value derived from
    // ior = 1.5
    float f0 = 0.04;  // pow( ( 1 - ior ) / ( 1 + ior ), 2 )
    float fr = f0 + (1 - f0) * pow(1 - abs(HdotV), 5);
    vec3 fresnel_mix = mix(diffuse_brdf, vec3(specular_brdf), fr);

    material_colour = mix(fresnel_mix, conductor_fresnel, metalness);
  }

  material_colour += emissive;

  return vec4(encode_srgb(material_colour), base_colour.a);
}

vec4 calculate_lighting_directional(vec4 base_colour, vec3 rmo, vec3 normal,
                                    vec3 emissive, vec3 vPosition,
                                    vec4 direction_intensity) {
  vec3 V = normalize(eye.xyz - vPosition);
  vec3 L = normalize(-direction_intensity.xyz);
  vec3 N = normal;
  vec3 H = normalize(L + V);

  float roughness = rmo.r;
  float metalness = rmo.g;
  float occlusion = rmo.b;

  float alpha = pow(roughness, 2.0);

  // https://www.khronos.org/registry/glTF/specs/2.0/glTF-2.0.html#specular-brdf
  float NdotH = clamp(dot(N, H), 0, 1);
  float alpha_squared = alpha * alpha;
  float d_denom = (NdotH * NdotH) * (alpha_squared - 1.0) + 1.0;
  float distribution =
      (alpha_squared * heaviside(NdotH)) / (PI * d_denom * d_denom);

  float NdotL = clamp(dot(N, L), 0, 1);
  float NdotV = clamp(dot(N, V), 0, 1);
  float HdotL = clamp(dot(H, L), 0, 1);
  float HdotV = clamp(dot(H, V), 0, 1);

  float intensity = direction_intensity.w;

  vec3 material_colour = vec3(0, 0, 0);
  if (NdotL > 0.0 || NdotV > 0.0) {
    float visibility =
        (heaviside(HdotL) /
         (abs(NdotL) +
          sqrt(alpha_squared + (1.0 - alpha_squared) * (NdotL * NdotL)))) *
        (heaviside(HdotV) /
         (abs(NdotV) +
          sqrt(alpha_squared + (1.0 - alpha_squared) * (NdotV * NdotV))));

    vec3 light_color = vec3(1.f, 0.956f, 0.878f);
    // vec3 light_color = vec3(1.f, 0.74f, 0.35f);

    float specular_brdf = intensity * NdotL * visibility * distribution;

    // vec3 diffuse_brdf = intensity * NdotL * (1 / PI) * base_colour.rgb;
    vec3 diffuse_brdf =
        intensity * NdotL * (1 / PI) * base_colour.rgb * light_color;

    // NOTE(marco): f0 in the formula notation refers to the base colour here
    vec3 conductor_fresnel =
        specular_brdf * light_color *
        (base_colour.rgb + (1.0 - base_colour.rgb) * pow(1.0 - abs(HdotV), 5));

    // NOTE(marco): f0 in the formula notation refers to the value derived from
    // ior = 1.5
    float f0 = 0.04;  // pow( ( 1 - ior ) / ( 1 + ior ), 2 )
    float fr = f0 + (1 - f0) * pow(1 - abs(HdotV), 5);
    vec3 fresnel_mix = mix(diffuse_brdf, light_color * vec3(specular_brdf), fr);

    material_colour = mix(fresnel_mix, conductor_fresnel, metalness);
  }

  material_colour += emissive;

  return vec4(encode_srgb(material_colour), base_colour.a);
}
#endif  // HELIX_GLSL_LIGHTING_H
