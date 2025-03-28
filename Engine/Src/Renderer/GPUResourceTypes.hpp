#pragma once

#include "Containers/ResourcePool.hpp"

namespace Helix {

using BufferHandle = ResourceHandle;
using PipelineHandle = ResourceHandle;
using TextureHandle = ResourceHandle;
// TODO MAke View and VKimage handles as well

namespace ShaderStage {
enum Enum { Vertex, Fragment, Compute };
}

namespace PipelineType {
enum Enum { Graphics, Compute };
}

namespace ResourceType {
enum Enum { Buffer, Texture };
}
} // namespace Helix
