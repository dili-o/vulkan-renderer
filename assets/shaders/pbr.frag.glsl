#version 450

layout ( std140, binding = 0 ) uniform LocalConstants {
    mat4        view_projection;
    vec4        eye;
    vec4        light;
    float       light_range;
    float       light_intensity;
};

uint DrawFlags_AlphaMask = 1 << 0;

layout ( std140, binding = 1 ) uniform Mesh {

    mat4        model;
    mat4        model_inverse;

    // x = diffuse index, y = roughness index, z = normal index, w = occlusion index.
    // Occlusion and roughness are encoded in the same texture
    uvec4       textures;
    vec4        base_color_factor;
    vec4        metallic_roughness_occlusion_factor;
    float       alpha_cutoff;
    uint        flags;
};

// Bindless support
// Enable non uniform qualifier extension
#extension GL_EXT_nonuniform_qualifier : enable
// Global bindless support. This should go in a common file.

layout ( set = 1, binding = 10 ) uniform sampler2D global_textures[];
// Alias textures to use the same binding point, as bindless texture is shared
// between all kind of textures: 1d, 2d, 3d.
layout ( set = 1, binding = 10 ) uniform sampler3D global_textures_3d[];


layout (location = 0) in vec2 vTexcoord0;
layout (location = 1) in vec3 vNormal;
layout (location = 2) in vec3 vTangent;
layout (location = 3) in vec3 vBiTangent;
layout (location = 4) in vec3 vPosition;

layout (location = 0) out vec4 frag_color;

#define PI 3.1415926538
#define INVALID_TEXTURE_INDEX 65535

vec3 decode_srgb( vec3 c ) {
    vec3 result;
    if ( c.r <= 0.04045) {
        result.r = c.r / 12.92;
    } else {
        result.r = pow( ( c.r + 0.055 ) / 1.055, 2.4 );
    }

    if ( c.g <= 0.04045) {
        result.g = c.g / 12.92;
    } else {
        result.g = pow( ( c.g + 0.055 ) / 1.055, 2.4 );
    }

    if ( c.b <= 0.04045) {
        result.b = c.b / 12.92;
    } else {
        result.b = pow( ( c.b + 0.055 ) / 1.055, 2.4 );
    }

    return clamp( result, 0.0, 1.0 );
}

vec3 encode_srgb( vec3 c ) {
    vec3 result;
    if ( c.r <= 0.0031308) {
        result.r = c.r * 12.92;
    } else {
        result.r = 1.055 * pow( c.r, 1.0 / 2.4 ) - 0.055;
    }

    if ( c.g <= 0.0031308) {
        result.g = c.g * 12.92;
    } else {
        result.g = 1.055 * pow( c.g, 1.0 / 2.4 ) - 0.055;
    }

    if ( c.b <= 0.0031308) {
        result.b = c.b * 12.92;
    } else {
        result.b = 1.055 * pow( c.b, 1.0 / 2.4 ) - 0.055;
    }

    return clamp( result, 0.0, 1.0 );
}

float heaviside( float v ) {
    if ( v > 0.0 ) return 1.0;
    else return 0.0;
}

float DistributionGGX(vec3 N, vec3 H, float roughness)
{
    float a = roughness*roughness;
    float a2 = a*a;
    float NdotH = max(dot(N, H), 0.0);
    float NdotH2 = NdotH*NdotH;

    float nom   = a2;
    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    denom = PI * denom * denom;

    return nom / denom;
}
// ----------------------------------------------------------------------------
float GeometrySchlickGGX(float NdotV, float roughness)
{
    float r = (roughness + 1.0);
    float k = (r*r) / 8.0;

    float nom   = NdotV;
    float denom = NdotV * (1.0 - k) + k;

    return nom / denom;
}
// ----------------------------------------------------------------------------
float GeometrySmith(vec3 N, vec3 V, vec3 L, float roughness)
{
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    float ggx2 = GeometrySchlickGGX(NdotV, roughness);
    float ggx1 = GeometrySchlickGGX(NdotL, roughness);

    return ggx1 * ggx2;
}
// ----------------------------------------------------------------------------
vec3 fresnelSchlick(float cosTheta, vec3 F0)
{
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

void main() {
    vec4 base_colour = texture(global_textures[nonuniformEXT(textures.x)], vTexcoord0) * base_color_factor;

    bool useAlphaMask = (flags & DrawFlags_AlphaMask) != 0;
    if (useAlphaMask && base_colour.a < alpha_cutoff) {
        base_colour.a = 0.0;
    }

    vec4 albedo = base_colour;

    vec3 normal = normalize( vNormal );
    vec3 tangent = normalize( vTangent );
    vec3 bitangent = normalize( vBiTangent );

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

    vec3 V = normalize( eye.xyz - vPosition );
    vec3 L = normalize( light.xyz - vPosition );
    vec3 N = normal;
    vec3 H = normalize( L + V );

    float metalness = metallic_roughness_occlusion_factor.x;
    float roughness = metallic_roughness_occlusion_factor.y;

    
    if (textures.w != INVALID_TEXTURE_INDEX) {
        vec4 rm = texture(global_textures[nonuniformEXT(textures.y)], vTexcoord0);

        // Green channel contains roughness values
        roughness *= rm.g;

        // Blue channel contains metalness
        metalness *= rm.b;
    }

    ////////////

    vec3 F0 = vec3(0.04); 
    F0 = mix(F0, albedo.rgb, metalness);

    vec3 Lo = vec3(0.0);

    float distance = length(light.xyz - vPosition);
    float attenuation = 1.0 / (distance * distance);
    vec3 radiance = vec3(light_intensity * attenuation);

    // Cook-Torrance BRDF
    float NDF = DistributionGGX(N, H, roughness);   
    float G   = GeometrySmith(N, V, L, roughness);      
    vec3 F    = fresnelSchlick(max(dot(H, V), 0.0), F0);

    vec3 numerator    = NDF * G * F; 
    float denominator = 4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0) + 0.0001; // + 0.0001 to prevent divide by zero
    vec3 specular = numerator / denominator;

    // kS is equal to Fresnel
    vec3 kS = F;

    // for energy conservation, the diffuse and specular light can't
    // be above 1.0 (unless the surface emits light); to preserve this
    // relationship the diffuse component (kD) should equal 1.0 - kS.
    vec3 kD = vec3(1.0) - kS;

    // multiply kD by the inverse metalness such that only non-metals 
    // have diffuse lighting, or a linear blend if partly metal (pure metals
    // have no diffuse light).
    kD *= 1.0 - metalness;

    // scale light by NdotL
    float NdotL = max(dot(N, L), 0.0);

    // add to outgoing radiance Lo
    Lo += (kD * albedo.rgb / PI + specular) * radiance * NdotL;  // note that we already multiplied the BRDF by the Fresnel (kS) so we won't multiply by kS again

    float occlusion = metallic_roughness_occlusion_factor.z;
    if (textures.w != INVALID_TEXTURE_INDEX) {
        vec4 o = texture(global_textures[nonuniformEXT(textures.w)], vTexcoord0);
        // Red channel for occlusion value
        occlusion *= o.r;
    }
    //base_colour.rgb = decode_srgb( base_colour.rgb );

    vec3 ambient = vec3(0.03) * albedo.rgb * occlusion;

    vec3 color = ambient + Lo;


    // HDR tonemapping
    color = color / (color + vec3(1.0));
    ////////////


    frag_color = vec4(color, 1.0);
}
