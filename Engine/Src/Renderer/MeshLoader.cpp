#include "MeshLoader.hpp"
#include "Containers/Array.hpp"
#include "Core/Assert.hpp"
#include "Core/Job.hpp"
#include "Core/Log.hpp"
#include "Core/Memory.hpp"
#include "Core/Profiler.hpp"
#include "Platform/File.hpp"
#include "Renderer/GPUResources.hpp"
#include "Renderer/RendererFrontEnd.hpp"
#include "Renderer/Scene.hpp"
// Vendors
#include <fastgltf/core.hpp>
#include <fastgltf/math.hpp>
#include <fastgltf/tools.hpp>
#include <fastgltf/types.hpp>
#include <glm/gtx/hash.hpp>
#include <mikktspace.h>
#include <stb_image.h>
#include <tiny_obj_loader.h>

#pragma region mikktspace

struct SMikkTSpaceContextUserData {
  const Helix::Vertex *vertices;
  const Helix::Array<u32> &indices;
  Helix::Array<glm::vec4> &tangents;
};

int GetNumFaces(const SMikkTSpaceContext *context) {
  SMikkTSpaceContextUserData *user_data =
      (SMikkTSpaceContextUserData *)context->m_pUserData;
  return (i32)user_data->indices.size / 3;
}

int GetNumVerticesOfFace(const SMikkTSpaceContext *, int) { return 3; }

void GetPosition(const SMikkTSpaceContext *context, float pos[3], int faceIdx,
                 int vertIdx) {
  SMikkTSpaceContextUserData *user_data =
      (SMikkTSpaceContextUserData *)context->m_pUserData;
  int index = user_data->indices[faceIdx * 3 + vertIdx];
  const glm::vec3 &position = user_data->vertices[index].pos;
  pos[0] = position.x;
  pos[1] = position.y;
  pos[2] = position.z;
}

void GetNormal(const SMikkTSpaceContext *context, float norm[3], int faceIdx,
               int vertIdx) {
  SMikkTSpaceContextUserData *user_data =
      (SMikkTSpaceContextUserData *)context->m_pUserData;
  int index = user_data->indices[faceIdx * 3 + vertIdx];
  const glm::vec3 &normal = user_data->vertices[index].normal;
  norm[0] = normal.x;
  norm[1] = normal.y;
  norm[2] = normal.z;
}

void GetTexCoord(const SMikkTSpaceContext *context, float uv[2], int faceIdx,
                 int vertIdx) {
  SMikkTSpaceContextUserData *user_data =
      (SMikkTSpaceContextUserData *)context->m_pUserData;
  int index = user_data->indices[faceIdx * 3 + vertIdx];
  const glm::vec2 &texCoord = user_data->vertices[index].tex_coord;
  uv[0] = texCoord.x;
  uv[1] = texCoord.y;
}

void SetTSpaceBasic(const SMikkTSpaceContext *context, const float tangent[3],
                    float sign, int faceIdx, int vertIdx) {
  SMikkTSpaceContextUserData *user_data =
      (SMikkTSpaceContextUserData *)context->m_pUserData;
  int index = user_data->indices[faceIdx * 3 + vertIdx];
  user_data->tangents[index] =
      glm::vec4(tangent[0], tangent[1], tangent[2], sign);
}

#pragma endregion mikktspace

namespace std {
template <> struct hash<Helix::Vertex> {
  size_t operator()(Helix::Vertex const &vertex) const {
    size_t h1 = hash<glm::vec3>()(vertex.pos);
    size_t h2 = hash<glm::vec3>()(vertex.normal);
    size_t h3 = hash<glm::vec4>()(vertex.tangent);
    size_t h4 = hash<glm::vec2>()(vertex.tex_coord);

    size_t combined = h1;
    combined ^= h2 + 0x9e3779b9 + (combined << 6) + (combined >> 2);
    combined ^= h3 + 0x9e3779b9 + (combined << 6) + (combined >> 2);
    combined ^= h4 + 0x9e3779b9 + (combined << 6) + (combined >> 2);
    return combined;
  }
};

} // namespace std

