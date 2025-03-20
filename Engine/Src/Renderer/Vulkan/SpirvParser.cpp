#include "SpirvParser.hpp"
#include "Containers/Array.hpp"
#include "Core/Assert.hpp"
#include "Core/Memory.hpp"

#include <spirv_reflect.h>

namespace Helix {

void parse_binary(const u32 *data, size_t data_size) {
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

  for (auto input_variable : input_variables) {
    if (!strcmp(input_variable->name, "gl_VertexIndex")) {
      continue;
    }
    u32 location = input_variable->location;
    SpvReflectFormat format = input_variable->format;
  }
}
} // namespace Helix
