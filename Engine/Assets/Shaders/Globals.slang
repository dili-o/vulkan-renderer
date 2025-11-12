struct SceneData {
	float4x4 view_proj;
  float4x4 light_view_proj; // TODO: DELETE THIS
  float4x4 light_view_projs[4];
  float4 light_dir_shadow_map;
  float4 cascade_splits;
  uint cascade_count;
};

[[vk::binding(0, 0)]]
Sampler2D global_samplers[];

[[vk::binding(0, 1)]]
ConstantBuffer<SceneData> scene_data;
