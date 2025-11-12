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
  float bias = max(0.05f * (1.f - dot(normal, light_dir)), 0.005f);
  shadow = current_depth - bias > closest_depth ? 1.f : 0.f;

  return shadow;
}

static const float3 colors[4] = {
  float3(1.f, 0.f, 0.f),
  float3(0.f, 1.f, 0.f),
  float3(0.f, 0.f, 1.f),
  float3(1.f, 1.f, 1.f)
};

[shader("fragment")]
float4 fragMain(VSOutput input) {
  uint cascade_index = 0;
	for(uint i = 0; i < scene_data.cascade_count - 1; ++i) {
		if(input.world_pos_view_z.w < scene_data.cascade_splits[i]) {	
			cascade_index = i + 1;
    }
  }
	float3 normal = normalize(input.normal);
  float3 light_dir = normalize(-scene_data.light_dir_shadow_map.xyz);  
  float3 diffuse = colors[cascade_index];

  float shadow = ShadowCalculation(mul(scene_data.light_view_projs[cascade_index],
                                       float4(input.world_pos_view_z.xyz, 1.f)), normal, light_dir, cascade_index);
  float3 lighting = (1.0 - shadow) * diffuse;
  
  return float4(lighting, 1.f);
}
