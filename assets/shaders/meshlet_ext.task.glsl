
#if defined (TASK)

#define DEBUG 0

#define CULL 1

layout(local_size_x = 32, local_size_y = 1, local_size_z = 1) in;

struct TaskData
{
    uint meshletIndices[32];
};
taskPayloadSharedEXT TaskData td;

// NOTE(marco): as described in meshoptimizer.h
bool coneCull(vec3 center, float radius, vec3 cone_axis, float cone_cutoff, vec3 camera_position)
{
    return dot(center - camera_position, cone_axis) >= cone_cutoff * length(center - camera_position) + radius;
}

void main()
{
    uint task_invo = gl_LocalInvocationID.x;

    

#if defined(TASK_TRANSPARENT_NO_CULL)
    uint mesh_instance_index = draw_commands[gl_DrawIDARB + total_opaque_mesh_count].drawId;
    uint task_group = gl_WorkGroupID.x + draw_commands[gl_DrawIDARB + total_opaque_mesh_count].firstTask;

#else
    uint mesh_instance_index = draw_commands[gl_DrawIDARB].drawId;
    uint task_group = gl_WorkGroupID.x + draw_commands[gl_DrawIDARB].firstTask;

#endif
    mat4 model = mesh_instance_draws[mesh_instance_index].model;

    uint meshlet_index = task_group * 32 + task_invo;
#if CULL
    vec4 world_center = model * vec4(meshlets[meshlet_index].center, 1);
    float scale = length( model[0] );
    float radius = meshlets[meshlet_index].radius * scale * 1.1;   // Artificially inflate bounding sphere.
    vec3 cone_axis = mat3( model ) * vec3(int(meshlets[meshlet_index].cone_axis[0]) / 127.0, int(meshlets[meshlet_index].cone_axis[1]) / 127.0, int(meshlets[meshlet_index].cone_axis[2]) / 127.0);
    float cone_cutoff = int(meshlets[meshlet_index].cone_cutoff) / 127.0;

    bool accept = false;
    vec4 view_center = vec4(0);
    // Backface culling and move meshlet in camera space
    if ( freeze_occlusion_camera == 0 ) {
        accept = !coneCull(world_center.xyz, radius, cone_axis, cone_cutoff, eye.xyz);
        view_center = world_to_camera * world_center;
    } else {
        accept = !coneCull(world_center.xyz, radius, cone_axis, cone_cutoff, eye_debug.xyz);
        view_center = world_to_camera_debug * world_center;
    }

    bool frustum_visible = true;
    for ( uint i = 0; i < 6; ++i ) {
        frustum_visible = frustum_visible && (dot( frustum_planes[i], view_center) > -radius);
    }

    //frustum_visible = frustum_visible || (frustum_cull_meshlets == 0);
    bool occlusion_visible = true;
    if ( frustum_visible ) {
        vec4 aabb;
        if ( project_sphere(view_center.xyz, radius, z_near, projection_00, projection_11, aabb ) ) {
            // TODO: improve
            ivec2 depth_pyramid_size = textureSize(global_textures[nonuniformEXT(depth_pyramid_texture_index)], 0);
            float width = (aabb.z - aabb.x) * depth_pyramid_size.x;
            float height = (aabb.w - aabb.y) * depth_pyramid_size.y;

            float level = floor(log2(max(width, height)));

            // Sampler is set up to do max reduction, so this computes the minimum depth of a 2x2 texel quad
            vec2 uv = (aabb.xy + aabb.zw) * 0.5;
            uv.y = 1 - uv.y;

            // Depth is raw, 0..1 space.
            float depth = textureLod(global_textures[nonuniformEXT(depth_pyramid_texture_index)], uv, level).r;
            // Sample also 4 corners
            depth = max(depth, textureLod(global_textures[nonuniformEXT(depth_pyramid_texture_index)], vec2(aabb.x, 1.0f - aabb.y), level).r);
            depth = max(depth, textureLod(global_textures[nonuniformEXT(depth_pyramid_texture_index)], vec2(aabb.z, 1.0f - aabb.w), level).r);
            depth = max(depth, textureLod(global_textures[nonuniformEXT(depth_pyramid_texture_index)], vec2(aabb.x, 1.0f - aabb.w), level).r);
            depth = max(depth, textureLod(global_textures[nonuniformEXT(depth_pyramid_texture_index)], vec2(aabb.z, 1.0f - aabb.y), level).r);

            vec3 dir = freeze_occlusion_camera == 0 ? normalize(eye.xyz - world_center.xyz) : normalize(eye_debug.xyz - world_center.xyz);
            mat4 view_projection_m = freeze_occlusion_camera == 0 ? previous_view_projection : view_projection_debug;
            vec4 sceen_space_center_last = view_projection_m * vec4(world_center.xyz + dir * radius, 1.0);

            float depth_sphere = sceen_space_center_last.z / sceen_space_center_last.w;

            occlusion_visible = (depth_sphere <= depth);
        }
    }

    accept = accept && frustum_visible && occlusion_visible;

    uvec4 ballot = subgroupBallot(accept); // Gets all the invocations in the subgroup with a visible meshlet

    uint index = subgroupBallotExclusiveBitCount(ballot);

    if (accept)
        td.meshletIndices[index] = meshlet_index;

    uint count = subgroupBallotBitCount(ballot);

    if (task_invo == 0)
        EmitMeshTasksEXT(count, 1, 1);
#else
    td.meshletIndices[task_invo] = meshlet_index;

    if (task_invo == 0)
        EmitMeshTasksEXT(32, 1, 1);
#endif // CULL
}
#endif // TASK

