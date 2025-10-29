#include "Renderer/RendererFrontEnd.hpp"
#include "Containers/ResourcePool.hpp"
#include "Core/Log.hpp"
#include "Core/Memory.hpp"
#include "Core/Profiler.hpp"
#include "Platform/Platform.hpp"
#include "Renderer/GPUResourceTypes.hpp"
#include "Renderer/GPUResources.hpp"
#include "Renderer/ImguiFrontend.hpp"
#include "RendererBackend.hpp"
#include "RendererTypes.hpp"
// Vendor
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
  device = create_device(config->backend_type);

  if (!device) {
    HCRITICAL("Failed to create backend!");
    return;
  }

  // TODO: Make this configurable
  if (!device->create_backbuffers(1, 1, 3)) {
    HCRITICAL("Failed to create backbuffer!");
    return;
  }

  // TODO: Make this configurable
  for (u32 i = 0; i < 3; ++i) {
    backbuffers[i] = device->get_backbuffer_texture(i);
  }

  current_frame_in_flight = 0;
  for (u32 i = 0; i < max_frames_in_flight; ++i) {
    frame_receipts[i] = device->create_receipt();
  }

  graphics_context = device->create_context(ContextType::Graphics);
  transfer_context = device->create_context(ContextType::Transfer);

  {
    RenderPassCreation creation{};
    creation.add_color_attachment(LoadOp::Clear, StoreOp::Store,
                                  backbuffers[0]);
    main_pass = create_render_pass(creation);
  }
  {
    BufferCreation creation{};
    creation.mapped = true;
    creation.size = MAX_ALLOCATION_SIZE;
    creation.usage_flags =
        BufferUsage::Enum(BufferUsage::TransferSrc | BufferUsage::TransferDst);
    creation.memory_access_flags = MemoryAccess::CPU_TO_GPU;
    creation.name = "StagingBuffer";
    staging_buffer = create_buffer(creation);
    staging_buffer_current_size = 0;
  }
  {
    BindingSetLayoutCreation creation;
    creation.is_bindless = true;
    creation.name = "BindlessDescriptorSetLayout";
    creation.add_binding(0, MAX_TEXTURES,
                         (ShaderStage::Enum)(ShaderStage::Fragment),
                         BindingType::CombinedSampler);
    bindless_set_layout = create_binding_set_layout(creation);
  }
  {
    BindingSetCreation creation;
    creation.name = "BindlessDescriptorSet";
    creation.layout = bindless_set_layout;
    bindless_set = create_binding_set(creation);
  }
  {
    SamplerCreation creation{};
    creation.name = "DefaultSampler";
    default_sampler = create_sampler(creation);
  }

  HeapAllocator *allocator = &MemoryService::instance()->system_allocator;
  bindless_textures_to_update.init(allocator, 10);

  HELIX_SERVICE_INIT_MSG(RendererFrontEnd);
  s_renderer_frontend = this;
}

void RendererFrontEnd::shutdown() {
  bindless_textures_to_update.shutdown();
  destroy_sampler(default_sampler);
  destroy_binding_set(bindless_set);
  destroy_binding_set_layout(bindless_set_layout);
  destroy_render_pass(main_pass);
  destroy_buffer(staging_buffer);

  for (u32 i = 0; i < max_frames_in_flight; ++i) {
    device->destroy_receipt(frame_receipts[i]);
  }

  device->destroy_context(graphics_context);
  device->destroy_context(transfer_context);

  destroy_device(device);

  HELIX_SERVICE_SHUTDOWN_MSG(RendererFrontEnd);
}

void RendererFrontEnd::on_resize(u16 width, u16 height) {
  device->resize_backbuffers();
}

bool RendererFrontEnd::render_frame(RenderPacket *packet) {
  HELIX_PROFILER_FUNCTION_COLOR(tracy::Color::Orange);

  if (begin_frame(packet)) {
    BarrierDescription barrier{};
    barrier.resource_type = ResourceType::Texture;
    barrier.src_state = ResourceState::Present;
    barrier.dst_state = ResourceState::RenderTarget;
    barrier.resource_handle = backbuffers[backbuffer_index];
    graphics_context->resource_barrier(&barrier);

    device->set_render_pass_texture(main_pass, backbuffers[backbuffer_index],
                                    false, 0);

    graphics_context->bind_renderpass(main_pass);
    // graphics_context->bind_pipeline(hello_triangle);
    i32 width, height;
    Platform::instance()->get_window_size(&width, &height);

    graphics_context->set_scissor(0.f, 0.f, (f32)width, (f32)height);
    graphics_context->set_viewport(0.f, 0.f, (f32)width, (f32)height, 0.f, 1.f);
    graphics_context->draw(3, 1, 0, 0);
    graphics_context->end_current_pass();

    barrier.resource_type = ResourceType::Texture;
    barrier.src_state = ResourceState::RenderTarget;
    barrier.dst_state = ResourceState::Present;
    barrier.resource_handle = backbuffers[backbuffer_index];

    graphics_context->resource_barrier(&barrier);

    end_frame(packet);
  }

  return true;
}

