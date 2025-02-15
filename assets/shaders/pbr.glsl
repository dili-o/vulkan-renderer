

#if defined(VERTEX_PBR)


layout (location = 0) out vec2 vTexcoord0;

void main() {

    vTexcoord0.xy = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    gl_Position = vec4(vTexcoord0.xy * 2.0f - 1.0f, 0.0f, 1.0f);
    gl_Position.y = -gl_Position.y;
}

#endif // VERTEX

#if defined (FRAGMENT_PBR)

float ShadowCalculation(vec4 fragPosLightSpace)
 {
    vec3 projCoords = fragPosLightSpace.xyz; // No divide for orthographic projection
    if(projCoords.z < 0.f || projCoords.z > 1.f){
      return 0.2f;
    }
    projCoords.xy = projCoords.xy * 0.5 + 0.5; // Transform from NDC [-1,1] to [0,1]

    // Directional Shadow Map Sampler is VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER
    float closestDepth = texture(global_textures[nonuniformEXT(directional_shadow_map_index)], projCoords.xy).r;
    float currentDepth = projCoords.z;

    float bias = 0.005f; // Adjust as needed for self-shadowing prevention
    float shadow = (currentDepth - bias < closestDepth) ? 1.0 : 0.2;
    
    return shadow;
 }


layout(set = MATERIAL_SET, binding = 1) buffer PointLightData
{
	PointLight pointLights[];
};

layout(set = MATERIAL_SET, binding = 2) uniform DirectionalLightData {
  DirectionalLight directional_light_data;
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

    const mat4 biasMat = mat4( 
      0.5, 0.0, 0.0, 0.0,
      0.0, 0.5, 0.0, 0.0,
      0.0, 0.0, 1.0, 0.0,
      0.5, 0.5, 0.0, 1.0 );

    vec4 shadow_pos = directional_light_data.projection *
      directional_light_data.view *
      vec4(vPosition.xyz, 1.0f);
    float shadow = ShadowCalculation(shadow_pos);

    frag_color += calculate_lighting_directional(base_colour,
        rmo, normal, vec3(0.f),
        vPosition,
        directional_light_data.direction_intensity) * shadow ;

    for(uint i = 0; i < point_light_count; i++){
      frag_color += calculate_lighting_point(base_colour, rmo, normal, vec3(0.f), vPosition, pointLights[i]);
    }
    frag_color.w = 1.0f;
}

#endif // FRAGMENT
