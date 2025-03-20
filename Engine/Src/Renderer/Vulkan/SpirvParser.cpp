#include "SpirvParser.hpp"
#include "Containers/Array.hpp"
#include "Core/Assert.hpp"
#include "Core/Memory.hpp"
#include "Renderer/Vulkan/VulkanTypes.hpp"

#include <spirv_reflect.h>

namespace Helix {

void parse_binary(const u32 *data, size_t data_size,
                  PipelineCreation &creation) {
  // NOTE: StackAllocator clearing is handled by VulkanBackend::create_pipeline
  SpvReflectShaderModule module = {};
  SpvReflectResult result =
      spvReflectCreateShaderModule(data_size, data, &module);
  HASSERT(result == SPV_REFLECT_RESULT_SUCCESS);

  u32 input_variable_count = 0;
  result = spvReflectEnumerateInputVariables(&module, &input_variable_count,
                                             nullptr);
  HASSERT(result == SPV_REFLECT_RESULT_SUCCESS);

  StackAllocator *stack_allocator = &MemoryService::instance()->stack_allocator;
  Array<SpvReflectInterfaceVariable *> input_variables{};
  input_variables.init(stack_allocator, input_variable_count,
                       input_variable_count);
  result = spvReflectEnumerateInputVariables(&module, &input_variable_count,
                                             input_variables.data);
  HASSERT(result == SPV_REFLECT_RESULT_SUCCESS);

  if (module.shader_stage != SPV_REFLECT_SHADER_STAGE_VERTEX_BIT) {
    return;
  }
  creation.binding_descriptions.init(stack_allocator, 1, 1);
  creation.attribute_descriptions.init(stack_allocator, input_variable_count);

  VkVertexInputBindingDescription &binding_description =
      creation.binding_descriptions[0];
  binding_description.binding = 0;
  binding_description.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
  binding_description.stride = 0;

  for (auto input_variable : input_variables) {
    if (!strcmp(input_variable->name, "gl_VertexIndex")) {
      continue;
    }
    u32 location = input_variable->location;
    SpvReflectFormat format = input_variable->format;

    VkVertexInputAttributeDescription attribute{};
    attribute.binding = 0;
    attribute.location = location;
    attribute.format = (VkFormat)format;
    attribute.offset = binding_description.stride;

    creation.attribute_descriptions.push(attribute);

    binding_description.stride +=
        (input_variable->numeric.vector.component_count *
         (input_variable->numeric.scalar.width / 8));
  }
}
} // namespace Helix