namespace Helix {

enum MaterialAttribute { MaterialAttribute_Albedo, MaterialAttribute_Normal };

struct TextureLoadRequest {
  char *file_path;
  FileReadResult read_result;
};

struct TextureLoadSuccess {
  TextureCreation tex_creation{};
  void *texture_data{nullptr};
  u32 material_to_update_index{0};
  MaterialAttribute attribute_to_update;
  Array<PBRMaterial> *pbr_materials{nullptr};
};

// TODO: Specify the image component type
bool load_texture_data(void *entry_data, void *result_data) {
  HELIX_PROFILER_FUNCTION_COLOR(0xFF00FF);
  TextureLoadRequest *request = (TextureLoadRequest *)entry_data;
  TextureLoadSuccess *res = (TextureLoadSuccess *)result_data;

  // Only load the file if it is not already loaded
  if (request->read_result.size == 0)
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
    HERROR("Unable to load texture data: {}, Reason: {}",
           res->tex_creation.name, stbi_failure_reason());
    return false;
  }

  res->tex_creation.initial_data = texture_data;
  res->tex_creation.width = width;
  res->tex_creation.height = height;

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

  switch (res->attribute_to_update) {
  case MaterialAttribute_Albedo: {
    res->tex_creation.format = TextureFormat::R8G8B8A8_SRGB;
    TextureHandle handle = renderer_frontend->create_texture(res->tex_creation);
    (*res->pbr_materials)[res->material_to_update_index].albedo_texture_handle =
        handle;
  } break;
  case MaterialAttribute_Normal: {
    res->tex_creation.format = TextureFormat::R8G8B8A8_UNORM;
    TextureHandle handle = renderer_frontend->create_texture(res->tex_creation);
    (*res->pbr_materials)[res->material_to_update_index].normal_texture_handle =
        handle;
  } break;
  }

  free(res->tex_creation.initial_data);
  return true;
}

