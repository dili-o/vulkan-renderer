
#define DEBUG 0

layout(set = MATERIAL_SET, binding = 9) buffer LightData
{
  Light lights[];
};

#if defined (TASK)

#define CULL 1

layout(local_size_x = 32, local_size_y = 1, local_size_z = 1) in;

out taskNV block
{
    uint meshletIndices[32];
};

// NOTE(marco): as described in meshoptimizer.h
bool coneCull(vec3 center, float radius, vec3 cone_axis, float cone_cutoff, vec3 camera_position)
{
    return dot(center - camera_position, cone_axis) >= cone_cutoff * length(center - camera_position) + radius;
}

void main()
{
    uint task_invo = gl_LocalInvocationID.x;
    uint task_group = gl_WorkGroupID.x;

    uint meshlet_index = task_group * 32 + task_invo;

#if defined(TASK_TRANSPARENT_NO_CULL)
    uint mesh_instance_index = draw_commands[gl_DrawIDARB + total_opaque_mesh_count].drawId;
#else
    uint mesh_instance_index = draw_commands[gl_DrawIDARB].drawId;
#endif
    mat4 model = mesh_instance_draws[mesh_instance_index].model;

#if CULL
    vec4 world_center = model * vec4(meshlets[meshlet_index].center, 1);
    float scale = length( model[0] );
    float radius = meshlets[meshlet_index].radius * scale;   // Artificially inflate bounding sphere.
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
            mat4 view_projection_m = freeze_occlusion_camera == 0 ? view_projection : view_projection_debug;
            vec4 sceen_space_center_last = view_projection_m * vec4(world_center.xyz + dir * radius, 1.0);

            float depth_sphere = sceen_space_center_last.z / sceen_space_center_last.w;

            occlusion_visible = (depth_sphere <= depth);
        }
    }

    accept = accept && frustum_visible && occlusion_visible;

    uvec4 ballot = subgroupBallot(accept); // Gets all the invocations in the subgroup with a visible meshlet

    uint index = subgroupBallotExclusiveBitCount(ballot);

    if (accept)
        meshletIndices[index] = meshlet_index;

    uint count = subgroupBallotBitCount(ballot);

    if (task_invo == 0)
        gl_TaskCountNV = count;
#else
    meshletIndices[task_invo] = meshlet_index;

    if (task_invo == 0)
        gl_TaskCountNV = 32;
#endif
}

#endif // TASK


#if defined(MESH_GBUFFER_CULLING) || defined(MESH_MESH) || defined(MESH_TRANSPARENT_NO_CULL)

layout(local_size_x = 32, local_size_y = 1, local_size_z = 1) in;
layout(triangles, max_vertices = 64, max_primitives = 124) out;

in taskNV block
{
    uint meshletIndices[32];
};

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
    uint meshlet_index = meshletIndices[gl_WorkGroupID.x];

    MaterialData material = material_data[ meshlets[meshlet_index].mesh_index ];

    uint vertexCount = uint(meshlets[meshlet_index].vertexCount);
    uint triangleCount = uint(meshlets[meshlet_index].triangleCount);
    uint indexCount = triangleCount * 3;

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

        gl_MeshVerticesNV[ i ].gl_Position = view_projection * (model * vec4(position, 1));

        vec4 worldPosition = model * vec4(position, 1.0);
        vPosition_BiTanZ[ i ].xyz = worldPosition.xyz;

        mesh_draw_index[ i ] = meshlets[meshlet_index].mesh_index;


#if DEBUG
        vColour[i] = vec4(mcolor, 1.0);
#endif
    }

    uint indexGroupCount = (indexCount + 3) / 4;

    for (uint i = task_invo; i < indexGroupCount; i += 32)
    {
        writePackedPrimitiveIndices4x8NV(i * 4, meshletData[indexOffset + i]);
    }

    if (task_invo == 0)
        gl_PrimitiveCountNV = uint(meshlets[meshlet_index].triangleCount);
}

#endif // MESH

#if defined(FRAGMENT_GBUFFER_CULLING) || defined(FRAGMENT_MESH)

layout (location = 0) in vec2 vTexcoord0;
layout (location = 1) in vec4 vNormal_BiTanX;
layout (location = 2) in vec4 vTangent_BiTanY;
layout (location = 3) in vec4 vPosition_BiTanZ;
layout (location = 4) in flat uint mesh_draw_index;

#if DEBUG
layout (location = 5) in vec4 vColour;
#endif

