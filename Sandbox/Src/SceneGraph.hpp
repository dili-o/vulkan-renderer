#include "Core/Defines.hpp"
#include "Containers/Array.hpp"
#include "Containers/HashMap.hpp"
#include "Core/String.hpp"
// Vendor
#include "glm/fwd.hpp"

#define MAX_NODE_LEVEL 5
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

  hlx::Array<glm::mat4> local_transforms;
  hlx::Array<glm::mat4> global_transforms;
  hlx::Array<Hierarchy> hierarchy;
  hlx::Array<i32> transform_changed[MAX_NODE_LEVEL];

  hlx::HashMap<u32, u32> mesh_to_node;
  hlx::HashMap<u32, cstring> node_to_name;
  hlx::StringBuffer node_names;

  i32 selected_node = -1;
};


i32 add_node(Scene &scene, i32 parent, i32 level);

i32 render_scene_tree_ui(Scene &scene, i32 node);
void render_node_property_ui(Scene &scene, i32 node);
