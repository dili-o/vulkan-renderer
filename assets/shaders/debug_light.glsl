

#if defined(VERTEX)

layout(set = MATERIAL_SET, binding = 1) buffer PointLightData
{
	vec4 position_textures[];
};

struct Vertex{
  vec2 position;
  vec2 tex_coord;
};

const Vertex VERTICES[6] = Vertex[](
  Vertex(vec2(-1.0, -1.0),vec2(0.f, 1.f)),
  Vertex(vec2(-1.0,  1.0),vec2(0.f, 0.f)),
  Vertex(vec2( 1.0, -1.0),vec2(1.f, 1.f)),
  Vertex(vec2( 1.0, -1.0),vec2(1.f, 1.f)),
  Vertex(vec2(-1.0,  1.0),vec2(0.f, 0.f)),
  Vertex(vec2( 1.0,  1.0),vec2(1.f, 0.f))
);

layout(location = 0) out vec2 vTexCoord;
layout(location = 1) flat out int texture_index;

void main() {
  float radius = 0.1f;
  Vertex vertex = VERTICES[gl_VertexIndex];

  vTexCoord = vertex.tex_coord;
  texture_index = int( position_textures[gl_InstanceIndex].w );

  vec3 cam_right = vec3(world_to_camera[0][0], world_to_camera[1][0], world_to_camera[2][0]);
  vec3 cam_up = vec3(world_to_camera[0][1], world_to_camera[1][1], world_to_camera[2][1]);

  vec3 world_position = position_textures[gl_InstanceIndex].xyz
    + radius * vertex.position.x * cam_right
    + radius * vertex.position.y * cam_up;

  gl_Position = view_projection * vec4(world_position, 1.f);
}

#endif // VERTEX

#if defined(FRAGMENT)

layout(location = 0) in vec2 vTexCoord;
layout(location = 1) flat in int texture_index;

layout(location = 0) out vec4 outColor;

void main(){
    outColor = texture(global_textures[nonuniformEXT(texture_index)], vTexCoord);  
}

#endif // FRAGMENT

