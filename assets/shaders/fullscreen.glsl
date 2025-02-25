

#if defined(VERTEX)

layout (location = 0) out vec2 vTexCoord;
layout (location = 1) flat out uint out_texture_id;

void main() {

    vTexCoord.xy = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    gl_Position = vec4(vTexCoord.xy * 2.0f - 1.0f, 0.0f, 1.0f);
    gl_Position.y = -gl_Position.y;

    out_texture_id = gl_InstanceIndex;
}

#endif // VERTEX

#if defined(FRAGMENT)

layout (location = 0) in vec2 vTexCoord;
layout (location = 1) flat in uint texture_id;

layout (location = 0) out vec4 out_color;

layout ( std140, set = MATERIAL_SET, binding = 0 ) uniform LocalConstants {
    mat4        view_projection;
    vec4        eye;
    vec4        light;
    float       light_range;
    float       light_intensity;
};

// AGX Tone Mapping Function
vec3 agxToneMap(vec3 color) {
    // Apply a basic curve for highlight compression
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;

    return clamp((color * (a * color + b)) / (color * (c * color + d) + e), 0.0, 1.0);
}

void main() {
    vec4 color = texture(global_textures[nonuniformEXT(texture_id)], vTexCoord.xy);

    float exposure = 1.f;
    // exposure tone mapping
    vec3 mapped = agxToneMap(color.xyz);
    //vec3 mapped = vec3(1.0) - exp(-color.rgb * exposure);
    out_color = vec4(mapped, color.a);
}

#endif // FRAGMENT