layout (location = 0) out vec4 color_out;
layout (location = 1) out vec2 normal_out;
layout (location = 2) out vec4 roughness_metallic_occlusion_out;

void main() {
    MaterialData material = material_data[mesh_draw_index];
    uint flags = material.flags;

    vec3 world_position = vPosition_BiTanZ.xyz;
    vec3 normal = normalize(vNormal_BiTanX.xyz);
    if ( (flags & DrawFlags_HasNormals) == 0 ) {
        normal = normalize(cross(dFdx(world_position), dFdy(world_position)));
    }

    vec3 tangent = normalize(vTangent_BiTanY.xyz);
    vec3 bitangent = normalize(vec3(vNormal_BiTanX.w, vTangent_BiTanY.w, vPosition_BiTanZ.w));
    if ( (flags & DrawFlags_HasTangents) == 0 ) {
        vec3 uv_dx = dFdx(vec3(vTexcoord0, 0.0));
        vec3 uv_dy = dFdy(vec3(vTexcoord0, 0.0));

        // NOTE(marco): code taken from https://github.com/KhronosGroup/glTF-Sample-Viewer
        vec3 t_ = (uv_dy.t * dFdx(world_position) - uv_dx.t * dFdy(world_position)) /
                  (uv_dx.s * uv_dy.t - uv_dy.s * uv_dx.t);
        tangent = normalize(t_ - normal * dot(normal, t_));

        bitangent = cross( normal, tangent );
    }

    uvec4 textures = material.textures;
    vec4 base_colour = material.base_color_factor;
    if (textures.x != INVALID_TEXTURE_INDEX) {
        vec3 texture_colour = decode_srgb( texture(global_textures[nonuniformEXT(textures.x)], vTexcoord0).rgb );
        base_colour *= vec4( texture_colour, 1.0 );
    }

    bool useAlphaMask = (flags & DrawFlags_AlphaMask) != 0;
    if (useAlphaMask && base_colour.a < material.alpha_cutoff) {
        discard;
    }

    bool use_alpha_dither = (flags & DrawFlags_AlphaDither) != 0;
    if ( use_alpha_dither ) {
        float dithered_alpha = dither(gl_FragCoord.xy, base_colour.a);
        if (dithered_alpha < 0.001f) {
            discard;
        }
    }

    if (gl_FrontFacing == false)
    {
        tangent *= -1.0;
        bitangent *= -1.0;
        normal *= -1.0;
    }

    if (textures.z != INVALID_TEXTURE_INDEX) {
        // NOTE(marco): normal textures are encoded to [0, 1] but need to be mapped to [-1, 1] value
        vec3 bump_normal = normalize( texture(global_textures[nonuniformEXT(textures.z)], vTexcoord0).rgb * 2.0 - 1.0 );
        mat3 TBN = mat3(
            tangent,
            bitangent,
            normal
        );

        normal = normalize(TBN * normalize(bump_normal));
    }

    // Encode then convert from [-1, 1] -> [0, 1]
    normal_out.xy = octahedral_encode(normal);
    normal_out.xy = (normal_out.xy + vec2(1.0f)) * 0.5f;

    float metalness = 0.0;
    float roughness = 0.0;
    float occlusion = 0.0;
    vec3 emissive_colour = vec3(0.f);

    roughness = material.roughness_metallic_occlusion_factor.x;
    metalness = material.roughness_metallic_occlusion_factor.y;

    if (textures.y != INVALID_TEXTURE_INDEX) {
        vec4 rm = texture(global_textures[nonuniformEXT(textures.y)], vTexcoord0);

        // Green channel contains roughness values
        roughness *= rm.g;

        // Blue channel contains metalness
        metalness *= rm.b;
    }

    occlusion = material.roughness_metallic_occlusion_factor.z;
    if (textures.w != INVALID_TEXTURE_INDEX) {
        vec4 o = texture(global_textures[nonuniformEXT(textures.w)], vTexcoord0);
        // Red channel for occlusion value
        occlusion *= o.r;
    }

    roughness_metallic_occlusion_out.rgb = vec3( roughness, metalness, occlusion );
    color_out = vec4(0.f);
#if DEBUG
    color_out = vColour;
#else
    for(int i = 0; i < int(current_light_count); i++){
      color_out = base_colour;
    }
#endif
}

#endif // FRAGMENT


#if defined(FRAGMENT_TRANSPARENT_NO_CULL)

