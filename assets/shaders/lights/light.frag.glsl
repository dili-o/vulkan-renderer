#version 450

layout( std140, set = 1, binding = 0 ) uniform LocalConstants {
    mat4 model;
    mat4 view_projection;
    vec4 camera_position;
    uint texture_index;
};

// Bindless support
#extension GL_EXT_nonuniform_qualifier : enable
layout ( set = 0, binding = 10 ) uniform sampler2D global_textures[];

layout(location = 0) in vec2 gTexCoord;
layout(location = 1) in vec4 gColor;

layout(location = 0) out vec4 outColor;

void main(){
    outColor = texture(global_textures[nonuniformEXT(texture_index)], gTexCoord) * vec4(gColor.xyz, 1.0);
    
}