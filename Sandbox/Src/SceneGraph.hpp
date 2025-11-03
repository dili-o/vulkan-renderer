#include "Containers/Array.hpp"
#include "Core/Defines.hpp"
#include "Core/String.hpp"
#include "Renderer/RendererTypes.hpp"
// Vendor
#include "glm/fwd.hpp"

#define MAX_NODE_LEVEL 10
struct Hierarchy {
  i32 parent = -1;
  i32 first_child = -1;
  i32 next_sibling = -1;
  i32 last_sibling = -1;
  i32 level = 0;
};

struct Scene {
  void init(hlx::Allocator *allocator);
  void shutdown();

  cstring get_node_name(i32 node);

  void mark_changed_node(i32 node);
  void update_scene_transforms();

  i32 add_node(i32 parent, i32 level, cstring name);

  hlx::Array<glm::mat4> local_transforms;
  hlx::Array<glm::mat4> global_transforms;
  hlx::Array<Hierarchy> hierarchy;
  hlx::Array<i32> transform_changed[MAX_NODE_LEVEL];

  // hlx::HashMap<i32, i32> mesh_to_node;
  std::unordered_map<i32, i32> mesh_to_node;
  hlx::Array<cstring> node_to_name;
  hlx::StringBuffer node_names;
};

struct SceneUI {
  i32 render_scene_tree_ui(Scene &scene, i32 node);
  void render_node_property_ui(Scene &scene, i32 node);

  i32 selected_node = -1;
};

struct Vertex {
  glm::vec3 position;
  glm::vec3 normal;
  glm::vec2 tex_coord;
};

bool load_gltf_scene(Scene &scene, hlx::Array<hlx::MeshDraw> &mesh_draws,
                     hlx::UnifiedBuffer<Vertex> &vertex_buffer,
                     hlx::UnifiedBuffer<u32> &index_buffer, cstring path,
                     cstring file_name);
