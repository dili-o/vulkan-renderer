

#if defined(VERTEX_PBR)

layout (location = 0) out vec2 vTexcoord0;

void main() {

    vTexcoord0.xy = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    gl_Position = vec4(vTexcoord0.xy * 2.0f - 1.0f, 0.0f, 1.0f);
    gl_Position.y = -gl_Position.y;
}

#endif // VERTEX

#if defined (FRAGMENT_PBR)
layout(set = MATERIAL_SET, binding = 1) buffer LightData
{
	Light lights[];
};

layout( push_constant ) uniform LightingData {

    // x = colour index, y = roughness_metalness_occlusion index, z = normal index, w = depth index.
    uvec4       gbuffer_textures;
};

layout (location = 0) in vec2 vTexcoord0;

layout (location = 0) out vec4 frag_color;

void main() {
    frag_color = vec4(0.f);
    vec4 base_colour = texture(global_textures[nonuniformEXT(gbuffer_textures.x)], vTexcoord0);
    float raw_depth = texture(global_textures[nonuniformEXT(gbuffer_textures.w)], vTexcoord0).r;

    if (raw_depth == 1.0f){
        frag_color = vec4( encode_srgb( base_colour.xyz ), base_colour.a );
        return;
    }

    vec3 rmo = texture(global_textures[nonuniformEXT(gbuffer_textures.y)], vTexcoord0).rgb;
    vec3 normal = texture(global_textures[nonuniformEXT(gbuffer_textures.z)], vTexcoord0).rgb;
    // Convert from [0, 1] -> [-1, 1] then decode
    normal.rg = (normal.rg * 2.0f) - vec2(1.0f);
    normal = octahedral_decode(normal.rg);

    vec3 vPosition = world_position_from_depth( vTexcoord0, raw_depth, inverse_view_projection);

    for(int i = 0; i < int(current_light_count); i++){
      frag_color += calculate_lighting(base_colour, rmo, normal, vec3(0.f), vPosition, lights[i]);
    }
}

#endif // FRAGMENT
