#include "RendererBackend.hpp"
#include "Core/Log.hpp"
#include "Renderer/RendererTypes.hpp"
#include "Renderer/Vulkan/VkGpuDevice.hpp"

namespace hlx {
GpuDevice *create_device(RendererBackendType type) {
  GpuDevice *device = nullptr;
  if (type == RENDERER_BACKEND_TYPE_VULKAN) {
    device = create_vulkan_device();
    device->type = RENDERER_BACKEND_TYPE_VULKAN;
    return device;
  } else {
    HCRITICAL("Unknown Backend Type");
  }
  return nullptr;
}

bool destroy_device(GpuDevice *device) {
  if (device->type == RENDERER_BACKEND_TYPE_VULKAN) {
    destroy_vulkan_device((VkGpuDevice *)device);
    MemorySys::system_allocator()->deallocate(device);
  } else {
  }
  return false;
}

} // namespace hlx