// TODO: Add tangent calculation
bool load_obj_mesh(Scene *scene, cstring path, cstring model) {
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
  size_t stack_marker = stack_allocator->get_marker();

  u32 previous_material_size = scene->pbr_materials.size;
  for (u32 i = 0; i < materials.size(); ++i) {
    PBRMaterial &pbr_material = scene->pbr_materials.push_use();
    tinyobj::material_t &material = materials[i];
    if (!material.diffuse_texname.empty()) {

      pbr_material.albedo_texture_handle =
          renderer_frontend->default_albedo_texture;
      char *file_full_path =
          string_concat(path, material.diffuse_texname.c_str(), allocator);
      TextureLoadRequest load_request{};
      load_request.file_path = file_full_path;

      JobInfo info = create_job_info(
          load_texture_data, load_texture_success, nullptr, &load_request,
          sizeof(TextureLoadRequest), sizeof(TextureLoadSuccess),
          JobType::General, JobPriority::Medium);

      TextureLoadSuccess *res_data = (TextureLoadSuccess *)info.result_data;
      res_data->tex_creation.name = renderer_frontend->string_buffer.append_use(
          FileService::get_file_from_path(file_full_path));
      res_data->pbr_materials = &scene->pbr_materials;
      res_data->material_to_update_index = scene->pbr_materials.size - 1;
      JobService::instance()->submit(info);
    } else {
      u8 def_colour[4];
      def_colour[0] =
          static_cast<u8>(std::clamp(material.diffuse[0], 0.0f, 1.0f) * 255.0f);
      def_colour[1] =
          static_cast<u8>(std::clamp(material.diffuse[1], 0.0f, 1.0f) * 255.0f);
      def_colour[2] =
          static_cast<u8>(std::clamp(material.diffuse[2], 0.0f, 1.0f) * 255.0f);
      def_colour[3] =
          static_cast<u8>(std::clamp(material.dissolve, 0.0f, 1.0f) * 255.0f);

      TextureCreation tex_creation{};
      tex_creation.initial_data = def_colour;

      tex_creation.usage = TextureUsage::Enum(TextureUsage::TransferDest |
                                              TextureUsage::Sampled);
      tex_creation.format = TextureFormat::R8G8B8A8_SRGB;
      tex_creation.type = TextureType::Texture2D;
      tex_creation.name =
          scene->string_buffer.append_use_f("%s", material.name.c_str());

      pbr_material.albedo_texture_handle =
          renderer_frontend->create_texture(tex_creation);
    }
  }

  FileService::change_directory(dir.path);

  Mesh &mesh = scene->meshes.push_use();
  mesh.draws.init(allocator, shapes.size(), shapes.size());

  cstring model_name = renderer_frontend->string_buffer.append_use(
      FileService::get_file_from_path(model));

  u32 node_hierarchy_index =
      scene->node_hierarchy.add_node(model_name, INVALID_NODE_ID, true);

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
    cstring primitive_name =
        scene->string_buffer.append_use_f("%s", shape.name.c_str());

    scene->node_hierarchy.add_node(primitive_name, node_hierarchy_index);

    for (const auto &index : shape.mesh.indices) {
      Vertex vertex{};

      vertex.pos = {attrib.vertices[3 * index.vertex_index + 0],
                    attrib.vertices[3 * index.vertex_index + 1],
                    attrib.vertices[3 * index.vertex_index + 2]};

      if (index.texcoord_index != -1) {
        vertex.tex_coord = {attrib.texcoords[2 * index.texcoord_index + 0],
                            1.0f -
                                attrib.texcoords[2 * index.texcoord_index + 1]};
      } else {
        HERROR("No Texcoords");
        vertex.tex_coord = {0.f, 0.f};
      }
      if (index.normal_index != -1) {
        vertex.normal = {attrib.normals[3 * index.normal_index + 0],
                         attrib.normals[3 * index.normal_index + 1],
                         attrib.normals[3 * index.normal_index + 2]};
      } else {
        HERROR("No normals");
      }

      if (unique_vertices.count(vertex) == 0) {
        unique_vertices[vertex] = static_cast<u32>(vertices.size);
        vertices.push(vertex);
      }
      indices.push(unique_vertices[vertex]);
    }

    u32 mesh_index;
    u32 material_index = shape.mesh.material_ids[0] + previous_material_size;

    // Arrange meshes based on transparency
    if ((shape.mesh.material_ids[0] != -1)) {
      if (!materials[shape.mesh.material_ids[0]].diffuse_texname.empty()) {

        if (materials[shape.mesh.material_ids[0]].dissolve > 0.f) {
          mesh_index = transparent_index--;
        } else {
          mesh_index = opaque_index++;
        }
      } else {
        mesh_index = opaque_index++;
      }
    } else {
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
        "%s_IndexBuffer", primitive_name);

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
  vertices.shutdown();
  stack_allocator->free_marker(stack_marker);

  return true;
}

