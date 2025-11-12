#include "Globals.slang"

struct VSInput {
	float3 pos : POSITION0;
	float3 normal : NORMAL;
	float2 tex_coord : TEXCOORD0;
};

struct VSOutput {
	float4 pos : SV_POSITION;
};

[shader("vertex")]
VSOutput vertMain(VSInput input,
                  uniform float4x4 model,
                  uniform uint cascade_level) {
	VSOutput output;
	// TODO: Temporary solution to force the compiler to include normal and tex_coord
	float3 norm = input.normal + float3(input.tex_coord, 1.f);
	output.pos = mul(scene_data.light_view_projs[cascade_level],
						 mul(model, float4(input.pos.xyz, 1.f)));
	output.pos.xyz -= norm; // dummy use
	output.pos.xyz += norm; // dummy use
	return output;
}

struct GSOutput {
	float4 pos : SV_POSITION;
	uint layer : SV_RenderTargetArrayIndex;
};

[shader("geometry")]
[instance(4)]
[maxvertexcount(3)]
void geomMain(triangle VSOutput input[3],
              inout TriangleStream<GSOutput> out_stream,
              uint invocation_id : SV_GSInstanceID) {
	for (int i = 0; i < 3; ++i) {
		GSOutput output;
    output.pos = mul(scene_data.light_view_projs[invocation_id], input[i].pos);
    output.layer = invocation_id;
    out_stream.Append(output);
  }
  out_stream.RestartStrip();
}
