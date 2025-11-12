#include "Globals.slang"

struct VSInput {
	float2 pos;
	float2 uv;
	float4 color_UNORM; // NOTE: This should be changed to a uint
};

struct VSOutput {
	float4 pos : SV_POSITION;
	float4 frag_color;
	float2 frag_uv;
	nointerpolation uint texture_id;
};

[shader("vertex")]
VSOutput vertMain(VSInput input,
									uniform float4 scale_translate,
									uint instance_id: SV_VulkanInstanceID) {
	VSOutput output;
  output.frag_uv = input.uv;
  output.frag_color = input.color_UNORM;
  output.texture_id = instance_id;
  float2 scale = scale_translate.xy;
  float2 translate = scale_translate.zw;
  output.pos = float4(input.pos * scale + translate, 0.f, 1.f);

  return output;
}

[shader("fragment")]
float4 fragMain(VSOutput input) {
	return global_samplers[NonUniformResourceIndex(input.texture_id)].Sample(input.frag_uv.xy) * input.frag_color;
}
