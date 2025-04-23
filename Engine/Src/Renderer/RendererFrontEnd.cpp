#include "Renderer/RendererFrontEnd.hpp"
#include "Containers/ResourcePool.hpp"
#include "Core/Clock.hpp"
#include "Core/Job.hpp"
#include "Core/Log.hpp"
#include "Core/Memory.hpp"
#include "Game.hpp"
#include "Platform/File.hpp"
#include "Platform/Platform.hpp"
#include "Renderer/GPUResourceTypes.hpp"
#include "Renderer/GPUResources.hpp"
#include "Renderer/ImguiFrontend.hpp"
#include "RendererBackend.hpp"
#include "RendererTypes.hpp"

#include <cstring>
#include <glm/gtx/hash.hpp>
#include <stb_image.h>
#include <tiny_obj_loader.h>

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
  char *file_name;
  FileReadResult read_result;
  u32 pbr_material_index;
  RendererFrontEnd *renderer_frontend;
};

struct TextureLoadSuccess {
  TextureCreation tex_creation{};
  void *file_data{nullptr};
  void *texture_data{nullptr};
  u32 pbr_material_index;
  RendererFrontEnd *renderer_frontend;
};

bool load_texture_data(void *entry_data, void *result_data) {
  TextureLoadRequest *request = (TextureLoadRequest *)entry_data;
  TextureLoadSuccess *res = (TextureLoadSuccess *)result_data;
  res->renderer_frontend = request->renderer_frontend;

  Platform::instance()->sleep(3000);

  if (!FileService::instance()->open_read_file_binary(
          request->file_name, &request->read_result,
          &MemoryService::instance()->system_allocator)) {
    HERROR("Failed to open file: {}", request->file_name);
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
  MemoryService::instance()->system_allocator.deallocate(request->file_name);
  if (!texture_data) {
    HERROR("Unable to load texture data: {}", request->file_name);
    return false;
  }

  res->tex_creation.initial_data = texture_data;
  res->tex_creation.width = width;
  res->tex_creation.height = height;
  res->pbr_material_index = request->pbr_material_index;

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

  TextureHandle handle =
      res->renderer_frontend->create_texture(res->tex_creation);
  res->renderer_frontend->model_textures.push(handle);
  res->renderer_frontend->pbr_materials[res->pbr_material_index]
      .albedo_texture_handle = handle;

  free(res->tex_creation.initial_data);
  return true;
}

static RendererFrontEnd *s_renderer_frontend{nullptr};
RendererFrontEnd *RendererFrontEnd::instance() { return s_renderer_frontend; }

void RendererFrontEnd::init(void *_config) {
  if (s_renderer_frontend) {
    HELIX_SERVICE_RECREATE_MSG(RendererFrontEnd);
    return;
  }

  RendererConfig *config = (RendererConfig *)_config;
  backend = RendererBackendCreate(config->backend_type);

  if (!backend) {
    HCRITICAL("Unable to get backend!");
    return;
  }

  config->max_frames_in_flight = max_frames_in_flight;

  // TODO: Use the vsync configuration
  if (!backend->init(config)) {
    HCRITICAL("Failed to create backend!");
    return;
  }

  HELIX_SERVICE_INIT_MSG(RendererFrontEnd);
  s_renderer_frontend = this;
  current_frame = 0;

  HeapAllocator *allocator = &MemoryService::instance()->system_allocator;
  StackAllocator *stack_allocator = &MemoryService::instance()->stack_allocator;
  size_t stack_marker = stack_allocator->get_marker();

  buffers.init(allocator, 10);
  pipelines.init(allocator, 10);
  textures.init(allocator, 10);
  model_textures.init(allocator, 10);
  string_buffer.init(allocator, hkilo(10));
  pbr_materials.init(allocator, 25);

  // Create Uniform Buffers
  {
    BufferCreation creation{};
    creation.reset();
    creation.usage_flags = BufferUsage::Uniform;
    creation.memory_state_flags = MemoryState::Persistent;
    creation.memory_access_flags = MemoryAccess::CPU_TO_GPU;
    creation.size = sizeof(UniformBufferObject);
    creation.initial_data = nullptr;
    for (u32 i = 0; i < max_frames_in_flight; ++i) {
      creation.name = string_buffer.append_use_f("uniform_buffer_%d", i);
      uniform_buffers[i] = create_buffer(creation);
    }
  }

  {
    PipelineCreation creation;
    creation.name = "test";
    creation.shader_create_infos = (ShaderCreateInfo *)halloca(
        sizeof(ShaderCreateInfo) * 2, stack_allocator);
    creation.shader_create_infos[0] = {"shader.vert", ShaderStage::Vertex};
    creation.shader_create_infos[1] = {"shader.frag", ShaderStage::Fragment};
    creation.shader_count = 2;
    creation.pipeline_type = PipelineType::Graphics;
    creation.cull_mode = CullMode::Back;

    Clock clock{};
    clock.start();
    f64 start_time = clock.get_elapsed_time_ms();
    pipeline = create_pipeline(creation);

    f64 delta_time = clock.get_elapsed_time_ms() - start_time;
    HTRACE("Pipeline create time: {:.3f} ms", delta_time);
  }

  ShaderUniform *shader_uniforms = (ShaderUniform *)halloca(
      sizeof(ShaderUniform) * max_frames_in_flight, stack_allocator);
  for (u32 i = 0; i < max_frames_in_flight; ++i) {
    shader_uniforms[i].binding = 0;
    BufferResource *buffer = buffers.obtain(uniform_buffers[i]);
    shader_uniforms[i].internal_resource_handle = buffer->internal_handle;
    shader_uniforms[i].resource_type = ResourceType::Buffer;
    shader_uniforms[i].buffer_info.offset = 0;
    shader_uniforms[i].buffer_info.range = sizeof(UniformBufferObject);

    ShaderUniformSet set{};
    set.uniform_count = 1;
    set.uniforms = &shader_uniforms[i];
    set.set_index = 1;
    update_shader_uniform_set(set, pipeline);
  }

  TextureCreation tex_creation{};
  // Magenta
  u8 def_colour[4] = {255, 0, 255, 255};

  FileReadResult read_result{};
  if (FileService::instance()->open_read_file_binary(
          ASSETS_PATH "/Textures/default.jpg", &read_result, allocator)) {
    i32 tex_width, tex_height, tex_channels;
    u8 *texture_data = stbi_load_from_memory((const stbi_uc *)read_result.data,
                                             read_result.size, &tex_width,
                                             &tex_height, &tex_channels, 4);
    allocator->deallocate(read_result.data);
    if (!texture_data) {
      tex_creation.initial_data = def_colour;
      tex_creation.width = 1;
      tex_creation.height = 1;
    } else {
      tex_creation.initial_data = texture_data;
      tex_creation.width = tex_width;
      tex_creation.height = tex_height;
    }
  } else {
    tex_creation.initial_data = def_colour;
    tex_creation.width = 1;
    tex_creation.height = 1;
  }

  tex_creation.name = "default_texture";
  tex_creation.depth = 1;
  tex_creation.array_layer_count = 1;
  tex_creation.array_base_level = 0;
  tex_creation.mip_level_count = 1;
  tex_creation.mip_base_level = 0;
  tex_creation.usage =
      TextureUsage::Enum(TextureUsage::TransferDest | TextureUsage::Sampled);
  tex_creation.format = TextureFormat::R8G8B8A8_SRGB;
  tex_creation.type = TextureType::Texture2D;
  default_texture = create_texture(tex_creation);

  meshes.init(allocator, 10);
  index_buffers.init(allocator, 10);

  print_gpu_stats();

  stack_allocator->free_marker(stack_marker);
}

void RendererFrontEnd::shutdown() {
  destroy_pipeline(pipeline);
  destroy_texture(default_texture);

  for (u32 i = 0; i < max_frames_in_flight; ++i) {
    destroy_buffer(uniform_buffers[i]);
  }
  destroy_model();

  meshes.shutdown();
  pbr_materials.shutdown();
  textures.shutdown();
  backend->shutdown();
  buffers.shutdown();
  pipelines.shutdown();
  hfree(backend, &MemoryService::instance()->system_allocator);

  string_buffer.shutdown();
  HELIX_SERVICE_SHUTDOWN_MSG(RendererFrontEnd);
}

void RendererFrontEnd::on_resize(u16 width, u16 height) {
  backend->on_resize(width, height);
}

bool RendererFrontEnd::render_frame(RenderPacket *packet) {
  packet->meshes = meshes.data;
  packet->mesh_count = meshes.size;

  if (begin_frame(packet)) {
    backend->render_frame(packet);

    ImguiFrontend::instance()->begin_frame();

    packet->game->render_frame(packet->delta_time);

    ImguiFrontend::instance()->render_frame(packet);

    bool result = end_frame(packet);
    if (!result) {
      HCRITICAL("End frame failed!");
      return false;
    }
  }

  return true;
}

bool RendererFrontEnd::begin_frame(RenderPacket *packet) {
  packet->current_frame = current_frame;
  BufferResource *uniform_buffer =
      buffers.obtain(uniform_buffers[current_frame]);
  packet->scene_data_buffer = uniform_buffer->internal_handle;
  return backend->begin_frame(packet);
}

bool RendererFrontEnd::end_frame(RenderPacket *packet) {
  current_frame = (current_frame + 1) % max_frames_in_flight;
  return backend->end_frame(packet);
}

bool RendererFrontEnd::load_model(cstring path, cstring model) {
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
  HDEBUG("# of materials = {}", materials.size());
  HDEBUG("# of shapes = {}", shapes.size());
  HDEBUG("# of indices in shape[0]: {}", shapes[0].mesh.indices.size());
  HDEBUG("# of materials in shape[0]: {}", shapes[0].mesh.material_ids.size());
  HDEBUG("# of faces in shape[0]: {}", shapes[0].mesh.num_face_vertices.size());

  HeapAllocator *allocator = &MemoryService::instance()->system_allocator;
  StackAllocator *stack_allocator = &MemoryService::instance()->stack_allocator;
  std::unordered_map<Vertex, uint32_t> unique_vertices{};
  Mesh &mesh = meshes.push_use();
  mesh.draws.init(allocator, shapes.size(), shapes.size());

  Array<Vertex> vertices{};
  vertices.init(stack_allocator, attrib.vertices.size() / 3);

  Array<u32> indices{};
  indices.init(stack_allocator, vertices.capacity / 2);

  FileService *file_service = FileService::instance();
  Directory dir{};
  file_service->current_directory(&dir);

  file_service->change_directory(path);

  for (u32 i = 0; i < materials.size(); ++i) {
    tinyobj::material_t &material = materials[i];
    int width, height, channels;
    PBRMaterial &pbr_material = pbr_materials.push_use();
    // TODO: Make a function for getting a file's full path
    // +2 for the backslash and null terminator
    if (!material.diffuse_texname.empty()) {

      pbr_material.albedo_texture_handle = default_texture;
      u32 file_full_path_size =
          strlen(path) + material.diffuse_texname.length() + 1;
      char *file_full_path = (char *)halloca(file_full_path_size, allocator);
      memset(file_full_path, 0, file_full_path_size);
      strncat(file_full_path, path, strlen(path));
      strncat(file_full_path, material.diffuse_texname.c_str(),
              material.diffuse_texname.length());

      TextureLoadRequest load_request{};
      load_request.file_name = file_full_path;
      load_request.pbr_material_index = pbr_materials.size - 1;
      load_request.renderer_frontend = this;

      JobInfo info = create_job_info(
          load_texture_data, load_texture_success, nullptr, &load_request,
          sizeof(TextureLoadRequest), sizeof(TextureLoadSuccess),
          JobType::ResourceLoad, JobPriority::Medium);
      TextureLoadSuccess *res_data = (TextureLoadSuccess *)info.result_data;
      res_data->tex_creation.name = material.diffuse_texname.c_str();
      JobService::instance()->submit(info);

    } else {
      pbr_material.albedo_texture_handle = default_texture;
    }
  }
  // Default material
  PBRMaterial &pbr_material = pbr_materials.push_use();
  pbr_material.albedo_texture_handle = default_texture;

  file_service->change_directory(dir.path);

  BufferCreation creation{};
  for (u32 i = 0; i < shapes.size(); ++i) {
    const auto &shape = shapes[i];
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

    creation.reset();
    creation.usage_flags =
        (BufferUsage::Enum)(BufferUsage::Index | BufferUsage::TransferDest);
    creation.memory_state_flags = MemoryState::Static;
    creation.memory_access_flags = MemoryAccess::GPU_ONLY;
    creation.size = sizeof(u32) * indices.size;
    creation.initial_data = indices.data;
    creation.name =
        string_buffer.append_use_f("Index_buffer_%d", index_buffers.size);

    index_buffers.push(create_buffer(creation));
    mesh.draws[i].primitive_count = shape.mesh.indices.size();
    BufferResource *index_buffer =
        buffers.obtain(index_buffers[index_buffers.size - 1]);
    mesh.draws[i].internal_index_buffer = index_buffer->internal_handle;
    mesh.draws[i].material_index = (shape.mesh.material_ids[0] == -1)
                                       ? pbr_materials.size - 1
                                       : shape.mesh.material_ids[0];

    indices.clear();
  }

  creation.reset();
  creation.usage_flags =
      (BufferUsage::Enum)(BufferUsage::Vertex | BufferUsage::TransferDest);
  creation.memory_state_flags = MemoryState::Static;
  creation.memory_access_flags = MemoryAccess::GPU_ONLY;
  creation.size = sizeof(Vertex) * vertices.size;
  creation.initial_data = vertices.data;
  creation.name = "Model_Vertex_Buffer";

  vertex_buffer = create_buffer(creation);
  BufferResource *buffer_resource = buffers.obtain(vertex_buffer);

  mesh.internal_vertex_buffer = buffer_resource->internal_handle;

  // HDEBUG("Vertex size = {}", vertices.size);

  return true;
}

bool RendererFrontEnd::destroy_model() {
  for (u32 i = 0; i < index_buffers.size; ++i) {
    destroy_buffer(index_buffers[i]);
  }
  for (u32 i = 0; i < meshes.size; ++i) {
    Mesh &mesh = meshes[i];
    mesh.draws.shutdown();
  }
  for (u32 i = 0; i < model_textures.size; ++i) {
    destroy_texture(model_textures[i]);
  }

  destroy_buffer(vertex_buffer);
  model_textures.shutdown();
  index_buffers.shutdown();
  meshes.shutdown();
  return true;
}

BufferHandle RendererFrontEnd::create_buffer(BufferCreation &creation) {
  BufferHandle handle = buffers.obtain_new();
  if (handle.index == k_invalid_index) {
    HERROR("Failed to obtain new buffer resource");
    return handle;
  }

  BufferHandle internal_handle = backend->create_buffer(creation);
  if (internal_handle.index == k_invalid_index) {
    buffers.release(handle);
    handle.index = k_invalid_index;
    return handle;
  }

  BufferResource *buffer = buffers.obtain(handle);
  buffer->handle = handle;
  buffer->internal_handle = internal_handle;

  return handle;
}

PipelineHandle RendererFrontEnd::create_pipeline(PipelineCreation &creation) {
  PipelineHandle handle = pipelines.obtain_new();
  if (handle.index == k_invalid_index) {
    HERROR("Failed to obntain new PipelineResource");
    return handle;
  }

  PipelineHandle internal_handle = backend->create_pipeline(creation);
  if (internal_handle.index == k_invalid_index) {
    pipelines.release(handle);
    handle.index = k_invalid_index;
    return handle;
  }

  PipelineResource *pipeline = pipelines.obtain(handle);
  pipeline->handle = handle;
  pipeline->internal_handle = internal_handle;

  return handle;
}

TextureHandle RendererFrontEnd::create_texture(TextureCreation &creation) {
  TextureHandle handle = textures.obtain_new();
  if (handle.index == k_invalid_index) {
    HERROR("Failed to obtain new TextureResource");
    return handle;
  }

  TextureHandle internal_handle = backend->create_texture(creation);
  if (internal_handle.index == k_invalid_index) {
    textures.release(handle);
    handle.index = k_invalid_index;
    return handle;
  }

  TextureResource *texture = textures.obtain(handle);
  texture->handle = handle;
  texture->internal_handle = internal_handle;

  return handle;
}

void RendererFrontEnd::destroy_buffer(BufferHandle handle) {
  if (handle.index == k_invalid_index) {
    HERROR("Attempting to destroy an invalid buffer");
    return;
  }
  BufferResource *buffer = buffers.obtain(handle);
  backend->destroy_buffer(buffer->internal_handle);

  buffers.release(handle);
}

void RendererFrontEnd::destroy_pipeline(PipelineHandle handle) {

  if (handle.index == k_invalid_index) {
    HERROR("Attempting to destroy an invalid pipeline");
    return;
  }
  PipelineResource *pipeline = pipelines.obtain(handle);
  backend->destroy_pipeline(pipeline->internal_handle);

  pipelines.release(handle);
}

void RendererFrontEnd::destroy_texture(TextureHandle handle) {

  if (handle.index == k_invalid_index) {
    HERROR("Attempting to destroy an invalid texture");
    return;
  }
  TextureResource *texture = textures.obtain(handle);
  backend->destroy_texture(texture->internal_handle);

  textures.release(handle);
}

bool RendererFrontEnd::update_shader_uniform_set(ShaderUniformSet &set,
                                                 PipelineHandle pipeline) {
  return backend->update_shader_uniform_set(set, pipeline);
}

void RendererFrontEnd::print_gpu_stats() { backend->print_gpu_stats(); }

} // namespace Helix
