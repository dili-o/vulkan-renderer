

#if defined(COMPUTE_GPU_CULLING)

layout(set = MATERIAL_SET, binding = 1) writeonly buffer VisibleMeshInstances
{
	MeshDrawCommand draw_commands[];
};

layout(set = MATERIAL_SET, binding = 11) buffer VisibleMeshCount
{
	uint opaque_mesh_visible_count;
	uint opaque_mesh_culled_count;
	uint transparent_mesh_visible_count;
	uint transparent_mesh_culled_count;

	uint total_count;
	uint depth_pyramid_texture_index;
	uint late_flag;
};

// 2D Polyhedral Bounds of a Clipped, Perspective-Projected 3D Sphere. Michael Mara, Morgan McGuire. 2013
bool project_sphere(vec3 C, float r, float znear, float P00, float P11, out vec4 aabb) {
	if (-C.z - r < znear)
		return false;

	vec2 cx = vec2(C.x, -C.z);
	vec2 vx = vec2(sqrt(dot(cx, cx) - r * r), r);
	vec2 minx = mat2(vx.x, vx.y, -vx.y, vx.x) * cx;
	vec2 maxx = mat2(vx.x, -vx.y, vx.y, vx.x) * cx;

	vec2 cy = -C.yz;
	vec2 vy = vec2(sqrt(dot(cy, cy) - r * r), r);
	vec2 miny = mat2(vy.x, vy.y, -vy.y, vy.x) * cy;
	vec2 maxy = mat2(vy.x, -vy.y, vy.y, vy.x) * cy;

	aabb = vec4(minx.x / minx.y * P00, miny.x / miny.y * P11, maxx.x / maxx.y * P00, maxy.x / maxy.y * P11);
	aabb = aabb.xwzy * vec4(0.5f, -0.5f, 0.5f, -0.5f) + vec4(0.5f); // clip space -> uv space

	return true;
}


layout (local_size_x = 64, local_size_y = 1, local_size_z = 1) in;
void main() {

	uint mesh_instance_index = gl_GlobalInvocationID.x;
	uint count = total_count;


	if (mesh_instance_index < count) {
		uint mesh_draw_index = mesh_instance_draws[mesh_instance_index].mesh_draw_index;

		MeshDraw mesh_draw = mesh_draws[mesh_draw_index];

		vec4 bounding_sphere = mesh_bounds[mesh_draw_index];
		mat4 model = mesh_instance_draws[mesh_instance_index].model;

		// Transform bounding sphere to view space.
		vec4 world_bounding_center = model * vec4(bounding_sphere.xyz, 1);
		vec4 view_bounding_center = freeze_occlusion_camera == 0 ? world_to_camera * world_bounding_center : world_to_camera_debug * world_bounding_center;

    	float scale = length( model[0] );
    	float radius = bounding_sphere.w * scale * 1.1;	// Artificially inflate bounding sphere.

    	bool frustum_visible = true;
	    for ( uint i = 0; i < 6; ++i ) {
	        frustum_visible = frustum_visible && (dot( frustum_planes[i], view_bounding_center) > -radius);
	    }

	    frustum_visible = frustum_visible || (frustum_cull_meshes == 0);

	    uint flags = mesh_draw.flags;
	    if (frustum_visible) {
	    	// Add opaque draws
			if ( /*(flags & (DrawFlags_AlphaMask | DrawFlags_Transparent)) == 0*/ true ) {
				uint draw_index = atomicAdd( opaque_mesh_visible_count, 1 );
	
				draw_commands[draw_index].drawId = mesh_instance_index;
				draw_commands[draw_index].indexCount = 0;
				draw_commands[draw_index].instanceCount = 1;
				draw_commands[draw_index].firstIndex = 0;
				draw_commands[draw_index].vertexOffset = mesh_draw.vertexOffset;
				draw_commands[draw_index].firstInstance = 0;
#if NVIDIA				
				draw_commands[draw_index].taskCount = (mesh_draw.meshlet_count + 31) / 32;
				draw_commands[draw_index].firstTask = (mesh_draw.meshlet_offset) / 32;
#else
				draw_commands[draw_index].x = (mesh_draw.meshlet_count + 31) / 32;
				draw_commands[draw_index].y = 1;
				draw_commands[draw_index].z = 1;
				draw_commands[draw_index].firstTask = (mesh_draw.meshlet_offset) / 32;
#endif // NVIDIA				
			}
	    }
	}
}

#endif