void gltf_load_pbr_texture(Scene *scene, fastgltf::Asset &asset,
                           fastgltf::Texture &texture, cstring texture_path,
                           MaterialAttribute attribute) {
  HeapAllocator *allocator = &MemoryService::instance()->system_allocator;
  RendererFrontEnd *renderer_frontend = RendererFrontEnd::instance();

  cstring texture_name = nullptr;
  if (!texture.name.empty()) {
    texture_name =
        renderer_frontend->string_buffer.append_use(texture.name.c_str());
  }

  fastgltf::Image &image = asset.images[texture.imageIndex.value()];
  if (!image.name.empty()) {
    texture_name =
        renderer_frontend->string_buffer.append_use(image.name.c_str());
  }
  std::visit(
      fastgltf::visitor{
          [](auto &arg) {
            HERROR("Current image type: {} is not yet supported",
                   typeid(arg).name());
          },
          [&](fastgltf::sources::Array &array) {
            TextureLoadRequest load_request{};
            load_request.file_path = nullptr;
            load_request.read_result.size = array.bytes.size();
            load_request.read_result.data =
                (char *)halloca(array.bytes.size(), allocator);
            memcpy(load_request.read_result.data, array.bytes.data(),
                   array.bytes.size());

            JobInfo info = create_job_info(
                load_texture_data, load_texture_success, nullptr, &load_request,
                sizeof(TextureLoadRequest), sizeof(TextureLoadSuccess),
                JobType::General, JobPriority::Medium);

            TextureLoadSuccess *res_data =
                (TextureLoadSuccess *)info.result_data;
            // TODO: Name after asset
            res_data->tex_creation.name =
                texture_name ? texture_name
                             : renderer_frontend->string_buffer.append_use_f(
                                   "texture_%d", scene->pbr_materials.size);
            res_data->attribute_to_update = attribute;
            res_data->pbr_materials = &scene->pbr_materials;
            res_data->material_to_update_index = scene->pbr_materials.size - 1;

            JobService::instance()->submit(info);
          },
          [&](fastgltf::sources::URI &filePath) {
            HASSERT_MSG(
                filePath.fileByteOffset == 0,
                "Gltf filePath.uri has a byteOffset"); // We don't support
                                                       // offsets with stbi.

            HASSERT_MSG(
                filePath.uri.isLocalPath(),
                "Gltf filePath.uri is not local"); // We're only capable of
                                                   // loading local files.

            TextureLoadRequest load_request{};
            load_request.file_path =
                string_concat(texture_path, filePath.uri.c_str(), allocator);

            JobInfo info = create_job_info(
                load_texture_data, load_texture_success, nullptr, &load_request,
                sizeof(TextureLoadRequest), sizeof(TextureLoadSuccess),
                JobType::General, JobPriority::Medium);

            TextureLoadSuccess *res_data =
                (TextureLoadSuccess *)info.result_data;
            res_data->tex_creation.name =
                texture_name ? texture_name
                             : renderer_frontend->string_buffer.append_use(
                                   filePath.uri.c_str());
            res_data->attribute_to_update = attribute;
            res_data->pbr_materials = &scene->pbr_materials;
            res_data->material_to_update_index = scene->pbr_materials.size - 1;
            JobService::instance()->submit(info);
          },

          [&](fastgltf::sources::BufferView &view) {
            auto &bufferView = asset.bufferViews[view.bufferViewIndex];
            auto &buffer = asset.buffers[bufferView.bufferIndex];

            std::visit(
                fastgltf::visitor{
                    // We only care about VectorWithMime here, because
                    // we specify LoadExternalBuffers, meaning all
                    // buffers are already loaded into a vector.
                    [](auto &arg) {
                      HERROR("Current image type: {} is not yet supported",
                             typeid(arg).name());
                    },
                    [&](fastgltf::sources::Array &array) {
                      TextureLoadRequest load_request{};
                      load_request.file_path = nullptr;
                      load_request.read_result.size = bufferView.byteLength;
                      load_request.read_result.data =
                          (char *)halloca(bufferView.byteLength, allocator);
                      memcpy(load_request.read_result.data,
                             array.bytes.data() + bufferView.byteOffset,
                             bufferView.byteLength);

                      JobInfo info = create_job_info(
                          load_texture_data, load_texture_success, nullptr,
                          &load_request, sizeof(TextureLoadRequest),
                          sizeof(TextureLoadSuccess), JobType::General,
                          JobPriority::Medium);

                      TextureLoadSuccess *res_data =
                          (TextureLoadSuccess *)info.result_data;
                      // TODO: Name after asset
                      res_data->tex_creation.name =
                          texture_name
                              ? texture_name
                              : renderer_frontend->string_buffer.append_use_f(
                                    "texture_%d", scene->pbr_materials.size);
                      res_data->attribute_to_update = attribute;
                      res_data->pbr_materials = &scene->pbr_materials;
                      res_data->material_to_update_index =
                          scene->pbr_materials.size - 1;
                      JobService::instance()->submit(info);
                    },
                    [&](fastgltf::sources::Vector &vector) {
                      TextureLoadRequest load_request{};
                      load_request.file_path = nullptr;
                      load_request.read_result.size = bufferView.byteLength;
                      load_request.read_result.data =
                          (char *)halloca(bufferView.byteLength, allocator);
                      memcpy(load_request.read_result.data,
                             vector.bytes.data() + bufferView.byteOffset,
                             bufferView.byteLength);

                      JobInfo info = create_job_info(
                          load_texture_data, load_texture_success, nullptr,
                          &load_request, sizeof(TextureLoadRequest),
                          sizeof(TextureLoadSuccess), JobType::General,
                          JobPriority::Medium);

                      TextureLoadSuccess *res_data =
                          (TextureLoadSuccess *)info.result_data;
                      // TODO: Name after asset
                      res_data->tex_creation.name =
                          texture_name
                              ? texture_name
                              : renderer_frontend->string_buffer.append_use_f(
                                    "texture_%d", scene->pbr_materials.size);
                      res_data->attribute_to_update = attribute;
                      res_data->pbr_materials = &scene->pbr_materials;
                      res_data->material_to_update_index =
                          scene->pbr_materials.size - 1;
                      JobService::instance()->submit(info);
                    }},
                buffer.data);
          },
      },
      image.data);
}

