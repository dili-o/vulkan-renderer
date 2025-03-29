#include "Renderer/RendererFrontEnd.hpp"
#include "Containers/ResourcePool.hpp"
#include "Core/Log.hpp"
#include "Core/Memory.hpp"
#include "Renderer/GPUResourceTypes.hpp"
#include "Renderer/GPUResources.hpp"
#include "RendererBackend.hpp"
#include "RendererTypes.hpp"

namespace Helix {

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

  string_buffer.init(allocator, hkilo(1));

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
    pipeline = create_pipeline(creation);
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

  // Magenta
  u8 def_colour[4] = {255, 0, 255, 255};
  TextureCreation tex_creation{};
  tex_creation.name = "default_texture";
  tex_creation.initial_data = def_colour;
  tex_creation.width = 1;
  tex_creation.height = 1;
  tex_creation.depth = 1;
  tex_creation.array_layer_count = 1;
  tex_creation.array_base_level = 0;
  tex_creation.mip_level_count = 1;
  tex_creation.mip_base_level = 0;
  tex_creation.usage = TextureUsage::TransferDest;
  tex_creation.format = TextureFormat::R8G8B8A8_SRGB;
  tex_creation.type = TextureType::Texture2D;
  default_texture = create_texture(tex_creation);

  TextureResource *default_tex = textures.obtain(default_texture);
  ShaderUniform texture_uniform{};
  texture_uniform.binding = 0;
  texture_uniform.internal_resource_handle = default_tex->internal_handle;
  texture_uniform.resource_type = ResourceType::Texture;
  texture_uniform.texture_info;

  ShaderUniformSet set{};
  set.uniform_count = 1;
  set.set_index = 0;
  set.uniforms = &texture_uniform;
  update_shader_uniform_set(set, pipeline);

  stack_allocator->free_marker(stack_marker);
}

void RendererFrontEnd::shutdown() {
  destroy_pipeline(pipeline);
  destroy_texture(default_texture);

  for (u32 i = 0; i < max_frames_in_flight; ++i) {
    destroy_buffer(uniform_buffers[i]);
  }

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

bool RendererFrontEnd::draw_frame(RenderPacket *packet) {

  if (begin_frame(packet)) {

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

bool RendererFrontEnd::load_model(cstring path) {
  // TODO: Create Vertex buffer
  // TODO: Create Index buffer
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

} // namespace Helix
