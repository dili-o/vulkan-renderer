#include "Renderer/Scene.hpp"
#include "Containers/RingQueue.hpp"
#include "Core/Job.hpp"
#include "Core/Log.hpp"
#include "Platform/File.hpp"
#include "Renderer/ImguiFrontend.hpp"
#include "Renderer/RendererFrontEnd.hpp"
#include "Renderer/RendererTypes.hpp"

#include <cstdint>
#include <glm/gtx/hash.hpp>
#include <stb_image.h>
#include <tiny_obj_loader.h>
#include <tracy/Tracy.hpp>

namespace std {
template <> struct hash<Helix::Vertex> {
  size_t operator()(Helix::Vertex const &vertex) const {
    size_t h1 = hash<glm::vec3>()(vertex.pos);
    size_t h2 = hash<glm::vec2>()(vertex.tex_coord);
    return h1 ^ (h2 << 1); // Combine hashes safely
  }
};

} // namespace std

namespace Helix {

struct TextureLoadRequest {
  char *file_path;
  FileReadResult read_result;
  u32 material_to_update_index{0};
  Array<PBRMaterial> *pbr_materials{nullptr};
};

struct TextureLoadSuccess {
  TextureCreation tex_creation{};
  void *texture_data{nullptr};
  u32 material_to_update_index{0};
  Array<PBRMaterial> *pbr_materials{nullptr};
};

bool load_texture_data(void *entry_data, void *result_data) {
  ZoneScopedC(0xFF00FF);
  TextureLoadRequest *request = (TextureLoadRequest *)entry_data;
  TextureLoadSuccess *res = (TextureLoadSuccess *)result_data;

  if (!FileService::open_read_file_binary(
          request->file_path, &request->read_result,
          &MemoryService::instance()->system_allocator)) {
    HERROR("Failed to open file: {}", request->file_path);
    return false;
  }

  i32 width;
  i32 height;
  i32 channel_count;
  u8 *texture_data = stbi_load_from_memory(
      (const stbi_uc *)request->read_result.data, request->read_result.size,
      &width, &height, &channel_count, 4);

  MemoryService::instance()->system_allocator.deallocate(
      request->read_result.data);
  MemoryService::instance()->system_allocator.deallocate(request->file_path);
  if (!texture_data) {
    HERROR("Unable to load texture data: {}", request->file_path);
    return false;
  }

  res->tex_creation.initial_data = texture_data;
  res->tex_creation.width = width;
  res->tex_creation.height = height;
  res->pbr_materials = request->pbr_materials;
  res->material_to_update_index = request->material_to_update_index;

  return true;
}

bool load_texture_success(void *result_data) {
  TextureLoadSuccess *res = (TextureLoadSuccess *)result_data;

  res->tex_creation.depth = 1;
  res->tex_creation.array_layer_count = 1;
  res->tex_creation.array_base_level = 0;
  res->tex_creation.mip_base_level = 0;
  res->tex_creation.usage =
      TextureUsage::Enum(TextureUsage::TransferDest | TextureUsage::Sampled |
                         TextureUsage::TransferSrc);
  res->tex_creation.format = TextureFormat::R8G8B8A8_SRGB;
  res->tex_creation.type = TextureType::Texture2D;

  u32 w = res->tex_creation.width;
  u32 h = res->tex_creation.height;
  u32 mip_levels = 1;

  while (w > 1 && h > 1) {
    w /= 2;
    h /= 2;

    ++mip_levels;
  }
  res->tex_creation.mip_level_count = mip_levels;

  RendererFrontEnd *renderer_frontend = RendererFrontEnd::instance();
  TextureHandle handle = renderer_frontend->create_texture(res->tex_creation);
  (*res->pbr_materials)[res->material_to_update_index].albedo_texture_handle =
      handle;

  free(res->tex_creation.initial_data);
  return true;
}

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
  local_transforms.init(&MemoryService::instance()->system_allocator, 32);
  world_transforms.init(&MemoryService::instance()->system_allocator, 32);

  point_light_info.init(&MemoryService::instance()->system_allocator, 32);
  point_light_nodes.init(&MemoryService::instance()->system_allocator, 32);

  string_buffer.init(&MemoryService::instance()->system_allocator, hkilo(5));
}

void NodeHierarchy::shutdown() {
  root_node_indices.shutdown();
  nodes.shutdown();
  local_transforms.shutdown();
  world_transforms.shutdown();

  point_light_info.shutdown();
  point_light_nodes.shutdown();

  string_buffer.shutdown();
}

