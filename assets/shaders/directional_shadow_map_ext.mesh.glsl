

#if defined(MESH)

layout(set = MATERIAL_SET, binding = 9) buffer PointLightData
{
	PointLight pointLights[];
};

layout(local_size_x = 32, local_size_y = 1, local_size_z = 1) in;
layout(triangles, max_vertices = 64, max_primitives = 124) out;

struct TaskData
{
    uint meshletIndices[32];
};
taskPayloadSharedEXT TaskData td;

uint hash(uint a)
{
   a = (a+0x7ed55d16) + (a<<12);
   a = (a^0xc761c23c) ^ (a>>19);
   a = (a+0x165667b1) + (a<<5);
   a = (a+0xd3a2646c) ^ (a<<9);
   a = (a+0xfd7046c5) + (a<<3);
   a = (a^0xb55a4f09) ^ (a>>16);
   return a;
}

void main()
{
    uint task_invo = gl_LocalInvocationID.x;
    uint meshlet_index = td.meshletIndices[gl_WorkGroupID.x];

    uint vertexCount = uint(meshlets[meshlet_index].vertexCount);
    uint triangleCount = uint(meshlets[meshlet_index].triangleCount);
    uint indexCount = triangleCount * 3;

    if(task_invo == 0){
        SetMeshOutputsEXT(vertexCount, triangleCount);
    }

    uint dataOffset = meshlets[meshlet_index].dataOffset;
    uint vertexOffset = dataOffset;
    uint indexOffset = dataOffset + vertexCount;

    float i8_inverse = 1.0 / 127.0;


    uint mesh_instance_index = draw_commands[gl_DrawIDARB].drawId;

    mat4 model = mesh_instance_draws[mesh_instance_index].model;
    mat4 model_inverse = mesh_instance_draws[mesh_instance_index].model_inverse;

    // TODO: if we have meshlets with 62 or 63 vertices then we pay a small penalty for branch divergence here - we can instead redundantly xform the last vertex
    for (uint i = task_invo; i < vertexCount; i += 32)
    {
        uint vi = meshletData[vertexOffset + i];

        vec3 position = vec3(vertex_positions[vi].v.x, vertex_positions[vi].v.y, vertex_positions[vi].v.z);

        gl_MeshVerticesEXT[ i ].gl_Position = directional_light_data.projection * directional_light_data.view * (model * vec4(position, 1));
    }

    for (uint i = 0; i < uint(meshlets[meshlet_index].triangleCount); ++i)
	{
    uint triangle = meshletData[indexOffset + i];
		gl_PrimitiveTriangleIndicesEXT[i] = uvec3(
        (triangle >> 16) & 0xff,
        (triangle >> 8) & 0xff,
        (triangle) & 0xff );
	}
}

#endif // MESH
