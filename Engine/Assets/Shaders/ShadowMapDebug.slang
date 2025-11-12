#include "Globals.slang"

struct VSOutput {
	float4 pos : SV_POSITION;
	float2 tex_coord;
	nointerpolation uint tex_id;
};

[shader("vertex")]
VSOutput vertMain(uint v_index: SV_VulkanVertexID,
                  uint instance_id: SV_VulkanInstanceID,
                  uniform uint pc) {
	VSOutput output;
  output.tex_coord = float2((v_index << 1) & 2, v_index & 2);
  output.pos = float4(output.tex_coord * 2.f - 1.f, 0.f, 1.f);
  output.pos.y *= -1.f;

  output.tex_id = instance_id;
  return output;
}


[shader("fragment")]
float4 fragMain(VSOutput input) {
  float2 tex_coord = input.tex_coord;
  tex_coord.y = 1.f - tex_coord.y;
  float depth_value = global_samplers[NonUniformResourceIndex(input.tex_id)].Sample(tex_coord).r;
  return float4(float3(depth_value), 1.f);
}
