#version 450

layout ( std140, binding = 0 ) uniform LocalConstants {
    mat4        view_projection;
    vec4        eye;
    vec4        light;
    float       light_range;
    float       light_intensity;
};

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
layout (location = 2) in vec3 vPosition;

layout (location = 0) out vec4 frag_color;

#define INVALID_TEXTURE_INDEX 65535

void main() {
    vec3 ambient = texture(global_textures[nonuniformEXT(textures.x)], vTexcoord0).rgb * 0.1;

    // diffuse 
    vec3 norm = normalize(vNormal);
    vec3 lightDir = normalize(light.xyz - vPosition);
    float diff = max(dot(norm, lightDir), 0.0);
    vec3 diffuse = vec3(0.8) * diff * texture(global_textures[nonuniformEXT(textures.x)], vTexcoord0).rgb;

    // specular
    vec3 viewDir = normalize(eye.xyz - vPosition);
    vec3 reflectDir = reflect(-lightDir, norm);  
    float spec = pow(max(dot(viewDir, reflectDir), 0.0), 32.f);
    vec3 specular = vec3(1.0) * (spec * vec3(1.0));  
        
    vec3 result = ambient + diffuse + specular;

    frag_color = vec4(result, 1.0);
}
