

#if defined(VERTEX)

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

void main() {
  float radius = 0.1f;
  Vertex vertex = VERTICES[gl_VertexIndex];

  vTexCoord = vertex.tex_coord;

  vec3 cam_right = vec3(world_to_camera[0][0], world_to_camera[1][0], world_to_camera[2][0]);
  vec3 cam_up = vec3(world_to_camera[0][1], world_to_camera[1][1], world_to_camera[2][1]);

  vec3 world_position = light.xyz 
    + radius * vertex.position.x * cam_right
    + radius * vertex.position.y * cam_up;

  gl_Position = view_projection * vec4(world_position, 1.f);
}

#endif // VERTEX

#if defined(FRAGMENT)

layout(location = 0) in vec2 vTexCoord;

layout(location = 0) out vec4 outColor;

void main(){
    outColor = texture(global_textures[nonuniformEXT(int(light.w))], vTexCoord);  
}

#endif // FRAGMENT

