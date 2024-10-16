#version 450

#if defined(VERTEX)

layout(std140, set = 1, binding = 0) uniform LocalConstants {
    mat4 model;
    mat4 view_projection;
    vec4 camera_position;
};

void main() {
    gl_Position = model * vec4(0.0, 0.0, 0.0, 1.0);
}

#endif // VERTEX

#if defined(FRAGMENT)

layout( std140, set = 1, binding = 0 ) uniform LocalConstants {
    mat4 model;
    mat4 view_projection;
    vec4 camera_position;
};

// Bindless support
#extension GL_EXT_nonuniform_qualifier : enable
layout ( set = 0, binding = 10 ) uniform sampler2D global_textures[];

layout(location = 0) in vec2 gTexCoord;
layout(location = 1) in vec4 gColor;

layout(location = 0) out vec4 outColor;

void main(){
    outColor = texture(global_textures[nonuniformEXT(int(camera_position.w))], gTexCoord) * vec4(gColor.xyz, 1.0);
}

#endif // FRAGMENT

#if defined(GEOMETRY)

layout(std140, set = 1, binding = 0) uniform LocalConstants {
    mat4 model;
    mat4 view_projection;
    vec4 camera_position;
};

layout(points) in;
layout (triangle_strip, max_vertices = 4) out;

layout (location = 0) out vec2 gTexCoord;
layout (location = 1) out vec4 gColor;

void main(void)
{	
    float radius = 0.5;
	vec3 Pos = gl_in[0].gl_Position.xyz;
	vec3 CameraToPoint = normalize(Pos - camera_position.xyz);
	vec3 up = vec3(0.0, 1.0, 0.0);                                                  
    vec3 right = cross(up, CameraToPoint) * radius;

	// bottom left
    Pos.y -= 0.5 * radius;
    Pos -= (right / 2);
    gl_Position = view_projection * vec4(Pos, 1.0);                                             
    gTexCoord = vec2(1.0, 0.0);    
    gColor = vec4(1.0);
    EmitVertex();

	//top left
	Pos.y += 1 * radius;
	gl_Position = view_projection * vec4(Pos, 1.0);                                                 
    gTexCoord = vec2(1.0, 1.0);     
    gColor = vec4(1.0);
    EmitVertex();    
	
    // bootom right
	Pos.y -= 1 * radius;                                                        
    Pos += right;                                                                   
    gl_Position = view_projection * vec4(Pos, 1.0);                                      
    gTexCoord = vec2(0.0, 0.0);  
    gColor = vec4(1.0);
    EmitVertex();                                                                   
    
    // top right
    Pos.y += 1 * radius;                                                        
    gl_Position = view_projection * vec4(Pos, 1.0);                                             
    gTexCoord = vec2(0.0, 1.0);  
    gColor = vec4(1.0);
    EmitVertex();

    EndPrimitive();
}

#endif // GEOMETRY