layout (location = 0) in vec2 vTexcoord0;
layout (location = 1) in vec4 vNormal_BiTanX;
layout (location = 2) in vec4 vTangent_BiTanY;
layout (location = 3) in vec4 vPosition_BiTanZ;
layout (location = 4) in flat uint mesh_draw_index;

#if DEBUG
layout (location = 5) in vec4 vColour;
#endif

layout (location = 0) out vec4 color_out;

void main() {
    MaterialData material = material_data[mesh_draw_index];
    uint flags = material.flags;

    vec3 world_position = vPosition_BiTanZ.xyz;
    vec3 normal = normalize(vNormal_BiTanX.xyz);
    if ( (flags & DrawFlags_HasNormals) == 0 ) {
        normal = normalize(cross(dFdx(world_position), dFdy(world_position)));
    }

    vec3 tangent = normalize(vTangent_BiTanY.xyz);
    vec3 bitangent = normalize(vec3(vNormal_BiTanX.w, vTangent_BiTanY.w, vPosition_BiTanZ.w));
    if ( (flags & DrawFlags_HasTangents) == 0 ) {
        vec3 uv_dx = dFdx(vec3(vTexcoord0, 0.0));
        vec3 uv_dy = dFdy(vec3(vTexcoord0, 0.0));

        // NOTE(marco): code taken from https://github.com/KhronosGroup/glTF-Sample-Viewer
        vec3 t_ = (uv_dy.t * dFdx(world_position) - uv_dx.t * dFdy(world_position)) /
                  (uv_dx.s * uv_dy.t - uv_dy.s * uv_dx.t);
        tangent = normalize(t_ - normal * dot(normal, t_));

        bitangent = cross( normal, tangent );
    }

    uvec4 textures = material.textures;
    vec4 base_colour = material.base_color_factor;
    if (textures.x != INVALID_TEXTURE_INDEX) {
        vec4 texture_colour = texture(global_textures[nonuniformEXT(textures.x)], vTexcoord0);
        base_colour *= vec4( decode_srgb( texture_colour.rgb ), texture_colour.a );
    }

    bool useAlphaMask = (flags & DrawFlags_AlphaMask) != 0;
    if (useAlphaMask && base_colour.a < material.alpha_cutoff) {
        discard;
    }

    bool use_alpha_dither = (flags & DrawFlags_AlphaDither) != 0;
    if ( use_alpha_dither ) {
        float dithered_alpha = dither(gl_FragCoord.xy, base_colour.a);
        if (dithered_alpha < 0.001f) {
            discard;
        }
    }

    if (gl_FrontFacing == false)
    {
        tangent *= -1.0;
        bitangent *= -1.0;
        normal *= -1.0;
    }

    if (textures.z != INVALID_TEXTURE_INDEX) {
        // NOTE(marco): normal textures are encoded to [0, 1] but need to be mapped to [-1, 1] value
        vec3 bump_normal = normalize( texture(global_textures[nonuniformEXT(textures.z)], vTexcoord0).rgb * 2.0 - 1.0 );
        mat3 TBN = mat3(
            tangent,
            bitangent,
            normal
        );

        normal = normalize(TBN * normalize(bump_normal));
    }

    float metalness = 0.0;
    float roughness = 0.0;
    float occlusion = 0.0;
    vec3 emissive_colour = vec3(0);

    roughness = material.roughness_metallic_occlusion_factor.x;
    metalness = material.roughness_metallic_occlusion_factor.y;

    if (textures.y != INVALID_TEXTURE_INDEX) {
        vec4 rm = texture(global_textures[nonuniformEXT(textures.y)], vTexcoord0);

        // Green channel contains roughness values
        roughness *= rm.g;

        // Blue channel contains metalness
        metalness *= rm.b;
    }

    occlusion = material.roughness_metallic_occlusion_factor.z;
    if (textures.w != INVALID_TEXTURE_INDEX) {
        vec4 o = texture(global_textures[nonuniformEXT(textures.w)], vTexcoord0);
        // Red channel for occlusion value
        occlusion *= o.r;
    }
    color_out = vec4(0.f);

#if DEBUG
    color_out = vColour;
#else
   for(int i = 0; i < int(current_light_count); i++){
      color_out += calculate_lighting( base_colour, vec3(roughness, metalness ,occlusion), normal, emissive_colour.rgb, world_position, lights[i] );
    }
#endif
}

#endif // FRAGMENT_TRANSPARENT_NO_CULL
