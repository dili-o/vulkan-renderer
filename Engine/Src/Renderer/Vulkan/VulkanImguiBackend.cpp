#include "VulkanImguiBackend.hpp"
#include "Core/Profiler.hpp"
#include "Renderer/GPUResourceTypes.hpp"
#include "Renderer/RendererFrontEnd.hpp"
#include "Renderer/Vulkan/CommandBuffer.hpp"
#include "Renderer/Vulkan/VulkanBackend.hpp"
#include "Renderer/Vulkan/VulkanTypes.hpp"
// Vendor
#include <Vendor/imgui/imgui.h>

namespace Helix {

void VulkanImguiBackend::init(void *configuration) {}

void VulkanImguiBackend::shutdown() {}

void VulkanImguiBackend::render_frame(RenderPacket *packet) {
  HELIX_PROFILER_FUNCTION();
}
} // namespace Helix
