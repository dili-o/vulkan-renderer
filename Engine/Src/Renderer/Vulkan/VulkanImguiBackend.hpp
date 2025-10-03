#pragma once

#include "Renderer/GPUResourceTypes.hpp"
#include "Renderer/ImguiFrontend.hpp"

namespace Helix {

struct VulkanBackend;

struct VulkanImguiBackend : public ImguiBackend {
  virtual void init(void *configuration) override;
  virtual void shutdown() override;

  virtual void render_frame(RenderPacket *packet) override;

  TextureHandle font_texture{};
  Array<BufferHandle> vertex_buffers;
  Array<BufferHandle> index_buffers;
  PipelineHandle pipeline{};

  VulkanBackend *backend = nullptr;
};

} // namespace Helix
