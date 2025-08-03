#pragma once

#include "Renderer/GPUResourceTypes.hpp"
#include "Renderer/ImguiFrontend.hpp"
#include <Vendor/imgui/imgui.h>

namespace Helix {

#define FRAMES_IN_FLIGHT 2

struct VulkanBackend;

struct VulkanImguiBackend : public ImguiBackend {
  virtual void init(void *configuration) override;
  virtual void shutdown() override;

  TextureHandle font_texture{};
  BufferHandle vertex_buffers[FRAMES_IN_FLIGHT];
  BufferHandle index_buffers[FRAMES_IN_FLIGHT];
  PipelineHandle pipeline{};

  virtual void begin_frame() override;
  virtual void render_frame(RenderPacket *packet) override;

  VulkanBackend *backend = nullptr;
};

} // namespace Helix
