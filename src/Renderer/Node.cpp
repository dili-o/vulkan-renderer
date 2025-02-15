#include <vendor/imgui/imgui.h>

#include "Scene.hpp"

namespace Helix {
cstring node_type_to_cstring(NodeType type) {
  switch (type) {
    case Helix::NodeType::Node:
      return "Node";
      break;
    case Helix::NodeType::MeshNode:
      return "Mesh Node";
      break;
    case Helix::NodeType::PointLightNode:
      return "Light Node";
      break;
    case Helix::NodeType::DirectionalLightNode:
      return "Light Node";
      break;
    default:
      HCRITICAL("Invalid node type");
      return "Invalid node type";
      break;
  }
}

static void update_transform(Node* node, NodePool* node_pool) {
  if (node->parent.index != k_invalid_index) {
    Node* parent_node = (Node*)node_pool->access_node(node->parent);
    glm::mat4 combined_matrix =
        parent_node->world_transform.calculate_matrix() *
        node->local_transform.calculate_matrix();
    node->world_transform.set_transform(combined_matrix);
  } else {
    node->world_transform.set_transform(
        node->local_transform.calculate_matrix());
  }

  for (u32 i = 0; i < node->children.size; i++) {
    Node* child_node = (Node*)node_pool->access_node(node->children[i]);
    child_node->update_transform(node_pool);
  }
}

static void draw_node_property_p_light(NodePool& node_pool,
                                       NodeHandle node_handle) {
  Node* node = (Node*)node_pool.access_node(node_handle);
  ImGui::Text("Name: %s", node->name);
  ImGui::Text("Type: %s", node_type_to_cstring(node_handle.type));

  if (!(node->parent.index == k_invalid_index)) {
    Node* parent = (Node*)node_pool.access_node(node->parent);
    ImGui::Text("Parent: %s", parent->name);
  }

  bool modified = false;

  glm::vec3 local_rotation =
      glm::degrees(glm::eulerAngles(node->local_transform.rotation));
  glm::vec3 world_rotation =
      glm::degrees(glm::eulerAngles(node->world_transform.rotation));

  // TODO: Represent rotation as quats
  ImGui::Text("Local Transform");
  modified |= ImGui::InputFloat3("position##local",
                                 (float*)&node->local_transform.translation);
  modified |=
      ImGui::InputFloat3("scale##local", (float*)&node->local_transform.scale);
  modified |= ImGui::InputFloat3("rotation##local", (float*)&local_rotation);

  ImGui::Text("World Transform");
  ImGui::InputFloat3("position##world",
                     (float*)&node->world_transform.translation);
  ImGui::InputFloat3("scale##world", (float*)&node->world_transform.scale);
  ImGui::InputFloat3("rotation##world", (float*)&world_rotation);

  if (modified) {
    node->local_transform.rotation = glm::quat(glm::radians(local_rotation));
    node->update_transform(&node_pool);
  }
}

static void update_mesh_transform(Node* node, NodePool* node_pool) {
  update_transform(node, node_pool);
}

void NodePool::init(Allocator* allocator_) {
  allocator = allocator_;

  mesh_nodes.init(allocator_, 300, sizeof(MeshNode));
  base_nodes.init(allocator_, 50, sizeof(Node));
  point_light_nodes.init(allocator_, 5, sizeof(PointLightNode));
  directional_light_nodes.init(allocator_, 1, sizeof(DirectionalLightNode));

  root_node = obtain_node(NodeType::Node);

  Node* root = (Node*)access_node(root_node);
  root->children.init(allocator, 4);
  root->parent = {k_invalid_index, NodeType::Node};
  root->name = "Root_Node";
  root->world_transform = Transform{};
  root->local_transform = Transform{};
}

void NodePool::shutdown() {
  mesh_nodes.shutdown();
  base_nodes.shutdown();
  point_light_nodes.shutdown();
  directional_light_nodes.shutdown();
}

void* NodePool::access_node(NodeHandle handle) {
  switch (handle.type) {
    case NodeType::Node:
      return base_nodes.access_resource(handle.index);
    case NodeType::MeshNode:
      return mesh_nodes.access_resource(handle.index);
    case NodeType::PointLightNode:
      return point_light_nodes.access_resource(handle.index);
    case NodeType::DirectionalLightNode:
      return directional_light_nodes.access_resource(handle.index);
    default:
      HERROR("Invalid NodeType");
      return nullptr;
  }
}

void NodePool::destroy_node(NodeHandle handle) {
  Node* node = (Node*)access_node(handle);
  for (u32 i = 0; i < node->children.size; i++) {
    destroy_node(node->children[i]);
  }
  node->children.shutdown();
  switch (handle.type) {
    case NodeType::Node:
      base_nodes.release_resource(handle.index);
      break;
    case NodeType::MeshNode:
      mesh_nodes.release_resource(handle.index);
      break;
    case NodeType::PointLightNode:
      point_light_nodes.release_resource(handle.index);
      break;
    case NodeType::DirectionalLightNode:
      directional_light_nodes.release_resource(handle.index);
      break;
    default:
      HERROR("Invalid NodeType");
      break;
  }
}

Node* NodePool::get_root_node() {
  Node* root = (Node*)access_node(root_node);
  HASSERT(root);
  return root;
}

NodeHandle NodePool::obtain_node(NodeType type) {
  NodeHandle handle{};
  switch (type) {
    case NodeType::Node: {
      handle = {base_nodes.obtain_resource(), NodeType::Node};
      Node* base_node = new ((Node*)access_node(handle)) Node();
      base_node->updateFunc = update_transform;
      base_node->handle = handle;
      break;
    }
    case NodeType::MeshNode: {
      handle = {mesh_nodes.obtain_resource(), NodeType::MeshNode};
      MeshNode* mesh_node = new ((MeshNode*)access_node(handle)) MeshNode();
      mesh_node->updateFunc = update_mesh_transform;
      mesh_node->handle = handle;
      break;
    }
    case NodeType::PointLightNode: {
      handle = {point_light_nodes.obtain_resource(), NodeType::PointLightNode};
      PointLightNode* light_node =
          new ((PointLightNode*)access_node(handle)) PointLightNode();
      light_node->updateFunc = update_transform;
      light_node->handle = handle;
      break;
    }
    case NodeType::DirectionalLightNode: {
      handle = {directional_light_nodes.obtain_resource(),
                NodeType::DirectionalLightNode};
      DirectionalLightNode* light_node =
          new ((DirectionalLightNode*)access_node(handle))
              DirectionalLightNode();
      light_node->updateFunc = update_transform;
      light_node->handle = handle;
      break;
    }
    default:
      HERROR("Invalid NodeType");
      break;
  }
  return handle;
}

}  // namespace Helix
