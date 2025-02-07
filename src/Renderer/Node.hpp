#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_ENABLE_EXPERIMENTAL

#include <vendor/glm/glm/glm.hpp>
#include <vendor/glm/glm/gtc/quaternion.hpp>
#include <vendor/glm/glm/gtx/quaternion.hpp>

#include "Core/Array.hpp"
#include "Core/DataStructures.hpp"
#include "Renderer/GPUResources.hpp"

namespace Helix {
// Forward Declaration
struct Mesh;

struct Transform {
  glm::vec3 scale = glm::vec3(1.0f);
  glm::quat rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
  glm::vec3 translation = glm::vec3(0.0f);

  // void                    reset();
  glm::mat4 calculate_matrix() const {
    glm::mat4 translation_matrix = glm::translate(glm::mat4(1.0f), translation);
    glm::mat4 rotation_matrix = glm::toMat4(rotation);
    glm::mat4 scale_matrix = glm::scale(glm::mat4(1.0f), scale);

    glm::mat4 local_matrix =
        translation_matrix * rotation_matrix * scale_matrix;
    return local_matrix;
  }

  void set_transform(const glm::mat4& model) {
    translation = glm::vec3(model[3]);

    glm::mat3 rotation_matrix = glm::mat3(glm::normalize(glm::vec3(model[0])),
                                          glm::normalize(glm::vec3(model[1])),
                                          glm::normalize(glm::vec3(model[2])));
    // Convert the rotation matrix to a quaternion
    rotation = glm::quat_cast(rotation_matrix);

    scale.x = glm::length(glm::vec3(model[0]));
    scale.y = glm::length(glm::vec3(model[1]));
    scale.z = glm::length(glm::vec3(model[2]));
  }
};  // struct Transform

// Nodes //////////////////////////////////////
enum class NodeType { Node, MeshNode, LightNode };

struct NodeHandle {
  u32 index = k_invalid_index;
  NodeType type = NodeType::Node;

  // Equality operator
  bool operator==(const NodeHandle& other) const {
    return index == other.index && type == other.type;
  }

  // Inequality operator
  bool operator!=(const NodeHandle& other) const { return !(*this == other); }
};

struct Node;

struct NodePool {
  void init(Allocator* allocator);
  void shutdown();

  NodeHandle obtain_node(NodeType type);
  void* access_node(NodeHandle handle);
  Node* get_root_node();

  Allocator* allocator;

  NodeHandle root_node;

  ResourcePool base_nodes;
  ResourcePool mesh_nodes;
  ResourcePool light_nodes;
};

struct Node {
  NodeHandle handle = {k_invalid_index, NodeType::Node};
  NodeHandle parent = {k_invalid_index, NodeType::Node};
  Array<NodeHandle> children;
  Transform local_transform{};
  Transform world_transform{};

  cstring name = nullptr;

  void (*updateFunc)(Node*, NodePool* node_pool);

  void update_transform(NodePool* node_pool) {
    if (!updateFunc) {
      HWARN("Node does not have update function");
      return;
    }
    updateFunc(this, node_pool);
  }
  void add_child(Node* node) {
    node->parent = handle;
    children.push(node->handle);  // TODO: Fix array issue!!!!!!!!!!!!
  }
};

struct MeshNode : public Node {
  Mesh* mesh;
};

struct LightNode : public Node {
  u32 light_index;
};
}  // namespace Helix
