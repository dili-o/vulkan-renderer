#pragma once
#include "Containers/ResourcePool.hpp"
#include "Core/Assert.hpp"
#include "GPUResourceTypes.hpp"

namespace Helix {

namespace MemoryState {
enum Enum { None = 0, Dynamic = 1 << 0, Static = 1 << 1, Mapped = 1 << 2 };
}

namespace BufferUsage {
enum Enum {
  None = 0,
  Vertex = 1 << 0,
  Index = 1 << 1,
  Uniform = 1 << 2,
  TransferSrc = 1 << 3,
  TransferDest = 1 << 4,
  IndexedIndirect = 1 << 5,
  ShaderAddress = 1 << 6,
  Storage = 1 << 7
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

namespace LoadOp {
enum Enum { Load, Clear, DontCare };
}

namespace StoreOp {
enum Enum { Store, DontCare };
}

#pragma region Creation
struct BufferCreation {
  BufferUsage::Enum usage_flags = BufferUsage::None;
  MemoryState::Enum memory_state_flags = MemoryState::None;
  MemoryAccess::Enum memory_access_flags = MemoryAccess::None;
  u64 size = 0;
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
  BindingSetLayoutHandle set_layouts[5]; // TODO: Remove magic number
  u32 set_layout_count = 0;

  RenderPassHandle render_pass;
  bool enable_depth_write = true;
  bool enable_depth_test = true;

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

  TextureHandle alias_image{};

  TextureFormat::Enum format = TextureFormat::Undefined;
  TextureType::Enum type = TextureType::Texture2D;

  cstring name = nullptr;
};

#pragma endregion Creation

// TODO: Expand
struct BufferInfo {
  u32 size = 0;
  cstring name = nullptr;
};

// TODO: Expand
struct TextureInfo {};

// TODO: Expand
struct PipelineInfo {};

#define MAX_BINDING_PER_SET 16

struct BindingInfo {
  u32 binding;
  u32 resource_count;
  ShaderStage::Enum stage;
  BindingType::Enum type;
};

struct BindingSetUpdateInfo {
  ResourceHandle resource_handle;
  ResourceType::Enum resource_type;
  u32 binding = 0;
  u32 resource_index = 0;
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

struct BindingSetLayoutCreation {
  BindingInfo binding_infos[MAX_BINDING_PER_SET];
  u32 binding_count = 0;
  bool is_bindless = false;
  cstring name = nullptr;

  BindingSetLayoutCreation &reset() {
    binding_count = 0;
    is_bindless = false;
    name = nullptr;
    return *this;
  }

  BindingSetLayoutCreation &add_binding(u32 binding, u32 resource_count,
                                        ShaderStage::Enum stage,
                                        BindingType::Enum type) {
    HASSERT(binding < 15);
    binding_infos[binding_count].binding = binding;
    binding_infos[binding_count].resource_count = resource_count;
    binding_infos[binding_count].stage = stage;
    binding_infos[binding_count].type = type;
    ++binding_count;
    return *this;
  }
};

struct BindingSetCreation {
  BindingSetLayoutHandle layout;
  cstring name = nullptr;

  BindingSetCreation &reset() {
    name = nullptr;
    layout.index = k_invalid_index;
    return *this;
  }
};

struct AttachmentOps {
  LoadOp::Enum load_op;
  StoreOp::Enum store_op;
  TextureFormat::Enum format = TextureFormat::Undefined;
};

#define MAX_COLOR_ATTACHMENTS 8

struct RenderPassCreation {
  AttachmentOps colour_attachments[MAX_COLOR_ATTACHMENTS];
  u32 num_colour_attachments{0};
  AttachmentOps depth_attachment{};

  RenderPassCreation &add_color_attachment(LoadOp::Enum load_op,
                                           StoreOp::Enum store_op,
                                           TextureFormat::Enum format) {
    colour_attachments[num_colour_attachments].format = format;
    colour_attachments[num_colour_attachments].store_op = store_op;
    colour_attachments[num_colour_attachments].load_op = load_op;
    ++num_colour_attachments;
    return *this;
  }

  RenderPassCreation &add_depth_attachment(LoadOp::Enum load_op,
                                           StoreOp::Enum store_op,
                                           TextureFormat::Enum format) {
    depth_attachment.format = format;
    depth_attachment.store_op = store_op;
    depth_attachment.load_op = load_op;
    return *this;
  }
};

struct RenderPass {
  AttachmentOps colour_attachments[MAX_COLOR_ATTACHMENTS];
  u32 num_colour_attachments;
  AttachmentOps depth_attachment;
};

} // namespace Helix
