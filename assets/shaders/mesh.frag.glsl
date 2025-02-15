
layout(set = MATERIAL_SET, binding = 9) buffer PointLightData
{
	PointLight pointLights[];
};

#if defined(FRAGMENT_GBUFFER_CULLING)

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
#if DEBUG
    color_out = vColour;
#else
    color_out = base_colour;
#endif
}

#endif // FRAGMENT


#if defined(FRAGMENT_TRANSPARENT_NO_CULL)

layout(set = MATERIAL_SET, binding = 11) uniform DirectionalLightData {
  DirectionalLight directional_light_data;
};

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
    color_out = vec4(0.f);
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
    

#if DEBUG
    color_out = vColour;
#else
    for(uint i = 0; i < point_light_count; i++){
      color_out += calculate_lighting_point( base_colour, vec3(roughness, metalness ,occlusion), normal, emissive_colour.rgb, world_position, pointLights[i] );
    }
    color_out += calculate_lighting_directional(base_colour, vec3(roughness, metalness ,occlusion), normal, emissive_colour.rgb, world_position, directional_light_data.direction_intensity);
#endif
}

#endif // FRAGMENT_TRANSPARENT_NO_CULL
