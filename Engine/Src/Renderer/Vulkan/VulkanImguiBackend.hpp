#pragma once

#include "Renderer/GPUResourceTypes.hpp"
#include "Renderer/ImguiFrontend.hpp"
#include <imgui/imgui.h>

namespace Helix {

struct VulkanBackend;

struct VulkanImguiBackend : public ImguiBackend {
  virtual void init(void *configuration) override;
  virtual void shutdown() override;

  TextureHandle font_texture{};
  BufferHandle vertex_buffer{};
  BufferHandle index_buffer{};
  PipelineHandle pipeline{};

  virtual void begin_frame() override;
  virtual void render_frame(RenderPacket *packet) override;

  VulkanBackend *backend = nullptr;
};

} // namespace Helix
