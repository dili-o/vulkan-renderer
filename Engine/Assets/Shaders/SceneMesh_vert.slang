#include "Globals.slang"

struct VSInput {
	float3 pos;
	float3 normal;
	float2 tex_coord;
};

struct VSOutput {
	float4 pos : SV_POSITION;
	float4 world_pos_view_z;
	float3 normal;
	float2 tex_coord;
	nointerpolation uint tex_id;
};

[shader("vertex")]
VSOutput vertMain(VSInput input,
              uniform float4x4 model,
              uniform float4x4 view,
							uint instance_id : SV_VulkanInstanceID) {
	VSOutput output;
  float4 view_pos = mul(view, 
										mul(model, float4(input.pos, 1.f)));
  output.world_pos_view_z = mul(model, float4(input.pos, 1.f));
  output.world_pos_view_z.w = view_pos.z;

  output.normal = input.normal;
  output.tex_coord = input.tex_coord;
  output.tex_id = instance_id;
  output.pos = mul(scene_data.view_proj,
							 mul(model, float4(input.pos, 1.f)));
  return output;
}
