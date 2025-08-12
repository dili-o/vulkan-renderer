#pragma once

#include "Containers/Array.hpp"
#include "Core/String.hpp"
#include "Renderer/RendererTypes.hpp"

namespace Helix {

#define INVALID_NODE_ID UINT32_MAX

struct RenderPacket;

// TODO: Maybe add a primitive node that can be a child.
struct Node {
  cstring name{nullptr};
  u32 parent_index{INVALID_NODE_ID};
  u32 children_count{0};
  u32 first_child_index{INVALID_NODE_ID};
};

struct PointLightNode {
  cstring name;
  u32 info_index;
};

struct PointLightInfo {
  glm::vec3 position;
  f32 radius;
  f32 range;
  f32 intensity;
};

struct NodeDrawProperty {
  void *node_property{nullptr};
  u32 node_index{INVALID_NODE_ID};
};

typedef bool (*PFN_draw_node_property)(NodeDrawProperty *);

struct NodeHierarchy {
public:
  void init();
  void shutdown();

  u32 add_node(cstring name, u32 parent_index, bool is_mesh_node = false);
  void add_point_light_node();

  void imgui_draw_node_hierarchy();
  void imgui_draw_node_property();

  void update(u32 node_index);

  Array<Node> nodes;
  Array<u32> root_node_indices;
  Array<u32> mesh_node_indices;
  Array<Transform> local_transforms;
  Array<Transform> world_transforms;
  StringBuffer string_buffer{};

private:
  void draw_node(u32 node_index);
  void draw_point_light_nodes();

private:
  u32 current_node{INVALID_NODE_ID};
  u32 current_point_light{INVALID_NODE_ID};

  NodeDrawProperty current_node_property{};

  Array<PointLightInfo> point_light_info;
  Array<PointLightNode> point_light_nodes;

  PFN_draw_node_property draw_node_property{nullptr};
};

struct Scene {
  void init();
  void shutdown();

  bool load_mesh(cstring path, cstring model);
  void unload_mesh(u32 mesh_index);

  void update(RenderPacket *packet);

  NodeHierarchy node_hierarchy;
  Array<PBRMaterial> pbr_materials{};
  Array<Mesh> meshes;
  StringBuffer string_buffer{};
};

} // namespace Helix
