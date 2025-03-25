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

  PipelineCreation &reset();
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
} // namespace Helix
