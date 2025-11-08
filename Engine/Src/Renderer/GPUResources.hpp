#pragma once
#include "Containers/ResourcePool.hpp"
#include "Core/Assert.hpp"
#include "GPUResourceTypes.hpp"

namespace hlx {

namespace BufferUsage {
enum Enum {
  None = 0,
  Vertex = 1 << 0,
  Index = 1 << 1,
  Uniform = 1 << 2,
  TransferSrc = 1 << 3,
  TransferDst = 1 << 4,
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
enum Enum {
  Undefined,
  D32,
  B8G8R8A8_UNORM,
  R8G8B8A8_SRGB,
  R8G8B8A8_UNORM,
  R32_UINT,
  R32_SINT,
  R32_SFLOAT,
};
}

namespace TextureUsage {
enum Enum {
  Undefined = 0,
  RenderTarget = 1 << 0,
  Compute = 1 << 1,
  TransferSrc = 1 << 2,
  TransferDest = 1 << 3,
  Depth = 1 << 4,
  Sampled = 1 << 5
};
}

namespace SamplerFilter {
enum Enum {
  Linear,
  Nearest,
};
}

namespace SamplerAddressMode {
enum Enum {
  Repeat,
  MirroredRepeat,
  ClampToEdge,
  ClampToBorder,
  MirrorClampToEdge,
};
}

namespace BorderColor {
enum Enum {
  IntOpaqueBlack,
  IntOpaqueWhite,
  FloatOpaqueBlack,
  FloatOpaqueWhite
};
}

namespace CullMode {
enum Enum { None, Front, Back, FrontAndBack };
}

namespace PrimitiveType {
enum Enum { Triangle, Line };
}

namespace CompareOp {
enum Enum {
  Never,
  Less,
  Equal,
  LessOrEqual,
  Greater,
  GreaterOrEqual,
  NotEqual,
  Always
};
}

namespace LoadOp {
enum Enum { Load, Clear, DontCare };
}

namespace StoreOp {
enum Enum { Store, DontCare };
}

#pragma region Creation
#define MAX_ALLOCATION_SIZE UINT64_MAX
struct BufferCreation {
  BufferUsage::Enum usage_flags = BufferUsage::None;
  MemoryAccess::Enum memory_access_flags = MemoryAccess::None;
  u64 size = 0;
  cstring name = nullptr;
  bool mapped;
};

struct ShaderCreateInfo {
  cstring filename;
  ShaderStage::Enum stage;
};

struct PipelineCreation {
  PipelineCreation &reset();

  ShaderCreateInfo *shader_create_infos;
  u32 shader_count = 0;
  PipelineType::Enum pipeline_type{};
  CullMode::Enum cull_mode = CullMode::Back;
  PrimitiveType::Enum primitive_type = PrimitiveType::Triangle;
  BindingSetLayoutHandle set_layouts[5]; // TODO: Remove magic number
  u32 set_layout_count = 0;
  RenderPassHandle render_pass;
  bool enable_depth_write = true;
  bool enable_depth_test = true;
  CompareOp::Enum compare_op = CompareOp::Never;
  cstring name;
};

struct TextureCreation {
  u16 width = 1;
  u16 height = 1;
  u16 depth = 1;
  u16 array_layer_count = 1;
  u16 array_base_level = 0;
  u8 mip_level_count = 1;
  u8 mip_base_level = 0;

  TextureUsage::Enum usage;
  TextureFormat::Enum format = TextureFormat::Undefined;
  TextureType::Enum type = TextureType::Texture2D;

  SamplerHandle sampler;
  // NOTE(Vulkan): Assigning this means we are only creating a VkImageView
  TextureHandle base_texture;

  cstring name = nullptr;
};

struct SamplerCreation {
  SamplerFilter::Enum min_filter = SamplerFilter::Linear;
  SamplerFilter::Enum mag_filter = SamplerFilter::Linear;
  SamplerFilter::Enum mip_filter = SamplerFilter::Linear;

  SamplerAddressMode::Enum address_mode_u = SamplerAddressMode::Repeat;
  SamplerAddressMode::Enum address_mode_v = SamplerAddressMode::Repeat;
  SamplerAddressMode::Enum address_mode_w = SamplerAddressMode::Repeat;

  BorderColor::Enum border_color = BorderColor::IntOpaqueBlack;

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
struct PipelineInfo {
  cstring name;
};

#define MAX_BINDING_PER_SET 16

struct BindingInfo {
  u32 binding;
  u32 resource_count;
  ShaderStage::Enum stage;
  BindingType::Enum type;
};

struct BindingSetUpdateInfo {
  ResourceType::Enum resource_type;
  u32 binding = 0;
  u32 resource_index = 0;
  union {
    struct {
      BufferHandle buffer;
      u32 offset;
      u32 range;
    } buffer_info;

    struct {
      TextureHandle texture;
    } texture_info;
  };

  BindingSetUpdateInfo() { memset(&buffer_info, 0, sizeof(buffer_info)); }
};

struct HLX_API BindingSetLayoutCreation {
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

struct AttachmentInfo {
  TextureHandle texture_handle{k_invalid_index};
  LoadOp::Enum load_op;
  StoreOp::Enum store_op;
};

#define MAX_COLOR_ATTACHMENTS 8

struct RenderPassCreation {
  AttachmentInfo colour_attachments[MAX_COLOR_ATTACHMENTS];
  u32 num_colour_attachments{0};
  AttachmentInfo depth_attachment{};

  RenderPassCreation &add_color_attachment(LoadOp::Enum load_op,
                                           StoreOp::Enum store_op,
                                           TextureHandle handle) {
    colour_attachments[num_colour_attachments].texture_handle = handle;
    colour_attachments[num_colour_attachments].store_op = store_op;
    colour_attachments[num_colour_attachments].load_op = load_op;
    ++num_colour_attachments;
    return *this;
  }

  RenderPassCreation &add_depth_attachment(LoadOp::Enum load_op,
                                           StoreOp::Enum store_op,
                                           TextureHandle handle) {
    depth_attachment.texture_handle = handle;
    depth_attachment.store_op = store_op;
    depth_attachment.load_op = load_op;
    return *this;
  }
};

struct RenderPass {
  AttachmentInfo colour_attachments[MAX_COLOR_ATTACHMENTS];
  u32 num_colour_attachments;
  AttachmentInfo depth_attachment{};
};

} // namespace hlx
