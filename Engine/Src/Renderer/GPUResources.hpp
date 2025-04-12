#pragma once
#include "GPUResourceTypes.hpp"

namespace Helix {

namespace MemoryState {
enum Enum { None = 0, Dynamic = 1 << 0, Static = 1 << 1, Persistent = 1 << 2 };
}

namespace BufferUsage {
enum Enum {
  None = 0,
  Vertex = 1 << 0,
  Index = 1 << 1,
  Uniform = 1 << 2,
  TransferSrc = 1 << 3,
  TransferDest = 1 << 4
};
}

namespace MemoryAccess {
enum Enum {
  None = 0,
  GPU_ONLY = 1 << 0,
  CPU_TO_GPU = 1 << 1,
  GPU_TO_CPU = 1 << 2,
  CPU_ONLY = 1 << 3
};
}
namespace TextureType {
enum Enum {
  Texture1D,
  Texture2D,
  Texture3D,
  Texture_1D_Array,
  Texture_2D_Array,
  Texture_3D_Array,
};
}

namespace TextureFormat {
enum Enum { Undefined, D32, B8G8R8A8_UNORM, R8G8B8A8_SRGB, R8G8B8A8_UNORM };
}

namespace TextureUsage {
enum Enum {
  RenderTarget = 0,
  Compute = 1 << 0,
  TransferSrc = 1 << 1,
  TransferDest = 1 << 2,
  Depth = 1 << 3,
  Sampled = 1 << 4
};
}

namespace CullMode {
enum Enum { None, Front, Back, FrontAndBack };
}

#pragma region Creation
struct BufferCreation {
  BufferUsage::Enum usage_flags = BufferUsage::None;
  MemoryState::Enum memory_state_flags = MemoryState::None;
  MemoryAccess::Enum memory_access_flags = MemoryAccess::None;
  u32 size = 0;
  void *initial_data = nullptr;
  cstring name = nullptr;

  BufferCreation &reset();
};

struct ShaderCreateInfo {
  cstring filename;
  ShaderStage::Enum stage;
};

struct PipelineCreation {
  ShaderCreateInfo *shader_create_infos;
  u32 shader_count = 0;
  PipelineType::Enum pipeline_type{};
  cstring name;

  CullMode::Enum cull_mode = CullMode::Back;

  PipelineCreation &reset();
};

struct TextureCreation {
  void *initial_data = nullptr;
  u16 width = 1;
  u16 height = 1;
  u16 depth = 1;
  u16 array_layer_count = 1;
  u16 array_base_level = 0;
  u8 mip_level_count = 1;
  u8 mip_base_level = 0;

  TextureUsage::Enum usage;

  ResourceHandle alias_image{};

  TextureFormat::Enum format = TextureFormat::Undefined;
  TextureType::Enum type = TextureType::Texture2D;

  cstring name = nullptr;
};
#pragma endregion Creation

struct BufferResource {
  BufferHandle handle;
  BufferHandle internal_handle;
};

struct BufferInfo {
  u32 size = 0;
  cstring name = nullptr;
};

struct PipelineResource {
  PipelineHandle handle;
  PipelineHandle internal_handle;
};

struct TextureResource {
  TextureHandle handle;
  TextureHandle internal_handle;
};

struct ShaderUniform {
  ResourceHandle internal_resource_handle;
  ResourceType::Enum resource_type;
  u32 binding;
  union {
    struct {
      u32 offset;
      u32 range;
    } buffer_info;

    struct {
      // TODO: Texture data
      u32 size;
    } texture_info;
  };
};

struct ShaderUniformSet {
  ShaderUniform *uniforms = nullptr;
  u32 uniform_count = 0;
  u32 set_index;
};

} // namespace Helix