u32 NodeHierarchy::add_node(cstring name, u32 parent_index) {
  if (parent_index != INVALID_NODE_ID) {
    Node &parent_node = nodes[parent_index];
    if (parent_node.first_child_index == INVALID_NODE_ID)
      parent_node.first_child_index = nodes.size;
    ++parent_node.children_count;
  } else {
    root_node_indices.push(nodes.size);
  }

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
    if (node.children_count) {
      for (u32 i = 0; i < node.children_count; ++i) {
        draw_node(i + node.first_child_index);
      }
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
  string_buffer.init(&MemoryService::instance()->system_allocator, hmega(1));
  pbr_materials.init(&MemoryService::instance()->system_allocator, 25);

  // Default material
  PBRMaterial &pbr_material = pbr_materials.push_use();
  pbr_material.albedo_texture_handle =
      RendererFrontEnd::instance()->default_texture;
}

void Scene::shutdown() {
  u32 mesh_size = meshes.size;
  for (u32 i = 0; i < mesh_size; ++i) {
    unload_mesh(i);
  }

  // TODO: Right now some .obj models (bistro interior and exterior) have shapes
  // that use multiple materials. Deleting all materials right (calls delete on
  // already deleted materials)
  for (PBRMaterial &mat : pbr_materials) {
    RendererFrontEnd::instance()->destroy_texture(mat.albedo_texture_handle);
  }
  pbr_materials.shutdown();
  string_buffer.shutdown();
  meshes.shutdown();
  node_hierarchy.shutdown();
}

bool Scene::load_mesh(cstring path, cstring model) {

  RendererFrontEnd *renderer_frontend = RendererFrontEnd::instance();
  Directory dir{};
  FileService::current_directory(&dir);
  FileService::change_directory(path);

  tinyobj::attrib_t attrib;
  std::vector<tinyobj::shape_t> shapes;
  std::vector<tinyobj::material_t> materials;
  std::string warn, err;

  bool res =
      tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err, model, path);
  if (!warn.empty()) {
    HWARN("TINYOBJLOADER: {}", warn);
  }
  if (!err.empty()) {
    HERROR("TINYOBJLOADER: {}", err);
  }
  if (!res) {
    HERROR("TINYOBJLOADER: Failed to load .obj");
  }

  HDEBUG("# of vertices = {}", attrib.vertices.size() / 3);
  HDEBUG("# of normals = {}", attrib.normals.size() / 3);
  HDEBUG("# of texcoords = {}", attrib.texcoords.size() / 2);
  HINFO("# of materials = {}", materials.size());
  HDEBUG("# of shapes = {}", shapes.size());
  HDEBUG("# of indices in shape[0]: {}", shapes[0].mesh.indices.size());
  HDEBUG("# of materials in shape[0]: {}", shapes[0].mesh.material_ids.size());
  HDEBUG("# of faces in shape[0]: {}", shapes[0].mesh.num_face_vertices.size());

  HeapAllocator *allocator = &MemoryService::instance()->system_allocator;
  StackAllocator *stack_allocator = &MemoryService::instance()->stack_allocator;

  // TODO: Create materials for meshes without a diffuse_texname
  u32 previous_material_size = pbr_materials.size;
  for (u32 i = 0; i < materials.size(); ++i) {
    PBRMaterial &pbr_material = pbr_materials.push_use();
    tinyobj::material_t &material = materials[i];
    if (!material.diffuse_texname.empty()) {

      pbr_material.albedo_texture_handle = renderer_frontend->default_texture;
      char *file_full_path =
          string_concat(path, material.diffuse_texname.c_str(), allocator);
      TextureLoadRequest load_request{};
      load_request.file_path = file_full_path;
      load_request.pbr_materials = &pbr_materials;
      load_request.material_to_update_index = pbr_materials.size - 1;

      JobInfo info = create_job_info(
          load_texture_data, load_texture_success, nullptr, &load_request,
          sizeof(TextureLoadRequest), sizeof(TextureLoadSuccess),
          JobType::General, JobPriority::Medium);

      TextureLoadSuccess *res_data = (TextureLoadSuccess *)info.result_data;
      res_data->tex_creation.name = renderer_frontend->string_buffer.append_use(
          FileService::get_file_from_path(file_full_path));
      JobService::instance()->submit(info);
    } else {
      pbr_material.albedo_texture_handle = renderer_frontend->default_texture;
    }
  }

  FileService::change_directory(dir.path);

  Mesh &mesh = meshes.push_use();
  mesh.draws.init(allocator, shapes.size(), shapes.size());

  cstring model_name = renderer_frontend->string_buffer.append_use(
      FileService::get_file_from_path(model));

  node_hierarchy.add_node(model_name, INVALID_NODE_ID);

  std::unordered_map<Vertex, uint32_t> unique_vertices{};
  Array<Vertex> vertices{};
  vertices.init(stack_allocator, attrib.vertices.size() / 3);
  Array<u32> indices{};
  indices.init(stack_allocator, vertices.capacity / 2);

  // Used for mesh sorting
  u32 opaque_index = 0;
  u32 transparent_index = mesh.draws.size - 1;
  for (u32 i = 0; i < shapes.size(); ++i) {
    const auto &shape = shapes[i];
    cstring node_name = string_buffer.append_use_f("%s", shape.name.c_str());
    node_hierarchy.add_node(
        node_name,
        node_hierarchy
            .root_node_indices[node_hierarchy.root_node_indices.size - 1]);

    for (size_t face = 0; face < shape.mesh.num_face_vertices.size(); ++face) {
      i32 material_id = shape.mesh.material_ids[face];
      i32 face_vertex = shape.mesh.num_face_vertices[face];
    }
    for (const auto &index : shape.mesh.indices) {
      Vertex vertex{};

      vertex.pos = {attrib.vertices[3 * index.vertex_index + 0],
                    attrib.vertices[3 * index.vertex_index + 1],
                    attrib.vertices[3 * index.vertex_index + 2]};

      if (index.texcoord_index != -1) {
        vertex.tex_coord = {attrib.texcoords[2 * index.texcoord_index + 0],
                            1.0f -
                                attrib.texcoords[2 * index.texcoord_index + 1]};
      }
      if (unique_vertices.count(vertex) == 0) {
        unique_vertices[vertex] = static_cast<u32>(vertices.size);
        vertices.push(vertex);
      }
      indices.push(unique_vertices[vertex]);
    }

    u32 mesh_index;
    u32 material_index;

    // Arrange meshes based on transparency
    if ((shape.mesh.material_ids[0] != -1)) {
      if (!materials[shape.mesh.material_ids[0]].diffuse_texname.empty()) {
        material_index =
            shape.mesh.material_ids[0] +
            previous_material_size; // Adding 1 here because 0 is the
                                    // default material in the scene

        if (materials[shape.mesh.material_ids[0]].dissolve > 0.f) {
          mesh_index = transparent_index--;
        } else {
          mesh_index = opaque_index++;
        }
      } else {
        material_index = 0;
        mesh_index = opaque_index++;
      }
    } else {
      material_index = 0;
      mesh_index = opaque_index++;
    }

    BufferCreation creation{};
    creation.usage_flags =
        (BufferUsage::Enum)(BufferUsage::Index | BufferUsage::TransferDest);
    creation.memory_state_flags = MemoryState::Static;
    creation.memory_access_flags = MemoryAccess::GPU_ONLY;
    creation.size = sizeof(u32) * indices.size;
    creation.initial_data = indices.data;
    creation.name = renderer_frontend->string_buffer.append_use_f(
        "%s_IndexBuffer", node_name);

    BufferHandle index_buffer_handle =
        renderer_frontend->create_buffer(creation);

    MeshDraw &mesh_draw = mesh.draws[mesh_index];
    mesh_draw.primitive_count = shape.mesh.indices.size();
    mesh_draw.index_buffer = index_buffer_handle;
    mesh_draw.material_index = material_index;
    indices.clear();
  }

  BufferCreation creation{};
  creation.usage_flags =
      (BufferUsage::Enum)(BufferUsage::Vertex | BufferUsage::TransferDest);
  creation.memory_state_flags = MemoryState::Static;
  creation.memory_access_flags = MemoryAccess::GPU_ONLY;
  creation.size = sizeof(Vertex) * vertices.size;
  creation.initial_data = vertices.data;
  creation.name = "Model_Vertex_Buffer";

  mesh.vertex_buffer = renderer_frontend->create_buffer(creation);

  // HDEBUG("Vertex size = {}", vertices.size);

  return true;
}

void Scene::unload_mesh(u32 mesh_index) {
  RendererFrontEnd *renderer_frontend = RendererFrontEnd::instance();

  Mesh &mesh = meshes[mesh_index];

  for (u32 i = 0; i < mesh.draws.size; ++i) {
    // Skip the default material
    if (mesh.draws[i].material_index != 0) {
      PBRMaterial &material = pbr_materials[mesh.draws[i].material_index];
      // TODO: remove
      if (material.albedo_texture_handle.index !=
          renderer_frontend->default_texture.index) {
        renderer_frontend->destroy_texture(material.albedo_texture_handle);
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
    for (u32 i = 0; i < meshes[m].draws.size; ++i) {
      packet->meshes[m].draws[i].transform =
          node_hierarchy
              .world_transforms[node_hierarchy.root_node_indices[m] + 1];
    }
  }
}

} // namespace Helix