void gltf_set_vec2(glm::vec2 &glm_vec, const fastgltf::math::fvec2 &f_vec) {
  glm_vec.x = f_vec.data()[0];
  glm_vec.y = f_vec.data()[1];
}

void gltf_set_vec3(glm::vec3 &glm_vec, const fastgltf::math::fvec3 &f_vec) {
  glm_vec.x = f_vec.data()[0];
  glm_vec.y = f_vec.data()[1];
  glm_vec.z = f_vec.data()[2];
}

void gltf_set_vec4(glm::vec4 &glm_vec, const fastgltf::math::fvec4 &f_vec) {
  glm_vec.x = f_vec.data()[0];
  glm_vec.y = f_vec.data()[1];
  glm_vec.z = f_vec.data()[2];
  glm_vec.w = f_vec.data()[3];
}

void gltf_set_quat(glm::quat &glm_vec, const fastgltf::math::fquat &f_vec) {
  glm_vec.x = f_vec.data()[0];
  glm_vec.y = f_vec.data()[1];
  glm_vec.z = f_vec.data()[2];
  glm_vec.w = f_vec.data()[3];
}

glm::mat4 gltf_get_matrix4x4(const fastgltf::math::fmat4x4 &m) {
  return glm::mat4(m[0][0], m[0][1], m[0][2], m[0][3], m[1][0], m[1][1],
                   m[1][2], m[1][3], m[2][0], m[2][1], m[2][2], m[2][3],
                   m[3][0], m[3][1], m[3][2], m[3][3]);
}

