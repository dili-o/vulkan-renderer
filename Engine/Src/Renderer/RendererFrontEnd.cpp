#include "Renderer/RendererFrontEnd.hpp"
#include "Containers/HashMap.hpp"
#include "Containers/ResourcePool.hpp"
#include "Core/Log.hpp"
#include "Core/Memory.hpp"
#include "Core/Profiler.hpp"
#include "Core/String.hpp"
#include "Game.hpp"
#include "Platform/File.hpp"
#include "Platform/Platform.hpp"
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

  RenderPassCreation creation{};
  creation.add_color_attachment(LoadOp::Clear, StoreOp::Store, backbuffers[0]);
  main_pass = create_render_pass(creation);

  HELIX_SERVICE_INIT_MSG(RendererFrontEnd);
  s_renderer_frontend = this;
}

void RendererFrontEnd::shutdown() {
  destroy_render_pass(main_pass);

  for (u32 i = 0; i < max_frames_in_flight; ++i) {
    device->destroy_receipt(frame_receipts[i]);
  }

  device->destroy_context(graphics_context);
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

  device->submit_work(graphics_context,
                      frame_receipts[current_frame_in_flight]);
  device->present_to_display();
  current_frame_in_flight =
      (current_frame_in_flight + 1) % max_frames_in_flight;

  return true;
}

BufferHandle RendererFrontEnd::create_buffer(BufferCreation &creation) {
  return BufferHandle();
}

PipelineHandle RendererFrontEnd::create_pipeline(PipelineCreation &creation) {
  return PipelineHandle();
}

TextureHandle RendererFrontEnd::create_texture(TextureCreation &creation) {
  HELIX_PROFILER_FUNCTION();
  HELIX_PROFILER_ZONE_TEXT(creation.name, strlen(creation.name));
  return TextureHandle();
}

BindingSetLayoutHandle RendererFrontEnd::create_binding_set_layout(
    BindingSetLayoutCreation &creation) {
  return BindingSetLayoutHandle();
}

BindingSetHandle
RendererFrontEnd::create_binding_set(BindingSetCreation &creation) {
  return BindingSetHandle();
}

RenderPassHandle
RendererFrontEnd::create_render_pass(RenderPassCreation &creation) {
  return device->create_render_pass(creation);
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

void RendererFrontEnd::destroy_buffer(BufferHandle handle) {}

void RendererFrontEnd::destroy_pipeline(PipelineHandle handle) {}

void RendererFrontEnd::destroy_texture(TextureHandle handle) {}

void RendererFrontEnd::destroy_binding_set(BindingSetHandle handle) {}

void RendererFrontEnd::destroy_render_pass(RenderPassHandle handle) {
  device->destroy_render_pass(handle);
}

bool RendererFrontEnd::update_binding_set(BindingSetHandle set,
                                          BindingSetUpdateInfo *update_infos,
                                          u32 update_count) {
  return true;
}

void RendererFrontEnd::set_pipeline_binding_set(PipelineHandle pipeline,
                                                BindingSetHandle set,
                                                u32 set_index) {}

void RendererFrontEnd::upload_buffer_data(void *data, BufferHandle dst_buffer,
                                          u64 size, u64 offset) {}

void RendererFrontEnd::upload_to_image(void *data, TextureHandle dst_image) {}

void RendererFrontEnd::print_gpu_stats() {}

} // namespace Helix
