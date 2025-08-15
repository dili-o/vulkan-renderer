#version 450

#extension GL_GOOGLE_include_directive : require

#include "Mesh.glsl"

layout(set = 0, binding = 0) uniform UniformBufferObject {
    mat4 view_matrix;
    mat4 proj_matrix;
    mat4 view_proj_matrix;

    mat4 prev_view_matrix;

    vec4 view_frustum_planes[6];
};

layout(push_constant) uniform constants
{
  BoundingVolumes bounding_volumes;
  Models models;
  IndexedDrawCommands draw_commands;
  MeshDraws mesh_draws;
  CountBuffer count_buffer;

  uint total_mesh_count;
  bool freeze_camera;
};

layout (local_size_x = 32, local_size_y = 1, local_size_z = 1) in;

void main() {
	uint mesh_id = gl_GlobalInvocationID.x;

	if (mesh_id < total_mesh_count) {
		vec4 bounding_sphere = bounding_volumes.bounding_spheres[mesh_id];
		mat4 model = models.data[mesh_id];

		// Transform bounding sphere to view space.
		vec4 world_bounding_center = model * vec4(bounding_sphere.xyz, 1);
		vec4 view_bounding_center = freeze_camera == true ? 
      prev_view_matrix * world_bounding_center : 
      view_matrix      * world_bounding_center;

    float scale = length( model[0] );
    float radius = bounding_sphere.w * scale * 1.1;	// Artificially inflate bounding sphere.

    bool frustum_visible = true;
    for ( uint i = 0; i < 6; ++i ) {
        frustum_visible = frustum_visible && (dot( view_frustum_planes[i], view_bounding_center) > -radius);
    }

    if (frustum_visible) {
      uint draw_index = atomicAdd(count_buffer.visible_count, 1);
      
      draw_commands.data[draw_index].mesh_id = mesh_id;
      draw_commands.data[draw_index].index_count = mesh_draws.data[mesh_id].primitive_count;
      draw_commands.data[draw_index].instance_count = 1;
      draw_commands.data[draw_index].first_index = mesh_draws.data[mesh_id].index_buffer_offset;
      draw_commands.data[draw_index].vertex_offset = mesh_draws.data[mesh_id].vertex_buffer_offset;
      draw_commands.data[draw_index].first_instance = 0;
    }
	}
}
