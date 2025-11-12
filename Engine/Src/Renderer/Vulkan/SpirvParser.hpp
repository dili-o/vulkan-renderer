#include "Core/Defines.hpp"
#include "Renderer/GPUResources.hpp"
#include "VulkanTypes.hpp"

namespace hlx {
struct ParseResult {
  // TODO: For now assume only one descriptor binding
  VkVertexInputBindingDescription vertex_binding;
  VkVertexInputAttributeDescription *vertex_attributes = nullptr;
  u32 vertex_attribute_count = 0;
  VkPushConstantRange push_constant{}; // NOTE: only assuming one push constant
                                       // in whole pipeline
};

void parse_binary(const u32 *data, size_t data_size, ParseResult &parse_result, char **entry_point_name);
} // namespace hlx
