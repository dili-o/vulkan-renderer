
#if defined(MESH)

#define DEBUG 0

#define CULL 1

layout(local_size_x = 32, local_size_y = 1, local_size_z = 1) in;
layout(triangles, max_vertices = 64, max_primitives = 124) out;

struct TaskData
{
    uint meshletIndices[32];
};
taskPayloadSharedEXT TaskData td;

layout (location = 0) out vec2 vTexcoord0[];
layout (location = 1) out vec4 vNormal_BiTanX[];
layout (location = 2) out vec4 vTangent_BiTanY[];
layout (location = 3) out vec4 vPosition_BiTanZ[];
layout (location = 4) out flat uint mesh_draw_index[];

#if DEBUG
layout (location = 5) out vec4 vColour[];
#endif

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

    MaterialData material = material_data[ meshlets[meshlet_index].mesh_index ];

    uint vertexCount = uint(meshlets[meshlet_index].vertexCount);
    uint triangleCount = uint(meshlets[meshlet_index].triangleCount);
    uint indexCount = triangleCount * 3;

    if(task_invo == 0){
        SetMeshOutputsEXT(vertexCount, triangleCount);
    }

    uint dataOffset = meshlets[meshlet_index].dataOffset;
    uint vertexOffset = dataOffset;
    uint indexOffset = dataOffset + vertexCount;

    bool has_normals = (material.flags & DrawFlags_HasNormals) != 0;
    bool has_tangents = (material.flags & DrawFlags_HasTangents) != 0;

    float i8_inverse = 1.0 / 127.0;

#if DEBUG
    uint mhash = hash(meshlet_index);
    vec3 mcolor = vec3(float(mhash & 255), float((mhash >> 8) & 255), float((mhash >> 16) & 255)) / 255.0;
#endif

#if defined(MESH_TRANSPARENT_NO_CULL)
    uint mesh_instance_index = draw_commands[gl_DrawIDARB + total_opaque_mesh_count].drawId;
#else
    uint mesh_instance_index = draw_commands[gl_DrawIDARB].drawId;
#endif

    mat4 model = mesh_instance_draws[mesh_instance_index].model;
    mat4 model_inverse = mesh_instance_draws[mesh_instance_index].model_inverse;

    // TODO: if we have meshlets with 62 or 63 vertices then we pay a small penalty for branch divergence here - we can instead redundantly xform the last vertex
    for (uint i = task_invo; i < vertexCount; i += 32)
    {
        uint vi = meshletData[vertexOffset + i];

        vec3 position = vec3(vertex_positions[vi].v.x, vertex_positions[vi].v.y, vertex_positions[vi].v.z);

        if ( has_normals ) {
            vec3 normal = vec3(int(vertex_data[vi].nx), int(vertex_data[vi].ny), int(vertex_data[vi].nz)) * i8_inverse - 1.0;
            vNormal_BiTanX[ i ].xyz = normalize( mat3(model_inverse) * normal );
        }

        if ( has_tangents ) {
            vec3 tangent = vec3(int(vertex_data[vi].tx), int(vertex_data[vi].ty), int(vertex_data[vi].tz)) * i8_inverse - 1.0;
            vTangent_BiTanY[ i ].xyz = normalize( mat3(model) * tangent.xyz );

            vec3 bitangent = cross( vNormal_BiTanX[ i ].xyz, tangent.xyz ) * ( int(vertex_data[vi].tw) * i8_inverse  - 1.0 );
            vNormal_BiTanX[ i ].w = bitangent.x;
            vTangent_BiTanY[ i ].w = bitangent.y;
            vPosition_BiTanZ[ i ].w = bitangent.z;
        }

        vTexcoord0[i] = vec2(vertex_data[vi].tu, vertex_data[vi].tv);

        gl_MeshVerticesEXT[ i ].gl_Position = view_projection * (model * vec4(position, 1));

        vec4 worldPosition = model * vec4(position, 1.0);
        vPosition_BiTanZ[ i ].xyz = worldPosition.xyz;

        mesh_draw_index[ i ] = meshlets[meshlet_index].mesh_index;


#if DEBUG
        vColour[i] = vec4(mcolor, 1.0);
#endif
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
