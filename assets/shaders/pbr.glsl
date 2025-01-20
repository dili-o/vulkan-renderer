

#if defined(VERTEX_PBR)

layout (location = 0) out vec2 vTexcoord0;

void main() {

    vTexcoord0.xy = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    gl_Position = vec4(vTexcoord0.xy * 2.0f - 1.0f, 0.0f, 1.0f);
    gl_Position.y = -gl_Position.y;
}

#endif // VERTEX

#if defined (FRAGMENT_PBR)

layout( push_constant ) uniform LightingData {

    // x = diffuse index, y = roughness index, z = normal index, w = occlusion index.
    // Occlusion and roughness are encoded in the same texture
    uvec4       gbuffer_textures;
};

layout (location = 0) in vec2 vTexcoord0;

layout (location = 0) out vec4 frag_color;

bool decimalPlacesMatch(vec4 a, vec4 b) {
    // Extract the first decimal place for each component and compare
    vec4 aDecimals = floor(fract(a) * 10.0); // First decimal place of 'a'
    vec4 bDecimals = floor(fract(b) * 10.0); // First decimal place of 'b'

    return all(equal(aDecimals, bDecimals)); // Check if all components match
}


void main() {
    vec4 base_colour = texture(global_textures[nonuniformEXT(gbuffer_textures.x)], vTexcoord0);
    if (decimalPlacesMatch(base_colour, vec4(0.52941, 0.80784, 0.92157, 1.00))){
        frag_color = vec4( encode_srgb( base_colour.xyz ), base_colour.a );
        return;
    }

    vec3 rmo = texture(global_textures[nonuniformEXT(gbuffer_textures.y)], vTexcoord0).rgb;
    vec3 normal = texture(global_textures[nonuniformEXT(gbuffer_textures.z)], vTexcoord0).rgb;
    // Convert from [0, 1] -> [-1, 1] then decode
    normal.rg = (normal.rg * 2.0f) - vec2(1.0f);
    normal = octahedral_decode(normal.rg);
    vec3 vPosition = texture(global_textures[nonuniformEXT(gbuffer_textures.w)], vTexcoord0).rgb;

    vec3 V = normalize( eye.xyz - vPosition );
    vec3 L = normalize( light.xyz - vPosition );
    vec3 N = normalize( normal );
    vec3 H = normalize( L + V );

    float roughness = rmo.r;
    float metalness = rmo.g;
    float occlusion = rmo.b;

    float alpha = pow(roughness, 2.0);

    // https://www.khronos.org/registry/glTF/specs/2.0/glTF-2.0.html#specular-brdf
    float NdotH = clamp(dot(N, H), 0, 1);
    float alpha_squared = alpha * alpha;
    float d_denom = ( NdotH * NdotH ) * ( alpha_squared - 1.0 ) + 1.0;
    float distribution = ( alpha_squared * heaviside( NdotH ) ) / ( PI * d_denom * d_denom );

    float NdotL = clamp(dot(N, L), 0, 1);
    float NdotV = clamp(dot(N, V), 0, 1);
    float HdotL = clamp(dot(H, L), 0, 1);
    float HdotV = clamp(dot(H, V), 0, 1);

    float distance = length(light.xyz - vPosition);
    float intensity = light_intensity * max(min(1.0 - pow(distance / light_range, 4.0), 1.0), 0.0) / pow(distance, 2.0);

    vec3 material_colour = vec3(0, 0, 0);
    if (NdotL > 0.0 || NdotV > 0.0)
    {
        float visibility = ( heaviside( HdotL ) / ( abs( NdotL ) + sqrt( alpha_squared + ( 1.0 - alpha_squared ) * ( NdotL * NdotL ) ) ) ) * ( heaviside( HdotV ) / ( abs( NdotV ) + sqrt( alpha_squared + ( 1.0 - alpha_squared ) * ( NdotV * NdotV ) ) ) );

        float specular_brdf = intensity * NdotL * visibility * distribution;

        vec3 diffuse_brdf = intensity * NdotL * (1 / PI) * base_colour.rgb;

        // NOTE(marco): f0 in the formula notation refers to the base colour here
        vec3 conductor_fresnel = specular_brdf * ( base_colour.rgb + ( 1.0 - base_colour.rgb ) * pow( 1.0 - abs( HdotV ), 5 ) );

        // NOTE(marco): f0 in the formula notation refers to the value derived from ior = 1.5
        float f0 = 0.04; // pow( ( 1 - ior ) / ( 1 + ior ), 2 )
        float fr = f0 + ( 1 - f0 ) * pow(1 - abs( HdotV ), 5 );
        vec3 fresnel_mix = mix( diffuse_brdf, vec3( specular_brdf ), fr );

        material_colour = mix( fresnel_mix, conductor_fresnel, metalness );
    }

    frag_color = vec4( encode_srgb( material_colour ), base_colour.a );
}

#endif // FRAGMENT
