#include "Globals.slang"

struct VSOutput {
	float4 pos : SV_POSITION;
	float4 world_pos_view_z;
	float3 normal;
	float2 tex_coord;
	nointerpolation uint tex_id;
};

float ShadowCalculation(float4 frag_pos_light_space,
                        float3 normal, float3 light_dir, uint cascade_index) {
  uint shadow_map = uint(scene_data.light_dir_shadow_map.w) + cascade_index;

  // perform perspective divide
  float3 proj_coords = frag_pos_light_space.xyz / frag_pos_light_space.w;
  // transform to [0,1] range
  proj_coords.xy = proj_coords.xy * 0.5f + 0.5f;
  if(proj_coords.z > 1.f)
    return 0.f;

  float closest_depth = global_samplers[NonUniformResourceIndex(shadow_map)].Sample(proj_coords.xy).r; 
  float current_depth = proj_coords.z;
  float shadow = 0.f;
#ifdef ENABLE_PCF
  float bias = max(0.0005f * (1.f - dot(normal, light_dir)) * (1.f + cascade_index), 0.0005f);
  float2 texture_size;
  global_samplers[NonUniformResourceIndex(shadow_map)].GetDimensions(texture_size.x, texture_size.y);
  float2 texel_size = 1.f / texture_size;
  for(int x = -4; x <= 4; ++x) {
    for(int y = -4; y <= 4; ++y) {
      float pcf_depth = global_samplers[NonUniformResourceIndex(shadow_map)]
                           .Sample(proj_coords.xy + float2(x, y) * texel_size).r; 
      shadow += current_depth - bias > pcf_depth  ? 1.f : 0.f;        
    }    
  }
  shadow /= 81.f;
#else
  float bias = max(0.05f * (1.f - dot(normal, light_dir)), 0.005f);
  shadow = current_depth - bias > closest_depth ? 1.f : 0.f;
#endif // ENABLE_PCF

  return shadow;
}

[shader("fragment")]
float4 fragMain(VSOutput input) {
  float3 color = global_samplers[NonUniformResourceIndex(input.tex_id)].Sample(input.tex_coord.xy).xyz;
  float3 normal = normalize(input.normal);
  float3 light_color = float3(0.3f);
  // ambient
  float3 ambient = 0.3f * light_color;
  // diffuse
  float3 light_dir = normalize(-scene_data.light_dir_shadow_map.xyz);  
  float diff = max(dot(normal, light_dir), 0.f);
  float3 diffuse = diff * light_color;

  uint cascade_index = 0;
	for(uint i = 0; i < scene_data.cascade_count - 1; ++i) {
		if(input.world_pos_view_z.w < scene_data.cascade_splits[i]) {	
			cascade_index = i + 1;
    }
  }

  float shadow = ShadowCalculation(mul(scene_data.light_view_projs[cascade_index],
                                       float4(input.world_pos_view_z.xyz, 1.f)), normal, light_dir, cascade_index);
  float3 lighting = (ambient + (1.0 - shadow) * diffuse) * color;    
  
  return float4(lighting, 1.f);
}
