#pragma once

#include "Containers/ResourcePool.hpp"
#include "Core/Service.hpp"
#include "GPUResources.hpp"
#include "RendererTypes.hpp"

namespace Helix {

const u32 max_frames_in_flight = 2;

struct RendererBackend;

struct RendererFrontEnd : public Service {
  virtual void init(void *config) override;
  virtual void shutdown() override;

  HELIX_DECLARE_SERVICE(RendererFrontEnd);

  void on_resize(u16 width, u16 height);

  bool draw_frame(RenderPacket *packet);

  bool begin_frame(RenderPacket *packet);

  bool end_frame(RenderPacket *packet);

  bool load_model(cstring path);

  ResourceHandle create_buffer(BufferCreation &creation);
  void destroy_buffer(ResourceHandle handle);

  u32 current_frame;
  RendererBackend *backend{nullptr};

  ResourcePool<BufferResource> buffers{};
  ResourceHandle vertex_buffer{};
  ResourceHandle index_buffer{};
  ResourceHandle uniform_buffers[max_frames_in_flight];
};
} // namespace Helix