bool load_gltf_mesh(Scene *scene, cstring path, cstring model) {

  Directory dir{};
  FileService::current_directory(&dir);
  FileService::change_directory(path);

  HeapAllocator *allocator = &MemoryService::instance()->system_allocator;
  char *file_full_path = string_concat(path, model, allocator);
  std::filesystem::path std_path(file_full_path);

  // Parse the glTF file and get the constructed asset
  static constexpr auto supportedExtensions =
      fastgltf::Extensions::KHR_mesh_quantization |
      fastgltf::Extensions::KHR_texture_transform |
      fastgltf::Extensions::KHR_materials_variants;

  fastgltf::Parser parser(supportedExtensions);

  constexpr auto gltfOptions = fastgltf::Options::DontRequireValidAssetMember |
                               fastgltf::Options::AllowDouble |
                               fastgltf::Options::LoadExternalBuffers |
                               fastgltf::Options::GenerateMeshIndices;

  auto gltfFile = fastgltf::MappedGltfFile::FromPath(std_path);
  if (!bool(gltfFile)) {
    HERROR("Failed to open glTF file: {}",
           fastgltf::getErrorMessage(gltfFile.error()));
    return false;
  }

  fastgltf::Expected<fastgltf::Asset> asset =
      parser.loadGltf(gltfFile.get(), std_path.parent_path(), gltfOptions);
  if (asset.error() != fastgltf::Error::None) {
    HERROR("Failed to open glTF file: {}",
           fastgltf::getErrorMessage(asset.error()));
    return false;
  }

  RendererFrontEnd *renderer_frontend = RendererFrontEnd::instance();

  // TODO: Load the default material if it has one
  u32 previous_material_size = scene->pbr_materials.size;
  for (fastgltf::Material &material : asset->materials) {
    PBRMaterial &pbr_material = scene->pbr_materials.push_use();

    if (material.pbrData.baseColorTexture.has_value()) {

      gltf_load_pbr_texture(
          scene, asset.get(),
          asset->textures[material.pbrData.baseColorTexture.value()
                              .textureIndex],
          path, MaterialAttribute_Albedo);

      pbr_material.albedo_texture_handle =
          renderer_frontend->default_albedo_texture;

    } else {
      u8 def_colour[4];
      f32 *albedo_colour = material.pbrData.baseColorFactor.data();
      def_colour[0] =
          static_cast<u8>(std::clamp(albedo_colour[0], 0.0f, 1.0f) * 255.0f);
      def_colour[1] =
          static_cast<u8>(std::clamp(albedo_colour[1], 0.0f, 1.0f) * 255.0f);
      def_colour[2] =
          static_cast<u8>(std::clamp(albedo_colour[2], 0.0f, 1.0f) * 255.0f);
      def_colour[3] =
          static_cast<u8>(std::clamp(albedo_colour[3], 0.0f, 1.0f) * 255.0f);

      TextureCreation tex_creation{};
      tex_creation.initial_data = def_colour;

      tex_creation.usage = TextureUsage::Enum(TextureUsage::TransferDest |
                                              TextureUsage::Sampled);
      tex_creation.format = TextureFormat::R8G8B8A8_SRGB;
      tex_creation.type = TextureType::Texture2D;
      tex_creation.name =
          scene->string_buffer.append_use_f("%s", material.name.c_str());

      pbr_material.albedo_texture_handle =
          renderer_frontend->create_texture(tex_creation);
    }

    // Normal Texture
    pbr_material.normal_texture_handle =
        renderer_frontend->default_normal_texture;
    if (material.normalTexture.has_value()) {

      gltf_load_pbr_texture(
          scene, asset.get(),
          asset->textures[material.normalTexture.value().textureIndex], path,
          MaterialAttribute_Normal);
    }
  }
  allocator->deallocate(file_full_path);

  StackAllocator *stack_allocator = &MemoryService::instance()->stack_allocator;
  size_t stack_marker = stack_allocator->get_marker();

  fastgltf::Scene &root_scene = asset->scenes[asset->defaultScene.value()];

  Array<u32> node_parents{};
  node_parents.init(stack_allocator, asset->nodes.size(), asset->nodes.size());
  memset(node_parents.data, INVALID_NODE_ID, sizeof(u32) * node_parents.size);

  Array<u32> gltf_to_hierarchy_node{};
  gltf_to_hierarchy_node.init(stack_allocator, asset->nodes.size(),
                              asset->nodes.size());
  memset(gltf_to_hierarchy_node.data, INVALID_NODE_ID,
         sizeof(u32) * gltf_to_hierarchy_node.size);

  RingQueue<u32> node_queue{};
  node_queue.init(stack_allocator, asset->nodes.size());

  u32 old_root_node_size = scene->node_hierarchy.root_node_indices.size;

  u32 root_node_parent =
      root_scene.nodeIndices.size() == 1
          ? INVALID_NODE_ID
          : scene->node_hierarchy.add_node(nullptr, INVALID_NODE_ID);

  // Enqueue root nodes
  for (u32 i = 0; i < root_scene.nodeIndices.size(); ++i) {
    node_queue.enqueue((u32)root_scene.nodeIndices[i]);
    fastgltf::Node &node = asset->nodes[root_scene.nodeIndices[i]];
    cstring node_name = node.name.empty()
                            ? nullptr
                            : scene->node_hierarchy.string_buffer.append_use_f(
                                  "%s", node.name.c_str());

    gltf_to_hierarchy_node[root_scene.nodeIndices[i]] =
        scene->node_hierarchy.add_node(node_name, root_node_parent,
                                       node.meshIndex.has_value());
  }

  while (node_queue.size) {
    u32 gltf_node_index = UINT32_MAX;
    node_queue.dequeue(&gltf_node_index);

    fastgltf::Node &node = asset->nodes[gltf_node_index];

    cstring node_name = node.name.empty()
                            ? nullptr
                            : scene->node_hierarchy.string_buffer.append_use_f(
                                  "%s", node.name.c_str());
    // Get the node_hierarchy_index
    u32 node_hierarchy_index = gltf_to_hierarchy_node[gltf_node_index];

    // Transform
    if (std::holds_alternative<fastgltf::TRS>(node.transform)) {
      const fastgltf::TRS &trs = std::get<fastgltf::TRS>(node.transform);

      Transform &transform =
          scene->node_hierarchy.local_transforms[node_hierarchy_index];
      gltf_set_vec3(transform.position, trs.translation);
      gltf_set_quat(transform.rotation, trs.rotation);
      gltf_set_vec3(transform.scale, trs.scale);
    } else if (std::holds_alternative<fastgltf::math::fmat4x4>(
                   node.transform)) {
      const fastgltf::math::fmat4x4 &mat =
          std::get<fastgltf::math::fmat4x4>(node.transform);

      Transform &transform =
          scene->node_hierarchy.local_transforms[node_hierarchy_index];
      glm::mat4 matrix = gltf_get_matrix4x4(mat);
      transform.set_transform(matrix);
    }

    // Add children to the queue
    for (u32 i = 0; i < node.children.size(); ++i) {
      node_parents[node.children[i]] = node_hierarchy_index;
      node_queue.enqueue(node.children[i]);
      fastgltf::Node &child_node = asset->nodes[node.children[i]];
      cstring child_node_name =
          child_node.name.empty()
              ? nullptr
              : scene->node_hierarchy.string_buffer.append_use_f(
                    "%s", child_node.name.c_str());
      gltf_to_hierarchy_node[node.children[i]] =
          scene->node_hierarchy.add_node(child_node_name, node_hierarchy_index,
                                         child_node.meshIndex.has_value());
    }

    if (node.meshIndex.has_value()) {
      size_t mesids = node.meshIndex.value();
      fastgltf::Mesh &gltf_mesh = asset->meshes[node.meshIndex.value()];
      Mesh &mesh = scene->meshes.push_use();
      mesh.draws.init(allocator, gltf_mesh.primitives.size(),
                      gltf_mesh.primitives.size());

      // Single Vertex buffer for each mesh
      Array<Vertex> vertices{};
      vertices.init(allocator, 4);

      for (u32 i = 0; i < gltf_mesh.primitives.size(); ++i) {
        fastgltf::Primitive &primitive = gltf_mesh.primitives[i];
        HASSERT_MSG(primitive.type == fastgltf::PrimitiveType::Triangles,
                    "Non-Triangle Primitive type");

        scene->node_hierarchy.add_node(
            scene->node_hierarchy.string_buffer.append_use_f(
                "%s_MeshPrimitive_%d", node_name, i),
            node_hierarchy_index);

        size_t initial_vtx = vertices.size;
        u32 material_index =
            primitive.materialIndex.value() + previous_material_size;

        // load indexes
        // Index buffer per primitive
        Array<u32> indices{};
        {
          fastgltf::Accessor &index_accessor =
              asset->accessors[primitive.indicesAccessor.value()];
          indices.init(allocator, index_accessor.count);

          fastgltf::iterateAccessor<u32>(
              asset.get(), index_accessor,
              [&](std::uint32_t idx) { indices.push(idx + initial_vtx); });
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
                vertex.pos.x = v.data()[0];
                vertex.pos.y = v.data()[1];
                vertex.pos.z = v.data()[2];
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
        // load tangent vertices
        {
          fastgltf::Attribute *tangent_attribute =
              primitive.findAttribute("TANGENT");
          if (tangent_attribute != primitive.attributes.end()) {
            fastgltf::Accessor &tangent_accessor =
                asset->accessors[tangent_attribute->accessorIndex];

            fastgltf::iterateAccessorWithIndex<fastgltf::math::fvec4>(
                asset.get(), tangent_accessor,
                [&](fastgltf::math::fvec4 t, size_t index) {
                  Vertex &vertex = vertices[initial_vtx + index];
                  vertex.tangent.x = t.data()[0];
                  vertex.tangent.y = t.data()[1];
                  vertex.tangent.z = t.data()[2];
                  vertex.tangent.w = t.data()[3];
                });
          } else {
            Array<glm::vec4> tangents_data{};
            tangents_data.init(allocator, vertices.size, vertices.size);
            // TODO: Generate tangesnts
            SMikkTSpaceInterface interface = {};
            interface.m_getNumFaces = GetNumFaces;
            interface.m_getNumVerticesOfFace = GetNumVerticesOfFace;
            interface.m_getPosition = GetPosition;
            interface.m_getNormal = GetNormal;
            interface.m_getTexCoord = GetTexCoord;
            interface.m_setTSpaceBasic = SetTSpaceBasic;

            SMikkTSpaceContextUserData user_data{vertices.data,
                                                 indices, tangents_data};

            SMikkTSpaceContext mikkContext = {};
            mikkContext.m_pInterface = &interface;
            mikkContext.m_pUserData = &user_data;

            genTangSpaceDefault(&mikkContext);

            for (u32 i = 0; i < indices.size; ++i) {
              u32 index = indices[i];
              Vertex &vertex = vertices[index];
              vertex.tangent = tangents_data[index];
            }

            tangents_data.shutdown();

            HERROR("Primitive does not have tangents");
          }
        }

        BufferCreation creation{};
        creation.usage_flags =
            (BufferUsage::Enum)(BufferUsage::Index | BufferUsage::TransferDest);
        creation.memory_state_flags = MemoryState::Static;
        creation.memory_access_flags = MemoryAccess::GPU_ONLY;
        // TODO: Verify that indices uses u32 or u16
        creation.size = sizeof(u32) * indices.size;
        creation.initial_data = indices.data;
        creation.name = renderer_frontend->string_buffer.append_use_f(
            "%s_IndexBuffer", node_name);

        BufferHandle index_buffer_handle =
            renderer_frontend->create_buffer(creation);

        // TODO: Transparency
        MeshDraw &mesh_draw = mesh.draws[i];
        mesh_draw.primitive_count = indices.size;
        mesh_draw.index_buffer = index_buffer_handle;
        mesh_draw.material_index = material_index;
        indices.shutdown();
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

      vertices.shutdown();
    }
  }

  u32 num_new_root_nodes =
      scene->node_hierarchy.root_node_indices.size - old_root_node_size;
  for (u32 i = 0; i < num_new_root_nodes; ++i) {
    scene->node_hierarchy.update(
        scene->node_hierarchy.root_node_indices[i + old_root_node_size]);
  }

  stack_allocator->free_marker(stack_marker);

  FileService::change_directory(dir.path);
  return true;
}

} // namespace Helix
