#include "Globals.slang"

float3 ndc_to_world_space(float4x4 inv_proj_view, float x, float y,
                             float z) {
  float4 ndc_point = float4(x, y, z, 1.0f);
  float4 world_point = mul(inv_proj_view, ndc_point);
  return world_point.xyz / world_point.w;
}

static const float4 positions [] = {
  float4(-1.f, 1.f, 0.f, 1.f),  // Near Top Left
  float4(1.f, 1.f, 0.f, 1.f),   // Near Top Right

  float4(1.f, 1.f, 0.f, 1.f),   // Near Top Right
  float4(1.f, -1.f, 0.f, 1.f),  // Near Bottom Right

  float4(1.f, -1.f, 0.f, 1.f),  // Near Bottom Right
  float4(-1.f, -1.f, 0.f, 1.f), // Near Bottom Left
                               
  float4(-1.f, -1.f, 0.f, 1.f), // Near Bottom Left
  float4(-1.f, 1.f, 0.f, 1.f),  // Near Top Left
  //
  //
  float4(-1.f, 1.f, 0.f, 1.f),  // Near Top Left
  float4(-1.f, 1.f, 1.f, 1.f),   // Far Top Left

  float4(1.f, 1.f, 0.f, 1.f),   // Near Top Right
  float4(1.f, 1.f, 1.f, 1.f),    // Far Top Right

  float4(1.f, -1.f, 0.f, 1.f),  // Near Bottom Right
  float4(1.f, -1.f, 1.f, 1.f),   // Far Bottom Right
                               
  float4(-1.f, -1.f, 0.f, 1.f), // Near Bottom Left
  float4(-1.f, -1.f, 1.f, 1.f),  // Far Bottom Left
  //
  //
  float4(-1.f, 1.f, 1.f, 1.f),  // Far Top Left
  float4(1.f, 1.f, 1.f, 1.f),   // Far Top Right

  float4(1.f, 1.f, 1.f, 1.f),   // Far Top Right
  float4(1.f, -1.f, 1.f, 1.f),  // Far Bottom Right

  float4(1.f, -1.f, 1.f, 1.f),  // Far Bottom Right
  float4(-1.f, -1.f, 1.f, 1.f), // Far Bottom Left
                               
  float4(-1.f, -1.f, 1.f, 1.f), // Far Bottom Left
  float4(-1.f, 1.f, 1.f, 1.f),  // Far Top Left
};

struct PushConstant {
  float4x4 inv_view_proj;
  float4 color;
};

struct VSOutput {
	float4 pos : SV_POSITION;
	float4 color;
};

[shader("vertex")]
VSOutput vertMain(uint v_index: SV_VulkanVertexID, uniform PushConstant pc) {
	VSOutput output;
  float4 position = positions[v_index];
  output.pos = mul(scene_data.view_proj,
                   float4(ndc_to_world_space(pc.inv_view_proj, position.x, position.y, position.z), 1.f));
  output.color = pc.color;
  return output;
}

[shader("fragment")]
float4 fragMain(VSOutput input) {
  return input.color;
}

