#include "SpirvParser.hpp"
#include "Core/Assert.hpp"
#include "Core/Memory.hpp"
#include "Renderer/Vulkan/VulkanTypes.hpp"

#include <cstring>
#include <spirv_reflect.h>
#include <vulkan/vulkan_core.h>

namespace Helix {

// Returns a new set layout if one does not already exist in the ParseResult
VulkanDescriptorSetLayout &
get_set(Array<VulkanDescriptorSetLayout> &set_layouts, u32 set_index) {
  if (set_layouts.size == 0) {
    VulkanDescriptorSetLayout &layout = set_layouts.push_use();
    layout.vk_bindings = nullptr;
    return layout;
  }

  for (u32 i = 0; i < set_layouts.size; ++i) {
    if (set_layouts[i].set_index == set_index)
      return set_layouts[i];
  }

  VulkanDescriptorSetLayout &layout = set_layouts.push_use();
  layout.vk_bindings = nullptr;
  return layout;
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

  StackAllocator *stack_allocator = &MemoryService::instance()->stack_allocator;
  HeapAllocator *allocator = &MemoryService::instance()->system_allocator;

  for (u32 i = 0; i < (u32)sets.size(); ++i) {
    // Check if we've already added the set
    VulkanDescriptorSetLayout &set_layout =
        get_set(parse_result.set_layouts, sets[i]->set);
    set_layout.set_index = sets[i]->set;
    set_layout.num_bindings = sets[i]->binding_count;

    if (set_layout.vk_bindings == nullptr) {
      set_layout.vk_bindings = (VkDescriptorSetLayoutBinding *)halloca(
          sizeof(VkDescriptorSetLayoutBinding) * set_layout.num_bindings,
          allocator);
      memset(set_layout.vk_bindings, -1,
             sizeof(VkDescriptorSetLayoutBinding) * set_layout.num_bindings);
    }

    for (u32 j = 0; j < sets[i]->binding_count; ++j) {
      SpvReflectDescriptorBinding *spirv_binding = sets[i]->bindings[j];
      VkDescriptorSetLayoutBinding &vk_binding = set_layout.vk_bindings[j];
      // First pass
      if (vk_binding.binding == -1) {

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

  // VERTEX ONLY
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

  for (auto input_variable : input_variables) {
    if (!strcmp(input_variable->name, "gl_VertexIndex")) {
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

    binding_description.stride +=
        (input_variable->numeric.vector.component_count *
         (input_variable->numeric.scalar.width / 8));
  }
}
} // namespace Helix
