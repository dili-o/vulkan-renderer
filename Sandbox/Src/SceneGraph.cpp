#include "SceneGraph.hpp"
#include "Containers/RingQueue.hpp"
#include "Core/Log.hpp"
#include "Core/Memory.hpp"
#include "Platform/File.hpp"
#include "Renderer/RendererFrontEnd.hpp"
// Vendor
#include <fastgltf/core.hpp>
#include <fastgltf/math.hpp>
#include <fastgltf/tools.hpp>
#include <fastgltf/types.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <imgui_internal.h>

static void gltf_set_vec3(glm::vec3 &glm_vec,
                          const fastgltf::math::fvec3 &f_vec) {
  glm_vec.x = f_vec.data()[0];
  glm_vec.y = f_vec.data()[1];
  glm_vec.z = f_vec.data()[2];
}

static void gltf_set_quat(glm::quat &glm_vec,
                          const fastgltf::math::fquat &f_vec) {
  glm_vec.x = f_vec.data()[0];
  glm_vec.y = f_vec.data()[1];
  glm_vec.z = f_vec.data()[2];
  glm_vec.w = f_vec.data()[3];
}

static glm::mat4 gltf_get_matrix4x4(const fastgltf::math::fmat4x4 &m) {
  return glm::mat4(m[0][0], m[0][1], m[0][2], m[0][3], m[1][0], m[1][1],
                   m[1][2], m[1][3], m[2][0], m[2][1], m[2][2], m[2][3],
                   m[3][0], m[3][1], m[3][2], m[3][3]);
}

void Scene::init(hlx::Allocator *allocator) {
  local_transforms.init(allocator, 4);
  global_transforms.init(allocator, 4);
  hierarchy.init(allocator, 4);
  for (u32 i = 0; i < MAX_NODE_LEVEL; ++i) {
    transform_changed[i].init(allocator, 4);
  }

  // mesh_to_node.init(allocator, 4);
  node_to_name.init(allocator, 4);
  node_names.init(allocator, hmega(5));
}

void Scene::shutdown() {
  for (u32 i = 0; i < MAX_NODE_LEVEL; ++i) {
    transform_changed[i].shutdown();
  }

  local_transforms.shutdown();
  global_transforms.shutdown();
  hierarchy.shutdown();
  // mesh_to_node.shutdown();
  node_to_name.shutdown();
  node_names.shutdown();
}

cstring Scene::get_node_name(i32 node) { return node_to_name[node]; }

