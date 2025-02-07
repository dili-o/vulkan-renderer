#include "SpirvParser.hpp"

#include <string.h>

#include "Core/Numerics.hpp"
#include "Core/String.hpp"

// #define SPIRV_REFLECT_USE_SYSTEM_SPIRV_H

#include <spirv_reflect.h>

namespace Helix {
namespace spirv {

static const u32 k_bindless_texture_binding = 10;

struct Member {
  u32 id_index;
  u32 offset;

  StringView name;
};

struct Id {
  SpvOp op;
  u32 set;
  u32 binding;

  // For integers and floats
  u8 width;
  u8 sign;

  // For arrays, vectors and matrices
  u32 type_index;
  u32 count;

  // For variables
  SpvStorageClass storage_class;

  // For constants
  u32 value;

  // For structs
  StringView name;
  Array<Member> members;
};

VkShaderStageFlags parse_execution_model(SpvExecutionModel model) {
  switch (model) {
    case (SpvExecutionModelVertex): {
      return VK_SHADER_STAGE_VERTEX_BIT;
    }
    case (SpvExecutionModelGeometry): {
      return VK_SHADER_STAGE_GEOMETRY_BIT;
    }
    case (SpvExecutionModelFragment): {
      return VK_SHADER_STAGE_FRAGMENT_BIT;
    }
    case (SpvExecutionModelKernel): {
      return VK_SHADER_STAGE_COMPUTE_BIT;
    }
  }

  return 0;
}

static void add_binding_if_unique(
    DescriptorSetLayoutCreation& creation,
    DescriptorSetLayoutCreation::Binding& binding) {
  bool found = false;
  for (u32 i = 0; i < creation.num_bindings; ++i) {
    const DescriptorSetLayoutCreation::Binding& b = creation.bindings[i];
    if (b.type == binding.type && b.index == binding.index) {
      found = true;
      break;
    }
  }

  if (!found) {
    creation.add_binding(binding);
  }
}

void parse_binary(const u32* data, size_t data_size, StringBuffer& name_buffer,
                  ParseResult* parse_result) {
  /////
  SpvReflectShaderModule module = {};
  SpvReflectResult result =
      spvReflectCreateShaderModule(data_size, data, &module);
  HASSERT(result == SPV_REFLECT_RESULT_SUCCESS);

  uint32_t count = 0;
  result = spvReflectEnumerateDescriptorSets(&module, &count, NULL);
  HASSERT(result == SPV_REFLECT_RESULT_SUCCESS);

  std::vector<SpvReflectDescriptorSet*> sets(count);
  result = spvReflectEnumerateDescriptorSets(&module, &count, sets.data());
  HASSERT(result == SPV_REFLECT_RESULT_SUCCESS);

  for (u32 i = 0; i < sets.size(); ++i) {
    bool image_detected = false;
    DescriptorSetLayoutCreation& creation = parse_result->sets[sets[i]->set];
    creation.set_set_index(sets[i]->set);
    for (u32 j = 0; j < sets[i]->binding_count; ++j) {
      DescriptorSetLayoutCreation::Binding binding{};
      SpvReflectDescriptorBinding* spirv_binding = sets[i]->bindings[j];

      binding.name = spirv_binding->name;
      binding.count = spirv_binding->count;
      binding.index = spirv_binding->binding;
      binding.type = (VkDescriptorType)spirv_binding->descriptor_type;
      image_detected =
          (binding.type == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER ||
           binding.type == VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);

      add_binding_if_unique(creation, binding);
    }
  }

  if (module.push_constant_block_count) {
    // TODO: Multiple push constants
    SpvReflectBlockVariable& push_constant = module.push_constant_blocks[0];
    parse_result->push_constant.size = push_constant.size;
    parse_result->push_constant.offset = push_constant.offset;
    parse_result->push_constant.stageFlags = module.shader_stage;
  }

  if (module.entry_points) {
    if (module.entry_points->local_size.x) {
      parse_result->local_size[0] = module.entry_points->local_size.x;
      parse_result->local_size[1] = module.entry_points->local_size.y;
      parse_result->local_size[2] = module.entry_points->local_size.z;
    }
  }

  parse_result->set_count = max(parse_result->set_count, (u32)sets.size());
  return;
}

}  // namespace spirv
}  // namespace Helix
