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

  HELIX_SERVICE_INIT_MSG(RendererFrontEnd);
  s_renderer_frontend = this;
}

void RendererFrontEnd::shutdown() {

  destroy_device(device);

  HELIX_SERVICE_SHUTDOWN_MSG(RendererFrontEnd);
}

void RendererFrontEnd::on_resize(u16 width, u16 height) {}

bool RendererFrontEnd::render_frame(RenderPacket *packet) {
  HELIX_PROFILER_FUNCTION_COLOR(tracy::Color::Orange);

  return true;
}

bool RendererFrontEnd::begin_frame(RenderPacket *packet) { return true; }

bool RendererFrontEnd::end_frame(RenderPacket *packet) {
  HELIX_PROFILER_FUNCTION();
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
  return RenderPassHandle();
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

void RendererFrontEnd::destroy_render_pass(RenderPassHandle handle) {}

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
