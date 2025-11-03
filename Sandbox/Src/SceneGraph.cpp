#include "SceneGraph.hpp"
#include "Core/Memory.hpp"
#include "Renderer/RendererTypes.hpp"
// Vendor
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <imgui_internal.h>

void Scene::init(hlx::Allocator *allocator) {
  local_transforms.init(allocator, 4);
  global_transforms.init(allocator, 4);
  hierarchy.init(allocator, 4);
  for (u32 i = 0; i < MAX_NODE_LEVEL; ++i) {
    transform_changed[i].init(allocator, 4);
  }

  mesh_to_node.init(allocator, 4);
  node_to_name.init(allocator, 4);
  node_names.init(allocator, hkilo(1));
}

void Scene::shutdown() {
  for (u32 i = 0; i < MAX_NODE_LEVEL; ++i) {
    transform_changed[i].shutdown();
  }

  local_transforms.shutdown();
  global_transforms.shutdown();
  hierarchy.shutdown();
  mesh_to_node.shutdown();
  node_to_name.shutdown();
  node_names.shutdown();
}

cstring Scene::get_node_name(i32 node) {
  const cstring *p_name = node_to_name.search(node);
  return p_name ? *p_name : nullptr;
}

void Scene::mark_changed_node(i32 node) {
  const i32 level = hierarchy[node].level;
  transform_changed[level].push(node);

  // Recursively update all child nodes
  for(i32 c = hierarchy[node].first_child;
      c != -1;
      c = hierarchy[c].next_sibling) {
    mark_changed_node(c);
  }
}

void Scene::update_scene_transforms() {
  if (transform_changed[0].size) {
    const i32 root = transform_changed[0][0];
    global_transforms[root] = local_transforms[root];
    transform_changed[0].clear();
  }
  for (u32 i = 1; i < MAX_NODE_LEVEL; ++i) {
    for (i32 c : transform_changed[i]) {
      i32 p = hierarchy[c].parent;
      global_transforms[c] = global_transforms[p] * local_transforms[c];
    }
    transform_changed[i].clear();
  }
}

i32 add_node(Scene &scene, i32 parent, i32 level) {
  const i32 node = (i32)scene.hierarchy.size;
  scene.local_transforms.push(glm::mat4(1.f));
  scene.global_transforms.push(glm::mat4(1.f));
  scene.hierarchy.push({parent});

  if (parent > -1) {
    const i32 s = scene.hierarchy[parent].first_child;
    if (s == -1) {
      scene.hierarchy[parent].first_child = node;
      scene.hierarchy[node].last_sibling = node;
    } else {
      i32 dest = scene.hierarchy[s].last_sibling;
      if (dest <= -1) {
        for (dest = s; 
             scene.hierarchy[dest].next_sibling != -1;
             dest = scene.hierarchy[dest].next_sibling);
      }
      scene.hierarchy[dest].next_sibling = node;
      scene.hierarchy[s].last_sibling = node;
    }
  }

  scene.hierarchy[node].first_child = -1;
  scene.hierarchy[node].next_sibling = -1;
  scene.hierarchy[node].level = level;
  return node;
}

i32 render_scene_tree_ui(Scene &scene, i32 node) {
  cstring name = scene.get_node_name(node);
  // TODO: Use the StringBuffer
  std::string label = name ? name : 
                  (std::string("Node") + std::to_string(node)).c_str();
  const bool is_leaf = scene.hierarchy[node].first_child < 0;
  ImGuiTreeNodeFlags flags = is_leaf ? 
    ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_Bullet : ImGuiTreeNodeFlags_OpenOnDoubleClick;
  if (node == scene.selected_node) {
    flags |= ImGuiTreeNodeFlags_Selected;
  }

  ImVec4 color = is_leaf ? ImVec4(0.f, 1.f, 0.f, 1.f) :
                           ImVec4(1.f, 1.f, 1.f, 1.f);
  ImGui::PushStyleColor(ImGuiCol_Text, color);
  const bool is_opened = ImGui::TreeNodeEx(
      &scene.hierarchy[node], flags, "%s", label.c_str());
  ImGui::PopStyleColor(); 
  ImGui::PushID(node);
  if (ImGui::IsItemClicked()) {
    scene.selected_node = node;
  }
  if (is_opened) {
    for (i32 ch = scene.hierarchy[node].first_child;
         ch != -1;
         ch = scene.hierarchy[ch].next_sibling) {
      if (i32 sub_node = render_scene_tree_ui(scene, ch);
          sub_node > -1) {
        scene.selected_node = sub_node;
      }
    }
    ImGui::TreePop();
  }
  ImGui::PopID();
  return scene.selected_node;
}

void render_node_property_ui(Scene &scene, i32 node) {
  if (node < 0)
    return;
  hlx::Transform local_transform;
  local_transform.set_transform(scene.local_transforms[node]);
  hlx::Transform world_transform;
  world_transform.set_transform(scene.global_transforms[node]);
  bool modified = false;

  ImGui::Text("Local Transform");
  ImGui::DragFloat3("Position", &local_transform.position.x, 0.5f, 0.f, 0.f,
                    "%.3f");
  modified |= ImGui::IsItemActive();

  glm::vec3 euler_radians = glm::eulerAngles(local_transform.rotation);
  glm::vec3 euler_degrees = glm::degrees(euler_radians);

  ImGui::DragFloat3("Rotation(Degrees)", &euler_degrees.x, 0.5f, 0.f, 0.f,
                    "%.3f");
  modified |= ImGui::IsItemActive();
  ImGui::DragFloat3("Scale", &local_transform.scale.x, 0.5f, 0.f, 0.f, "%.3f");
  modified |= ImGui::IsItemActive();

  ImGui::Text("World Transform");
  ImGui::BeginDisabled();

  ImGui::PushItemWidth(ImGui::CalcItemWidth() / 3);
  ImGui::InputFloat("##Position_X", &world_transform.position.x);
  ImGui::SameLine();
  ImGui::InputFloat("##Position_Y", &world_transform.position.y);
  ImGui::SameLine();
  ImGui::InputFloat("##Position_Z", &world_transform.position.z);
  ImGui::SameLine();
  ImGui::Text("World Position");
  ImGui::PopItemWidth();

  ImGui::PushItemWidth(ImGui::CalcItemWidth() / 4);
  ImGui::InputFloat("##Rotation_X", &world_transform.rotation.x);
  ImGui::SameLine();
  ImGui::InputFloat("##Rotation_Y", &world_transform.rotation.y);
  ImGui::SameLine();
  ImGui::InputFloat("##Rotation_Z", &world_transform.rotation.z);
  ImGui::SameLine();
  ImGui::InputFloat("##Rotation_W", &world_transform.rotation.w);
  ImGui::SameLine();
  ImGui::Text("World Rotation(Quat)");
  ImGui::PopItemWidth();

  ImGui::PushItemWidth(ImGui::CalcItemWidth() / 3);
  ImGui::InputFloat("##Scale_X", &world_transform.scale.x);
  ImGui::SameLine();
  ImGui::InputFloat("##Scale_Y", &world_transform.scale.y);
  ImGui::SameLine();
  ImGui::InputFloat("##Scale_Z", &world_transform.scale.z);
  ImGui::SameLine();
  ImGui::Text("World Scale");
  ImGui::PopItemWidth();

  ImGui::EndDisabled();

  if (modified) {
    local_transform.rotation = glm::quat(glm::radians(euler_degrees));
    scene.local_transforms[node] = local_transform.get_mat4();
    scene.mark_changed_node(node);
  }
}
