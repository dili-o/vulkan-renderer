#include "Renderer/RendererFrontEnd.hpp"
#include "Containers/ResourcePool.hpp"
#include "Core/Log.hpp"
#include "Core/Memory.hpp"
#include "Core/Profiler.hpp"
#include "Game.hpp"
#include "Platform/File.hpp"
#include "Renderer/GPUResourceTypes.hpp"
#include "Renderer/GPUResources.hpp"
#include "Renderer/ImguiFrontend.hpp"
#include "RendererBackend.hpp"
#include "RendererTypes.hpp"
// Vendor
#include <cstring>
#include <stb_image.h>

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

  string_buffer.init(allocator, hmega(6));

  BindingSetLayoutCreation layout_creation{};
  layout_creation.name = "Scene_Global_SetLayout";
  layout_creation.add_binding(0, 1, ShaderStage::Vertex,
                              BindingType::UniformBuffer);
  scene_set_layout = create_binding_set_layout(layout_creation);

  layout_creation.reset();
  layout_creation.name = "Bindless_SetLayout";
  layout_creation.is_bindless = true;
  layout_creation.add_binding(0, 1000, ShaderStage::AllStage,
                              BindingType::CombinedSampler);
  bindless_set_layout = create_binding_set_layout(layout_creation);
  BindingSetCreation set_creation{};
  set_creation.name = "Bindless_Set";
  set_creation.layout = bindless_set_layout;
  bindless_set = create_binding_set(set_creation);
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
      set_creation.reset();
      set_creation.layout = scene_set_layout;
      set_creation.name = string_buffer.append_use_f("Scene_Global_Set_%d", i);
      scene_sets[i] = create_binding_set(set_creation);

      BindingSetUpdateInfo update_info;
      update_info.resource_handle = uniform_buffers[i];
      update_info.resource_type = ResourceType::Buffer;
      update_info.binding = 0;
      update_info.resource_index = 0;
      update_info.buffer_info.offset = 0;
      update_info.buffer_info.range = sizeof(UniformBufferObject);
      update_binding_set(scene_sets[i], &update_info, 1);
    }
  }

  {
    PipelineCreation creation;
    creation.name = "pbr_pipeline";
    creation.shader_create_infos = (ShaderCreateInfo *)halloca(
        sizeof(ShaderCreateInfo) * 2, stack_allocator);
    creation.shader_create_infos[0] = {"shader.vert", ShaderStage::Vertex};
    creation.shader_create_infos[1] = {"shader.frag", ShaderStage::Fragment};
    creation.shader_count = 2;
    creation.pipeline_type = PipelineType::Graphics;
    creation.cull_mode = CullMode::None;
    creation.set_layouts[0] = bindless_set_layout;
    creation.set_layouts[1] = scene_set_layout;
    creation.set_layout_count = 2;

    pbr_pipeline = create_pipeline(creation);

    set_pipeline_binding_set(pbr_pipeline, bindless_set, 0);
    set_pipeline_binding_set(pbr_pipeline, scene_sets[0], 1);

    creation.name = "depth_prepass_pipeline";
    creation.shader_create_infos =
        (ShaderCreateInfo *)halloca(sizeof(ShaderCreateInfo), stack_allocator);
    creation.shader_create_infos[0] = {"depth_prepass.vert",
                                       ShaderStage::Vertex};
    creation.shader_count = 1;
    creation.pipeline_type = PipelineType::Graphics;
    creation.cull_mode = CullMode::Back;
    creation.set_layouts[0] = scene_set_layout;
    creation.set_layout_count = 1;
    depth_prepass_pipeline = create_pipeline(creation);
    set_pipeline_binding_set(depth_prepass_pipeline, scene_sets[0], 0);
  }

  TextureCreation tex_creation{};
  // Magenta
  u8 def_colour[4] = {255, 0, 255, 255};

  FileReadResult read_result{};
  if (FileService::open_read_file_binary(ASSETS_PATH "/Textures/default.jpg",
                                         &read_result, allocator)) {
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

  tex_creation.name = "default_albedo_texture";
  tex_creation.depth = 1;
  tex_creation.array_layer_count = 1;
  tex_creation.array_base_level = 0;
  tex_creation.mip_level_count = 1;
  tex_creation.mip_base_level = 0;
  tex_creation.usage =
      TextureUsage::Enum(TextureUsage::TransferDest | TextureUsage::Sampled);
  tex_creation.format = TextureFormat::R8G8B8A8_SRGB;
  tex_creation.type = TextureType::Texture2D;
  default_albedo_texture = create_texture(tex_creation);

  u8 def_normal[4] = {128, 128, 255, 255};
  tex_creation.format = TextureFormat::R8G8B8A8_UNORM;
  tex_creation.initial_data = def_normal;
  tex_creation.width = 1;
  tex_creation.height = 1;
  default_normal_texture = create_texture(tex_creation);

  print_gpu_stats();

  stack_allocator->free_marker(stack_marker);
}

