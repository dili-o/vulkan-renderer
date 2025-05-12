#pragma once

#include "Containers/ResourcePool.hpp"

namespace Helix {

using BufferHandle = ResourceHandle;
using PipelineHandle = ResourceHandle;
using TextureHandle = ResourceHandle;
using BindingSetLayoutHandle = ResourceHandle;
using BindingSetHandle = ResourceHandle;

namespace ShaderStage {
enum Enum {
  Vertex = 1 << 0,
  Fragment = 1 << 1,
  Compute = 1 << 2,
  AllStage = 1 << 3
};
}

namespace PipelineType {
enum Enum { Graphics, Compute };
}

namespace ResourceType {
enum Enum { Buffer, Texture };
}

namespace BindingType {
enum Enum { CombinedSampler, UniformBuffer };
}
} // namespace Helix