bool RendererFrontEnd::begin_frame(RenderPacket *packet) {
  HELIX_PROFILER_FUNCTION();
  device->wait_on_work(frame_receipts[current_frame_in_flight]);
  backbuffer_index =
      device->get_next_image_index(graphics_context, current_frame_in_flight);

  if (backbuffer_index == -1)
    return false;

  graphics_context->begin(current_frame_in_flight);

  return true;
}

bool RendererFrontEnd::end_frame(RenderPacket *packet) {
  HELIX_PROFILER_FUNCTION();
  graphics_context->end(current_frame_in_flight);

  if (bindless_textures_to_update.size) {
    u32 update_count =
        (bindless_textures_to_update.size < MAX_BINDLESS_UPDATE_PER_FRAME)
            ? bindless_textures_to_update.size
            : MAX_BINDLESS_UPDATE_PER_FRAME;

    BindingSetUpdateInfo infos[MAX_BINDLESS_UPDATE_PER_FRAME];
    u32 current_info = 0;
    for (i32 it = bindless_textures_to_update.size - 1; it >= 0; it--) {
      BindingSetUpdateInfo &info = infos[current_info];
      TextureHandle texture = bindless_textures_to_update[it];
      bindless_textures_to_update.pop();

      info.resource_handle = texture;
      info.resource_type = ResourceType::Texture;
      info.binding = 0;
      info.resource_index = texture.index;
    }

    update_binding_set(bindless_set, infos, update_count);
  }

  device->submit_work(graphics_context,
                      frame_receipts[current_frame_in_flight]);
  device->present_to_display();
  current_frame_in_flight =
      (current_frame_in_flight + 1) % max_frames_in_flight;

  return true;
}

BufferHandle RendererFrontEnd::create_buffer(const BufferCreation &creation) {
  return device->create_buffer(creation);
}

TextureHandle
RendererFrontEnd::create_texture(const TextureCreation &creation) {
  TextureHandle handle = device->create_texture(creation);
  if (creation.usage & TextureUsage::Sampled)
    bindless_textures_to_update.push(handle);

  return handle;
}

SamplerHandle
RendererFrontEnd::create_sampler(const SamplerCreation &creation) {
  return device->create_sampler(creation);
}

BindingSetHandle
RendererFrontEnd::create_binding_set(const BindingSetCreation &creation) {
  return device->create_binding_set(creation);
}

BindingSetLayoutHandle RendererFrontEnd::create_binding_set_layout(
    const BindingSetLayoutCreation &creation) {
  return device->create_binding_set_layout(creation);
}

PipelineHandle
RendererFrontEnd::create_pipeline(const PipelineCreation &creation) {
  return device->create_pipeline(creation);
}

RenderPassHandle
RendererFrontEnd::create_render_pass(const RenderPassCreation &creation) {
  return device->create_render_pass(creation);
}

void RendererFrontEnd::destroy_buffer(BufferHandle handle) {
  device->destroy_buffer(handle);
}

void RendererFrontEnd::destroy_texture(TextureHandle handle) {
  device->destroy_texture(handle);
}

void RendererFrontEnd::destroy_sampler(SamplerHandle handle) {
  device->destroy_sampler(handle);
}

void RendererFrontEnd::destroy_binding_set(BindingSetHandle handle) {
  device->destroy_binding_set(handle);
}

void RendererFrontEnd::destroy_binding_set_layout(
    BindingSetLayoutHandle handle) {
  device->destroy_binding_set_layout(handle);
}

void RendererFrontEnd::destroy_pipeline(PipelineHandle handle) {
  device->destroy_pipeline(handle);
}

void RendererFrontEnd::destroy_render_pass(RenderPassHandle handle) {
  device->destroy_render_pass(handle);
}

bool RendererFrontEnd::update_binding_set(BindingSetHandle set,
                                          BindingSetUpdateInfo *update_infos,
                                          u32 update_count) {
  return device->update_binding_set(set, update_infos, update_count);
}

void RendererFrontEnd::set_pipeline_binding_set(PipelineHandle pipeline,
                                                BindingSetHandle set,
                                                u32 set_index) {}

void *RendererFrontEnd::get_buffer_map(BufferHandle handle) {
  return device->get_buffer_map(handle);
}

void RendererFrontEnd::copy_data_to_image(void *data, TextureHandle texture,
                                          u64 texture_size) {
// TODO: Hard coded
#define MAX_ALLOC_SIZE 4292870144
  memcpy(get_buffer_map(staging_buffer), data, texture_size);

  device->wait_on_work(frame_receipts[current_frame_in_flight]);
  graphics_context->begin(current_frame_in_flight);

  graphics_context->copy_buffer_to_texture(texture, staging_buffer,
                                           texture_size);

  graphics_context->end(0);

  device->submit_work(graphics_context, nullptr);

  // TODO: Blocking
  graphics_context->wait_on_queue();
}

void RendererFrontEnd::copy_data_to_buffer(void *data,
                                           TextureHandle dst_image) {}

void RendererFrontEnd::copy_buffer_to_buffer(BufferHandle src_buffer,
                                             BufferHandle dst_buffer) {}

void RendererFrontEnd::print_gpu_stats() {}

} // namespace Helix
