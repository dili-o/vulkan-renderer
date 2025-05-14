#include "Renderer/Scene.hpp"
#include "Containers/RingQueue.hpp"
#include "Core/Log.hpp"
#include "Core/String.hpp"
#include "Platform/File.hpp"
#include "Renderer/ImguiFrontend.hpp"
#include "Renderer/MeshLoader.hpp"
#include "Renderer/RendererFrontEnd.hpp"
#include "Renderer/RendererTypes.hpp"

namespace Helix {

bool static imgui_point_light_property(NodeDrawProperty *node_property) {
  PointLightInfo *node_info = (PointLightInfo *)node_property->node_property;

  bool modified = false;
  ImGui::InputFloat3("Position", &node_info->position.x);
  modified |= ImGui::IsItemDeactivatedAfterEdit();
  ImGui::InputFloat("Radius", &node_info->radius);
  modified |= ImGui::IsItemDeactivatedAfterEdit();
  ImGui::InputFloat("Range", &node_info->range);
  modified |= ImGui::IsItemDeactivatedAfterEdit();
  ImGui::InputFloat("Intensity", &node_info->intensity);
  modified |= ImGui::IsItemDeactivatedAfterEdit();

  return modified;
}

bool static imgui_node_property(NodeDrawProperty *node_property) {
  // TODO: Change back to only inlcude the Transform as the node property
  // Transform *transform = (Transform *)node_property->node_property;
  NodeHierarchy *node_hierarchy = (NodeHierarchy *)node_property->node_property;

  Transform *local_transform =
      &node_hierarchy->local_transforms[node_property->node_index];

  Transform *world_transform =
      &node_hierarchy->world_transforms[node_property->node_index];

  const ImGuiInputFlags flags = 0;
  ImGui::Text("Local Transform");
  bool modified = false;
  ImGui::InputFloat3("Position", &local_transform->position.x, "%.3f", flags);
  modified |= ImGui::IsItemDeactivatedAfterEdit();

  glm::vec3 euler_radians = glm::eulerAngles(local_transform->rotation);
  glm::vec3 euler_degrees = glm::degrees(euler_radians);

  ImGui::InputFloat3("Rotation(Degrees)", &euler_degrees.x, "%.3f", flags);
  modified |= ImGui::IsItemDeactivatedAfterEdit();
  ImGui::InputFloat3("Scale", &local_transform->scale.x, "%.3f", flags);
  modified |= ImGui::IsItemDeactivatedAfterEdit();

  ImGui::Text("World Transform");

  ImGui::BeginDisabled();

  ImGui::PushItemWidth(ImGui::CalcItemWidth() / 3);
  ImGui::InputFloat("##Position_X", &world_transform->position.x);
  ImGui::SameLine();
  ImGui::InputFloat("##Position_Y", &world_transform->position.y);
  ImGui::SameLine();
  ImGui::InputFloat("##Position_Z", &world_transform->position.z);
  ImGui::SameLine();
  ImGui::Text("World Position");
  ImGui::PopItemWidth();

  ImGui::PushItemWidth(ImGui::CalcItemWidth() / 4);
  ImGui::InputFloat("##Rotation_X", &world_transform->rotation.x);
  ImGui::SameLine();
  ImGui::InputFloat("##Rotation_Y", &world_transform->rotation.y);
  ImGui::SameLine();
  ImGui::InputFloat("##Rotation_Z", &world_transform->rotation.z);
  ImGui::SameLine();
  ImGui::InputFloat("##Rotation_W", &world_transform->rotation.w);
  ImGui::SameLine();
  ImGui::Text("World Rotation(Quat)");
  ImGui::PopItemWidth();

  ImGui::PushItemWidth(ImGui::CalcItemWidth() / 3);
  ImGui::InputFloat("##Scale_X", &world_transform->scale.x);
  ImGui::SameLine();
  ImGui::InputFloat("##Scale_Y", &world_transform->scale.y);
  ImGui::SameLine();
  ImGui::InputFloat("##Scale_Z", &world_transform->scale.z);
  ImGui::SameLine();
  ImGui::Text("World Scale");
  ImGui::PopItemWidth();

  ImGui::EndDisabled();

  if (modified)
    local_transform->rotation = glm::quat(glm::radians(euler_degrees));

  return modified;
}

void NodeHierarchy::init() {

  nodes.init(&MemoryService::instance()->system_allocator, 32);
  root_node_indices.init(&MemoryService::instance()->system_allocator, 32);
  mesh_node_indices.init(&MemoryService::instance()->system_allocator, 32);
  local_transforms.init(&MemoryService::instance()->system_allocator, 32);
  world_transforms.init(&MemoryService::instance()->system_allocator, 32);

  point_light_info.init(&MemoryService::instance()->system_allocator, 32);
  point_light_nodes.init(&MemoryService::instance()->system_allocator, 32);

  string_buffer.init(&MemoryService::instance()->system_allocator, hmega(2));
}

void NodeHierarchy::shutdown() {
  root_node_indices.shutdown();
  mesh_node_indices.shutdown();
  nodes.shutdown();
  local_transforms.shutdown();
  world_transforms.shutdown();

  point_light_info.shutdown();
  point_light_nodes.shutdown();

  string_buffer.shutdown();
}

u32 NodeHierarchy::add_node(cstring name, u32 parent_index, bool is_mesh_node) {
  if (parent_index != INVALID_NODE_ID) {
    Node &parent_node = nodes[parent_index];
    if (parent_node.first_child_index == INVALID_NODE_ID)
      parent_node.first_child_index = nodes.size;
    ++parent_node.children_count;
  } else {
    root_node_indices.push(nodes.size);
  }
  if (is_mesh_node)
    mesh_node_indices.push(nodes.size);

  Transform transform{};
  local_transforms.push(transform);
  world_transforms.push(transform);

  if (name)
    nodes.push({name, parent_index});
  else
    nodes.push(
        {string_buffer.append_use_f("Node_%d", nodes.size), parent_index});
  return nodes.size - 1;
}

void NodeHierarchy::add_point_light_node() {
  PointLightInfo info{};
  point_light_info.push(info);

  point_light_nodes.push(
      {string_buffer.append_use_f("PointLight_%d", point_light_nodes.size),
       INVALID_NODE_ID});
}

void NodeHierarchy::draw_node(u32 node_index) {
  Node &node = nodes[node_index];
  ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnDoubleClick;
  flags |= node.children_count ? 0 : ImGuiTreeNodeFlags_Leaf;
  flags |= node_index == current_node ? ImGuiTreeNodeFlags_Selected : 0;
  if (ImGui::TreeNodeEx(node.name, flags)) {
    // Selected
    if (ImGui::IsItemClicked()) {
      current_node = node_index;
      current_point_light = INVALID_NODE_ID;

      // current_node_property.node_property = &local_transforms[node_index];
      current_node_property.node_property = this;
      current_node_property.node_index = node_index;
      draw_node_property = imgui_node_property;
    }
    for (u32 i = 0; i < node.children_count; ++i) {
      draw_node(i + node.first_child_index);
    }
    ImGui::TreePop();
  }
}

void NodeHierarchy::draw_point_light_nodes() {
  for (u32 i = 0; i < point_light_nodes.size; ++i) {
    PointLightNode &node = point_light_nodes[i];
    ImGuiTreeNodeFlags flags =
        ImGuiTreeNodeFlags_OpenOnDoubleClick | ImGuiTreeNodeFlags_Leaf;
    flags |= current_point_light == i ? ImGuiTreeNodeFlags_Selected : 0;
    if (ImGui::TreeNodeEx(node.name, flags)) {
      // Selected
      if (ImGui::IsItemClicked()) {
        current_point_light = i;
        current_node = INVALID_NODE_ID;

        current_node_property.node_property = &point_light_info[i];
        current_node_property.node_index = i;
        draw_node_property = imgui_point_light_property;
      }
      ImGui::TreePop();
    }
  }
}

void NodeHierarchy::imgui_draw_node_hierarchy() {
  if (ImGui::Begin("Node Hierarchy")) {
    draw_point_light_nodes();
    for (u32 i = 0; i < root_node_indices.size; ++i) {
      draw_node(root_node_indices[i]);
    }
  }
  ImGui::End();
}

void NodeHierarchy::imgui_draw_node_property() {
  if (ImGui::Begin("Node Property")) {
    if (draw_node_property) {
      bool node_property_changed = draw_node_property(&current_node_property);
      if (node_property_changed)
        update(current_node_property.node_index);
    }
  }
  ImGui::End();
}

void NodeHierarchy::update(u32 node_index) {
  StackAllocator *stack_allocator = &MemoryService::instance()->stack_allocator;
  size_t stack_marker = stack_allocator->get_marker();

  RingQueue<u32> update_queue{};
  update_queue.init(stack_allocator, nodes.size);
  update_queue.enqueue(node_index);

  while (update_queue.size) {
    u32 current_node_index;
    update_queue.dequeue(&current_node_index);

    Node &current_node = nodes[current_node_index];

    if (current_node.parent_index != INVALID_NODE_ID) {
      Transform &local_transform = local_transforms[current_node_index];
      Transform &parent_world_transform =
          world_transforms[current_node.parent_index];

      glm::mat4 new_transform_matrix =
          parent_world_transform.get_mat4() * local_transform.get_mat4();

      world_transforms[current_node_index].set_transform(new_transform_matrix);
    } else {
      world_transforms[current_node_index] =
          local_transforms[current_node_index];
    }

    for (u32 i = 0; i < current_node.children_count; ++i) {
      u32 next_node = current_node.first_child_index + i;
      update_queue.enqueue(next_node);
    }
  }

  update_queue.shutdown();
  stack_allocator->free_marker(stack_marker);
}

void Scene::init() {
  node_hierarchy.init();
  meshes.init(&MemoryService::instance()->system_allocator, 32);
  string_buffer.init(&MemoryService::instance()->system_allocator, hmega(2));
  pbr_materials.init(&MemoryService::instance()->system_allocator, 25);

  // Default material
  PBRMaterial &pbr_material = pbr_materials.push_use();
  pbr_material.albedo_texture_handle =
      RendererFrontEnd::instance()->default_albedo_texture;
  pbr_material.normal_texture_handle =
      RendererFrontEnd::instance()->default_normal_texture;
}

void Scene::shutdown() {
  u32 mesh_size = meshes.size;
  for (u32 i = 0; i < mesh_size; ++i) {
    unload_mesh(i);
  }

  // TODO: Right now some .obj models (bistro interior and exterior) have shapes
  // that use multiple materials. Deleting all materials right (calls delete on
  // already deleted materials)
  for (u32 i = 0; i < pbr_materials.size; ++i) {
    PBRMaterial &mat = pbr_materials[i];
    RendererFrontEnd::instance()->destroy_texture(mat.albedo_texture_handle);
    RendererFrontEnd::instance()->destroy_texture(mat.normal_texture_handle);
  }
  pbr_materials.shutdown();
  string_buffer.shutdown();
  meshes.shutdown();
  node_hierarchy.shutdown();
}

bool Scene::load_mesh(cstring path, cstring model) {

  cstring file_extension = FileService::get_file_extension(model);
  if (string_equals("obj", file_extension))
    return load_obj_mesh(this, path, model);
  else if (string_equals("gltf", file_extension))
    return load_gltf_mesh(this, path, model);
  else if (string_equals("glb", file_extension))
    return load_gltf_mesh(this, path, model);

  HERROR("Unknown mesh file type: {}", file_extension);
  return false;
}

void Scene::unload_mesh(u32 mesh_index) {
  RendererFrontEnd *renderer_frontend = RendererFrontEnd::instance();

  Mesh &mesh = meshes[mesh_index];

  for (u32 i = 0; i < mesh.draws.size; ++i) {
    // Skip the default material
    if (mesh.draws[i].material_index != 0) {
      PBRMaterial &material = pbr_materials[mesh.draws[i].material_index];
      if (material.albedo_texture_handle.index !=
          renderer_frontend->default_albedo_texture.index) {
        renderer_frontend->destroy_texture(material.albedo_texture_handle);
      }
      if (material.normal_texture_handle.index !=
          renderer_frontend->default_normal_texture.index) {
        renderer_frontend->destroy_texture(material.normal_texture_handle);
      }
    }
  }

  for (u32 i = 0; i < mesh.draws.size; ++i) {
    renderer_frontend->destroy_buffer(mesh.draws[i].index_buffer);
  }
  mesh.draws.shutdown();

  renderer_frontend->destroy_buffer(mesh.vertex_buffer);

  // meshes.delete_swap(mesh_index);
}

void Scene::update(RenderPacket *packet) {
  packet->meshes = meshes.data;
  packet->mesh_count = meshes.size;

  for (u32 m = 0; m < meshes.size; ++m) {
    Node &mesh_node = node_hierarchy.nodes[node_hierarchy.mesh_node_indices[m]];
    for (u32 i = 0; i < meshes[m].draws.size; ++i) {
      packet->meshes[m].draws[i].transform =
          node_hierarchy.world_transforms[mesh_node.first_child_index + i];
    }
  }
}

} // namespace Helix
