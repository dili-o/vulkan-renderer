#include "GPUResources.hpp"

namespace Helix {
BufferCreation &BufferCreation::reset() {

  usage_flags = BufferUsage::None;
  memory_state_flags = MemoryState::None;
  memory_access_flags = MemoryAccess::None;
  size = 0;
  initial_data = nullptr;
  name = nullptr;
  return *this;
}

} // namespace Helix
