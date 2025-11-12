#pragma once

#include "Containers/ResourcePool.hpp"

namespace hlx {
struct BufferTag {};
struct PipelineTag {};
struct TextureTag {};
struct SamplerTag {};
struct BindingSetLayoutTag {};
struct BindingSetTag {};
struct RenderPassTag {};
struct CommandBufferTag {};

using BufferHandle = ResourceHandle<BufferTag>;
using PipelineHandle = ResourceHandle<PipelineTag>;
using TextureHandle = ResourceHandle<TextureTag>;
using SamplerHandle = ResourceHandle<SamplerTag>;
using BindingSetLayoutHandle = ResourceHandle<BindingSetLayoutTag>;
using BindingSetHandle = ResourceHandle<BindingSetTag>;
using RenderPassHandle = ResourceHandle<RenderPassTag>;
using CommandBufferHandle = ResourceHandle<CommandBufferTag>;

namespace ShaderStage {
enum Enum {
  Vertex   = 1 << 0,
  Fragment = 1 << 1,
  Compute  = 1 << 2,
  Geometry = 1 << 3,
  AllStage = 1 << 4
};
}

namespace PipelineType {
enum Enum { Graphics, Compute };
}

namespace ResourceType {
enum Enum { Buffer, Texture };
}

namespace BindingType {
enum Enum { CombinedSampler, UniformBuffer, StorageBuffer };
}
} // namespace hlx
