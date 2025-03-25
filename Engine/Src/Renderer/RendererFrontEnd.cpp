#include "Renderer/RendererFrontEnd.hpp"
#include "Containers/ResourcePool.hpp"
#include "Core/Log.hpp"
#include "Core/Memory.hpp"
#include "Renderer/GPUResources.hpp"
#include "Renderer/Vulkan/VulkanBackend.hpp" // TODO: Remove
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

  buffers.init(allocator, 10);

  // Create Uniform Buffers
  BufferCreation creation{};
  creation.reset();
  creation.usage_flags = BufferUsage::Uniform;
  creation.memory_state_flags = MemoryState::Persistent;
  creation.memory_access_flags = MemoryAccess::CPU_TO_GPU;
  creation.size = sizeof(UniformBufferObject);
  creation.initial_data = nullptr;
  creation.name = "uniform_buffer_";
  for (u32 i = 0; i < max_frames_in_flight; ++i) {
    uniform_buffers[i] = create_buffer(creation);
  }
  // TODO: Remove
  VulkanBackend *bc = (VulkanBackend *)backend;
  bc->create_descriptor_sets(max_frames_in_flight);
}

void RendererFrontEnd::shutdown() {
  for (u32 i = 0; i < max_frames_in_flight; ++i) {
    destroy_buffer(uniform_buffers[i]);
  }
  backend->shutdown();
  buffers.shutdown();
  hfree(backend, &MemoryService::instance()->system_allocator);
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
  return backend->end_frame(packet);
  current_frame = (current_frame + 1) % max_frames_in_flight;
}

bool RendererFrontEnd::load_model(cstring path) {
  // TODO: Create Vertex buffer
  // TODO: Create Index buffer
  return true;
}

ResourceHandle RendererFrontEnd::create_buffer(BufferCreation &creation) {
  ResourceHandle handle = buffers.obtain_new();
  if (handle.index == k_invalid_index) {
    HERROR("Failed to obtain new buffer resource");
    return handle;
  }

  ResourceHandle internal_handle = backend->create_buffer(creation);
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

void RendererFrontEnd::destroy_buffer(ResourceHandle handle) {
  if (handle.index == k_invalid_index) {
    HWARN("Attempting to destroy an invalid buffer");
    return;
  }
  BufferResource *buffer = buffers.obtain(handle);
  backend->destroy_buffer(buffer->internal_handle);

  buffers.release(handle);
}
} // namespace Helix
