#include "RendererBackend.hpp"
#include "Core/Memory.hpp"
#include "Renderer/RendererTypes.hpp"
#include "Renderer/Vulkan/VulkanBackend.hpp"
#include <memory>

namespace Helix {
RendererBackend *RendererBackendCreate(RendererBackendType type) {

  if (type == RENDERER_BACKEND_TYPE_VULKAN) {
    void *memory = halloca(sizeof(VulkanBackend),
                           &MemoryService::instance()->system_allocator);
    // Create 'new' backend and return it.
    return new (memory) VulkanBackend();
  }

  return nullptr;
}
} // namespace Helix
