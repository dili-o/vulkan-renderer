

#if defined (TASK)

layout(set = MATERIAL_SET, binding = 9) buffer PointLightData
{
	PointLight pointLights[];
};

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

    uint mesh_instance_index = draw_commands[gl_DrawIDARB].drawId;
    uint task_group = gl_WorkGroupID.x + draw_commands[gl_DrawIDARB].firstTask;

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
    accept = !coneCull(world_center.xyz, radius, cone_axis, cone_cutoff,
        directional_light_data.position_enabled.xyz);
    view_center = directional_light_data.view * world_center;

    bool frustum_visible = true;
    for ( uint i = 0; i < 6; ++i ) {
        float distance = dot( directional_light_data.frustum_planes[i], view_center) + directional_light_data.frustum_planes[i].w;
      //frustum_visible = frustum_visible && distance > -radius;
    }

    // TODO: Maybe add occlusion culling
    accept = accept && frustum_visible;

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

