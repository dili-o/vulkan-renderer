#include "SpirvParser.hpp"
#include "Core/Assert.hpp"
#include "Core/Memory.hpp"
#include "Renderer/Vulkan/VulkanTypes.hpp"

#include <cstring>
#include <spirv_reflect.h>
#include <vulkan/vulkan_core.h>

namespace Helix {

VkFormat get_unorm_variant(u32 component_count) {
  switch (component_count) {
  case 4:
    return VK_FORMAT_R8G8B8A8_UNORM;
  default:
    HERROR("Unknown Unorm Variant!");
    return VK_FORMAT_UNDEFINED;
  }
}

// Returns a new set layout if one does not already exist in the ParseResult
VulkanDescriptorSetLayout &
get_set(Array<VulkanDescriptorSetLayout> &set_layouts, u32 set_index) {
  HeapAllocator *allocator = &MemoryService::instance()->system_allocator;
  if (set_layouts.size == 0) {
    VulkanDescriptorSetLayout &layout = set_layouts.push_use();
    layout.vk_bindings.init(allocator, 4);
    return layout;
  }

  for (u32 i = 0; i < set_layouts.size; ++i) {
    if (set_layouts[i].set_index == set_index)
      return set_layouts[i];
  }

  VulkanDescriptorSetLayout &layout = set_layouts.push_use();
  layout.vk_bindings.init(allocator, 4);
  return layout;
}

// Returns a new set binding if one does not already exist in the ParseResult
VkDescriptorSetLayoutBinding &
get_binding(Array<VkDescriptorSetLayoutBinding> &set_bindings,
            u32 binding_index, bool &is_unique) {
  if (set_bindings.size == 0) {
    is_unique = true;
    return set_bindings.push_use();
  }

  for (u32 i = 0; i < set_bindings.size; ++i) {
    if (set_bindings[i].binding == binding_index) {
      is_unique = false;
      return set_bindings[i];
    }
  }

  is_unique = true;
  return set_bindings.push_use();
}
void parse_binary(const u32 *data, size_t data_size,
                  ParseResult &parse_result) {
  // NOTE: StackAllocator clearing is handled by VulkanBackend::create_pipeline
  SpvReflectShaderModule module = {};
  SpvReflectResult result =
      spvReflectCreateShaderModule(data_size, data, &module);
  HASSERT(result == SPV_REFLECT_RESULT_SUCCESS);

  uint32_t count = 0;
  result = spvReflectEnumerateDescriptorSets(&module, &count, NULL);
  HASSERT(result == SPV_REFLECT_RESULT_SUCCESS);

  std::vector<SpvReflectDescriptorSet *> sets(count);
  result = spvReflectEnumerateDescriptorSets(&module, &count, sets.data());
  HASSERT(result == SPV_REFLECT_RESULT_SUCCESS);

  // Descriptor Sets
  StackAllocator *stack_allocator = &MemoryService::instance()->stack_allocator;

  for (u32 i = 0; i < (u32)sets.size(); ++i) {
    // Check if set == 0 (Reserved for bindless set)
    if (sets[i]->set == 0)
      continue;
    // Check if we've already added the set
    VulkanDescriptorSetLayout &set_layout =
        get_set(parse_result.set_layouts, sets[i]->set);
    set_layout.set_index = sets[i]->set;

    for (u32 j = 0; j < sets[i]->binding_count; ++j) {
      SpvReflectDescriptorBinding *spirv_binding = sets[i]->bindings[j];
      bool is_unique = false;
      VkDescriptorSetLayoutBinding &vk_binding = get_binding(
          set_layout.vk_bindings, spirv_binding->binding, is_unique);
      // First pass
      if (is_unique) {
        vk_binding.binding = spirv_binding->binding;
        vk_binding.descriptorType =
            (VkDescriptorType)spirv_binding->descriptor_type;
        vk_binding.descriptorCount = 1;
        vk_binding.stageFlags = (VkShaderStageFlagBits)module.shader_stage;
        vk_binding.pImmutableSamplers = nullptr;
      } else {
        vk_binding.stageFlags |= (VkShaderStageFlagBits)module.shader_stage;
      }
    }
  }

  // Push Constants
  u32 push_count = 0;
  result = spvReflectEnumeratePushConstantBlocks(&module, &push_count, nullptr);
  HASSERT(result == SPV_REFLECT_RESULT_SUCCESS);

  std::vector<SpvReflectBlockVariable *> push_constants(push_count);
  if (push_count) {
    // TODO: Right now you can only have the same push constant in all stages
    result = spvReflectEnumeratePushConstantBlocks(&module, &push_count,
                                                   push_constants.data());
    HASSERT(result == SPV_REFLECT_RESULT_SUCCESS);

    parse_result.push_constant.size = push_constants[0]->size;
    parse_result.push_constant.offset = push_constants[0]->offset;
    parse_result.push_constant.stageFlags =
        VK_SHADER_STAGE_ALL; // TODO: Make stage specific
  }

  // VERTEX ONLY (Vertex bindings and attributes)
  if (module.shader_stage != SPV_REFLECT_SHADER_STAGE_VERTEX_BIT) {
    return;
  }

  u32 input_variable_count = 0;
  result = spvReflectEnumerateInputVariables(&module, &input_variable_count,
                                             nullptr);
  HASSERT(result == SPV_REFLECT_RESULT_SUCCESS);

  Array<SpvReflectInterfaceVariable *> input_variables{};
  input_variables.init(stack_allocator, input_variable_count,
                       input_variable_count);
  result = spvReflectEnumerateInputVariables(&module, &input_variable_count,
                                             input_variables.data);
  HASSERT(result == SPV_REFLECT_RESULT_SUCCESS);

  VkVertexInputBindingDescription &binding_description =
      parse_result.vertex_binding;
  binding_description.binding = 0;
  binding_description.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
  binding_description.stride = 0;

  parse_result.vertex_attributes = (VkVertexInputAttributeDescription *)halloca(
      sizeof(VkVertexInputAttributeDescription) * input_variable_count,
      stack_allocator);

  parse_result.vertex_attribute_count = input_variable_count;

  // Sort input attributes
  std::sort(input_variables.data, input_variables.data + input_variables.size,
            [](const SpvReflectInterfaceVariable *a,
               const SpvReflectInterfaceVariable *b) {
              return a->location < b->location;
            });

  for (auto input_variable : input_variables) {
    if (!strcmp(input_variable->name, "gl_VertexIndex")) {
      --parse_result.vertex_attribute_count;
      continue;
    }
    if (!strcmp(input_variable->name, "gl_InstanceIndex")) {
      --parse_result.vertex_attribute_count;
      continue;
    }
    u32 location = input_variable->location;
    SpvReflectFormat format = input_variable->format;

    VkVertexInputAttributeDescription &attribute =
        parse_result.vertex_attributes[location];
    attribute.binding = 0; // TODO: For now assume only one descriptor binding
    attribute.location = location;
    attribute.format = (VkFormat)format;
    attribute.offset = binding_description.stride;

    // NOTE: Input attributes are marked with "_UNORM" if they should be
    // formatted as Unorms
    if (strstr(input_variable->name, "UNORM")) {
      attribute.format =
          get_unorm_variant(input_variable->numeric.vector.component_count);
      binding_description.stride +=
          input_variable->numeric.vector.component_count;
    } else {
      binding_description.stride +=
          (input_variable->numeric.vector.component_count *
           (input_variable->numeric.scalar.width / 8));
    }
  }
}
} // namespace Helix
