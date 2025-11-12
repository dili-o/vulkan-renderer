#include "SpirvParser.hpp"
#include "Containers/Array.hpp"
#include "Core/Assert.hpp"
#include "Core/Memory.hpp"
// Vendor
#include <cstring>
#include <spirv_reflect.h>

namespace hlx {

VkFormat get_unorm_variant(u32 component_count) {
  switch (component_count) {
  case 4:
    return VK_FORMAT_R8G8B8A8_UNORM;
  default:
    HERROR("Unknown Unorm Variant!");
    return VK_FORMAT_UNDEFINED;
  }
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
void parse_binary(const u32 *data, size_t data_size, ParseResult &parse_result,
                  char **entry_point_name) {
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

  // Entry point
  *entry_point_name =
      (char *)halloca(strlen(module.entry_point_name) + 1,
                      &MemoryService::instance()->system_allocator);
  memset(*entry_point_name, 0, strlen(module.entry_point_name) + 1);
  memcpy(*entry_point_name, module.entry_point_name,
         strlen(module.entry_point_name));

  // VERTEX ONLY (Vertex bindings and attributes)
  if (module.shader_stage != SPV_REFLECT_SHADER_STAGE_VERTEX_BIT) {
    spvReflectDestroyShaderModule(&module);
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
    if (!input_variable->name) {
      --parse_result.vertex_attribute_count;
      continue;
    }
    if (!strcmp(input_variable->name, "gl_VertexIndex")) {
      --parse_result.vertex_attribute_count;
      continue;
    }
    if (!strcmp(input_variable->name, "gl_InstanceIndex")) {
      --parse_result.vertex_attribute_count;
      continue;
    }
    if (!strcmp(input_variable->name, "gl_DrawIDARB")) {
      --parse_result.vertex_attribute_count;
      continue;
    }

    u32 location = input_variable->location;
    SpvReflectFormat format = input_variable->format;

    VkVertexInputAttributeDescription &attribute =
        parse_result.vertex_attributes[location];
    attribute.binding = 0; // TODO: For now assume only one description binding
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
  spvReflectDestroyShaderModule(&module);
}
} // namespace hlx