void Scene::mark_changed_node(i32 node) {
  const i32 level = hierarchy[node].level;
  HASSERT_MSG(level < MAX_NODE_LEVEL, "Level exceeds MAX_NODE_LEVEL!");
  transform_changed[level].push(node);

  // Recursively update all child nodes
  for (i32 c = hierarchy[node].first_child; c != -1;
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

i32 Scene::add_node(i32 parent, i32 level, cstring name) {
  const i32 node = (i32)hierarchy.size;
  local_transforms.push(glm::mat4(1.f));
  global_transforms.push(glm::mat4(1.f));
  hierarchy.push({parent});
  if (!name) {
    node_to_name.push(node_names.append_use_f("Node_%d", node));
  } else {
    node_to_name.push(node_names.append_use_f("%s", name));
  }

  if (parent > -1) {
    const i32 s = hierarchy[parent].first_child;
    if (s == -1) {
      hierarchy[parent].first_child = node;
      hierarchy[node].last_sibling = node;
    } else {
      i32 dest = hierarchy[s].last_sibling;
      if (dest <= -1) {
        for (dest = s; hierarchy[dest].next_sibling != -1;
             dest = hierarchy[dest].next_sibling)
          ;
      }
      hierarchy[dest].next_sibling = node;
      hierarchy[s].last_sibling = node;
    }
  }

  hierarchy[node].first_child = -1;
  hierarchy[node].next_sibling = -1;
  hierarchy[node].level = level;
  return node;
}

i32 SceneUI::render_scene_tree_ui(Scene &scene, i32 node) {
  cstring name = scene.get_node_name(node);
  // TODO: Use the StringBuffer
  std::string label =
      name ? name : (std::string("Node") + std::to_string(node)).c_str();
  const bool is_leaf = scene.hierarchy[node].first_child < 0;
  ImGuiTreeNodeFlags flags =
      is_leaf ? ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_Bullet
              : ImGuiTreeNodeFlags_OpenOnDoubleClick;
  if (node == selected_node) {
    flags |= ImGuiTreeNodeFlags_Selected;
  }

  ImVec4 color =
      is_leaf ? ImVec4(0.f, 1.f, 0.f, 1.f) : ImVec4(1.f, 1.f, 1.f, 1.f);
  ImGui::PushStyleColor(ImGuiCol_Text, color);
  const bool is_opened =
      ImGui::TreeNodeEx(&scene.hierarchy[node], flags, "%s", label.c_str());
  ImGui::PopStyleColor();
  ImGui::PushID(node);
  if (ImGui::IsItemClicked()) {
    selected_node = node;
  }
  if (is_opened) {
    for (i32 ch = scene.hierarchy[node].first_child; ch != -1;
         ch = scene.hierarchy[ch].next_sibling) {
      if (i32 sub_node = render_scene_tree_ui(scene, ch); sub_node > -1) {
        selected_node = sub_node;
      }
    }
    ImGui::TreePop();
  }

  ImGui::PopID();
  return selected_node;
}

void SceneUI::render_node_property_ui(Scene &scene, i32 node) {
  if (ImGui::Begin("Node Property")) {
    if (node > -1) {
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
      ImGui::DragFloat3("Scale", &local_transform.scale.x, 0.5f, 0.f, 0.f,
                        "%.3f");
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
        local_transform.scale.x = local_transform.scale.x < 0.0016f ? 0.0016f : local_transform.scale.x;
        local_transform.scale.y = std::max(0.0016f,
                                           local_transform.scale.y);
        local_transform.scale.z = std::max(0.0016f,
                                           local_transform.scale.z);
        scene.local_transforms[node] = local_transform.get_mat4();
        scene.mark_changed_node(node);
      }
    }
  }
  ImGui::End();
}

bool load_gltf_scene(Scene &scene, hlx::Array<hlx::MeshDraw> &mesh_draws,
                     hlx::UnifiedBuffer<Vertex> &vertex_buffer,
                     hlx::UnifiedBuffer<u32> &index_buffer, cstring path,
                     cstring file_name) {
  hlx::Directory current_dir{};
  hlx::FileService::current_directory(&current_dir);
  hlx::FileService::change_directory(path);

  hlx::HeapAllocator *allocator =
      &hlx::MemoryService::instance()->system_allocator;
  char *file_full_path = string_concat(path, file_name, allocator);
  std::filesystem::path std_path(file_full_path);

  // Parse the glTF file and get the constructed asset
  fastgltf::Parser parser(fastgltf::Extensions::None);

  constexpr auto gltf_options = fastgltf::Options::DontRequireValidAssetMember |
                                fastgltf::Options::AllowDouble |
                                fastgltf::Options::LoadExternalBuffers |
                                fastgltf::Options::GenerateMeshIndices;
  auto gltf_file = fastgltf::MappedGltfFile::FromPath(std_path);
  if (!bool(gltf_file)) {
    HERROR("Failed to open glTF file: {}",
           fastgltf::getErrorMessage(gltf_file.error()));
    return false;
  }

  fastgltf::Expected<fastgltf::Asset> asset =
      parser.loadGltf(gltf_file.get(), std_path.parent_path(), gltf_options);
  if (asset.error() != fastgltf::Error::None) {
    HERROR("Failed to open glTF file: {}",
           fastgltf::getErrorMessage(asset.error()));
    return false;
  }
  allocator->deallocate(file_full_path);

  hlx::ScopedAllocator scope_allocator(
      &hlx::MemoryService::instance()->stack_allocator);
  hlx::StackAllocator *stack_allocator = scope_allocator.allocator;

  hlx::Array<i32> node_parents{};
  node_parents.init(stack_allocator, asset->nodes.size(), asset->nodes.size());
  memset(node_parents.data, -1, sizeof(i32) * node_parents.size);

  hlx::Array<i32> gltf_to_hierarchy_node{};
  gltf_to_hierarchy_node.init(stack_allocator, asset->nodes.size(),
                              asset->nodes.size());
  memset(gltf_to_hierarchy_node.data, -1,
         sizeof(i32) * gltf_to_hierarchy_node.size);

  hlx::RingQueue<size_t> gltf_node_queue{};
  gltf_node_queue.init(stack_allocator, asset->nodes.size());

  fastgltf::Scene &root_scene = asset->scenes[asset->defaultScene.value()];
  i32 root_node_parent =
      root_scene.nodeIndices.size() == 1 ? 0 : scene.add_node(0, 0, nullptr);

  // Enqueue root nodes
  for (u32 i = 0; i < root_scene.nodeIndices.size(); ++i) {
    gltf_node_queue.enqueue(root_scene.nodeIndices[i]);
    fastgltf::Node &node = asset->nodes[root_scene.nodeIndices[i]];
    cstring node_name =
        node.name.empty()
            ? nullptr
            : scene.node_names.append_use_f("%s", node.name.c_str());

    gltf_to_hierarchy_node[root_scene.nodeIndices[i]] =
        scene.add_node(root_node_parent,
                       scene.hierarchy[root_node_parent].level + 1, node_name);
  }

  while (gltf_node_queue.size) {
    size_t gltf_node_index = UINT32_MAX;
    gltf_node_queue.dequeue(&gltf_node_index);

    fastgltf::Node &node = asset->nodes[gltf_node_index];
    i32 node_hierarchy_index = gltf_to_hierarchy_node[gltf_node_index];
    // Transform
    if (std::holds_alternative<fastgltf::TRS>(node.transform)) {
      const fastgltf::TRS &trs = std::get<fastgltf::TRS>(node.transform);

      hlx::Transform transform{};
      gltf_set_vec3(transform.position, trs.translation);
      gltf_set_quat(transform.rotation, trs.rotation);
      gltf_set_vec3(transform.scale, trs.scale);
      scene.local_transforms[node_hierarchy_index] = transform.get_mat4();
    } else if (std::holds_alternative<fastgltf::math::fmat4x4>(
                   node.transform)) {
      const fastgltf::math::fmat4x4 &mat =
          std::get<fastgltf::math::fmat4x4>(node.transform);

      hlx::Transform transform{};
      glm::mat4 matrix = gltf_get_matrix4x4(mat);
      transform.set_transform(matrix);
      scene.local_transforms[node_hierarchy_index] = transform.get_mat4();
    }

    // Add child nodes to the queue
    for (u32 i = 0; i < node.children.size(); ++i) {
      node_parents[node.children[i]] = node_hierarchy_index;
      gltf_node_queue.enqueue(node.children[i]);
      fastgltf::Node &child_node = asset->nodes[node.children[i]];
      cstring child_node_name =
          child_node.name.empty()
              ? nullptr
              : scene.node_names.append_use_f("%s", child_node.name.c_str());
      gltf_to_hierarchy_node[node.children[i]] = scene.add_node(
          node_hierarchy_index, scene.hierarchy[node_hierarchy_index].level + 1,
          child_node_name);
    }

    if (node.meshIndex.has_value()) {
      fastgltf::Mesh &gltf_mesh = asset->meshes[node.meshIndex.value()];

      // Single Vertex buffer for each mesh
      hlx::Array<Vertex> vertices{};
      hlx::Array<u32> indexes{};
      vertices.init(allocator, 4);
      indexes.init(allocator, 4);

      i32 node_level = scene.hierarchy[node_hierarchy_index].level + 1;

      for (u32 i = 0; i < gltf_mesh.primitives.size(); ++i) {
        fastgltf::Primitive &primitive = gltf_mesh.primitives[i];
        HASSERT_MSG(primitive.type == fastgltf::PrimitiveType::Triangles,
                    "Non-Triangle Primitive type");

        i32 draw_node =
            scene.add_node(node_hierarchy_index, node_level + 1,
                           scene.node_names.append_use_f("Mesh_%d", i));
        scene.mesh_to_node[draw_node] = (i32)mesh_draws.size;
        hlx::MeshDraw &mesh_draw = mesh_draws.push_use();

        u32 initial_vtx = vertices.size;
        u32 initial_idx = indexes.size;

        // Index buffer per primitive
        {
          fastgltf::Accessor &index_accessor =
              asset->accessors[primitive.indicesAccessor.value()];

          indexes.set_capacity(indexes.size + index_accessor.count);

          fastgltf::iterateAccessor<u32>(
              asset.get(), index_accessor,
              [&](std::uint32_t idx) { indexes.push(idx + initial_vtx); });
        }

        // load position vertices
        {
          fastgltf::Accessor &pos_accessor =
              asset->accessors[primitive.findAttribute("POSITION")
                                   ->accessorIndex];
          vertices.set_size(vertices.size + pos_accessor.count);

          fastgltf::iterateAccessorWithIndex<fastgltf::math::fvec3>(
              asset.get(), pos_accessor,
              [&](fastgltf::math::fvec3 v, size_t index) {
                Vertex vertex{};
                vertex.position.x = v.data()[0];
                vertex.position.y = v.data()[1];
                vertex.position.z = v.data()[2];
                vertices[initial_vtx + index] = vertex;
              });
        }

        // load normal vertices
        {
          fastgltf::Accessor &normal_accessor =
              asset
                  ->accessors[primitive.findAttribute("NORMAL")->accessorIndex];

          fastgltf::iterateAccessorWithIndex<fastgltf::math::fvec3>(
              asset.get(), normal_accessor,
              [&](fastgltf::math::fvec3 n, size_t index) {
                Vertex &vertex = vertices[initial_vtx + index];
                vertex.normal.x = n.data()[0];
                vertex.normal.y = n.data()[1];
                vertex.normal.z = n.data()[2];
              });
        }

        // load tex_coord vertices
        {
          fastgltf::Attribute *tex_attribute =
              primitive.findAttribute("TEXCOORD_0");
          if (tex_attribute != primitive.attributes.end()) {
            fastgltf::Accessor &tex_coord_accessor =
                asset->accessors[tex_attribute->accessorIndex];
            fastgltf::iterateAccessorWithIndex<fastgltf::math::fvec2>(
                asset.get(), tex_coord_accessor,
                [&](fastgltf::math::fvec2 uv, size_t index) {
                  Vertex &vertex = vertices[initial_vtx + index];
                  vertex.tex_coord.x = uv.data()[0];
                  vertex.tex_coord.y = uv.data()[1];
                });
          }
        }

        mesh_draw.primitive_count = indexes.size - initial_idx;
        mesh_draw.index_buffer_offset = initial_idx + index_buffer.current_size;
        mesh_draw.vertex_buffer_offset = vertex_buffer.current_size;
      }

      if (vertices.size) {
        // TODO: Make a function in RendererFrontEnd
        u64 upload_size = sizeof(Vertex) * vertices.size;
        hlx::RendererFrontEnd::instance()->copy_data_to_buffer(
            vertices.data, vertex_buffer.handle, vertex_buffer.size_in_bytes(),
            upload_size);
        vertex_buffer.current_size += vertices.size;
        HASSERT(vertex_buffer.current_size < hlx::max_vertex_count);

        upload_size = sizeof(u32) * indexes.size;
        hlx::RendererFrontEnd::instance()->copy_data_to_buffer(
            indexes.data, index_buffer.handle, index_buffer.size_in_bytes(),
            upload_size);
        index_buffer.current_size += indexes.size;
      }

      vertices.shutdown();
      indexes.shutdown();
    }
  }

  scene.mark_changed_node(root_node_parent);

  hlx::FileService::change_directory(current_dir.path);
  return true;
}