void RendererFrontEnd::shutdown() {
  destroy_pipeline(depth_prepass_pipeline);
  destroy_pipeline(pbr_pipeline);
  destroy_texture(default_albedo_texture);
  destroy_texture(default_normal_texture);

  for (u32 i = 0; i < max_frames_in_flight - 1; ++i) {
    destroy_binding_set(scene_sets[i + 1]);
  }

  for (u32 i = 0; i < max_frames_in_flight; ++i) {
    destroy_buffer(uniform_buffers[i]);
  }

  backend->shutdown();
  hfree(backend, &MemoryService::instance()->system_allocator);

  string_buffer.shutdown();
  HELIX_SERVICE_SHUTDOWN_MSG(RendererFrontEnd);
}

void RendererFrontEnd::on_resize(u16 width, u16 height) {
  backend->on_resize(width, height);
}

bool RendererFrontEnd::render_frame(RenderPacket *packet) {
  HELIX_PROFILER_FUNCTION_COLOR(tracy::Color::Orange);

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
  HELIX_PROFILER_FUNCTION();
  packet->current_frame = current_frame;
  packet->scene_data_buffer = uniform_buffers[current_frame];
  return backend->begin_frame(packet);
}

bool RendererFrontEnd::end_frame(RenderPacket *packet) {
  HELIX_PROFILER_FUNCTION();
  current_frame = (current_frame + 1) % max_frames_in_flight;
  return backend->end_frame(packet);
}

BufferHandle RendererFrontEnd::create_buffer(BufferCreation &creation) {
  return backend->create_buffer(creation);
}

PipelineHandle RendererFrontEnd::create_pipeline(PipelineCreation &creation) {
  return backend->create_pipeline(creation);
}

TextureHandle RendererFrontEnd::create_texture(TextureCreation &creation) {
  HELIX_PROFILER_FUNCTION();
  HELIX_PROFILER_ZONE_TEXT(creation.name, strlen(creation.name));
  return backend->create_texture(creation);
}

BindingSetLayoutHandle RendererFrontEnd::create_binding_set_layout(
    BindingSetLayoutCreation &creation) {
  return backend->create_binding_set_layout(creation);
}

BindingSetHandle
RendererFrontEnd::create_binding_set(BindingSetCreation &creation) {
  return backend->create_binding_set(creation);
}

// TODO: Implement
BufferInfo RendererFrontEnd::access_buffer_view(BufferHandle handle) {
  BufferInfo info{};
  return info;
}

// TODO: Implement
TextureInfo RendererFrontEnd::access_texture_view(TextureHandle handle) {
  TextureInfo info{};
  return info;
}

// TODO: Implement
PipelineInfo RendererFrontEnd::access_pipeline_view(PipelineHandle handle) {
  PipelineInfo info{};
  return info;
}

void RendererFrontEnd::destroy_buffer(BufferHandle handle) {
  if (handle.index == k_invalid_index) {
    HERROR("Attempting to destroy an invalid buffer");
    return;
  }
  backend->destroy_buffer(handle);
}

void RendererFrontEnd::destroy_pipeline(PipelineHandle handle) {
  if (handle.index == k_invalid_index) {
    HERROR("Attempting to destroy an invalid pipeline");
    return;
  }
  backend->destroy_pipeline(handle);
}

void RendererFrontEnd::destroy_texture(TextureHandle handle) {
  if (handle.index == k_invalid_index) {
    HERROR("Attempting to destroy an invalid texture");
    return;
  }
  backend->destroy_texture(handle);
}

void RendererFrontEnd::destroy_binding_set(BindingSetHandle handle) {
  if (handle.index == k_invalid_index) {
    HERROR("Attempting to destroy an invalid BindingSet");
    return;
  }
  backend->destroy_binding_set(handle);
}

bool RendererFrontEnd::update_binding_set(BindingSetHandle set,
                                          BindingSetUpdateInfo *update_infos,
                                          u32 update_count) {
  return backend->update_binding_set(set, update_infos, update_count);
}

void RendererFrontEnd::set_pipeline_binding_set(PipelineHandle pipeline,
                                                BindingSetHandle set,
                                                u32 set_index) {
  backend->set_pipeline_binding_set(pipeline, set, set_index);
}

void RendererFrontEnd::print_gpu_stats() { backend->print_gpu_stats(); }

} // namespace Helix
