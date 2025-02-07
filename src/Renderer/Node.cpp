#include "Scene.hpp"

namespace Helix {
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

static void update_mesh_transform(Node* node, NodePool* node_pool) {
  update_transform(node, node_pool);
}

void NodePool::init(Allocator* allocator_) {
  allocator = allocator_;

  mesh_nodes.init(allocator_, 300, sizeof(MeshNode));
  base_nodes.init(allocator_, 50, sizeof(Node));
  light_nodes.init(allocator_, 5, sizeof(LightNode));

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
  light_nodes.shutdown();
}

void* NodePool::access_node(NodeHandle handle) {
  switch (handle.type) {
    case NodeType::Node:
      return base_nodes.access_resource(handle.index);
    case NodeType::MeshNode:
      return mesh_nodes.access_resource(handle.index);
    case NodeType::LightNode:
      return light_nodes.access_resource(handle.index);
    default:
      HERROR("Invalid NodeType");
      return nullptr;
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
    case NodeType::LightNode: {
      handle = {light_nodes.obtain_resource(), NodeType::LightNode};
      LightNode* light_node = new ((LightNode*)access_node(handle)) LightNode();
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
