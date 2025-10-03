#include "Platform.hpp"

#ifdef HELIX_PLATFORM_WINDOWS
#include <windows.h>

namespace Helix {

u64 Platform::get_current_processor_id() {
  return (u64)GetCurrentProcessorNumber();
}

u64 Platform::get_current_thread_id() { return (u64)GetCurrentThreadId(); }

#ifndef HELIX_PLATFORM_SDL3
i32 Platform::get_logical_processor_count() {
  SYSTEM_INFO sysinfo;
  GetSystemInfo(&sysinfo);
  return sysinfo.dwNumberOfProcessors;
}
#endif // !HELIX_PLATFORM_SDL3

// TODO: Rest of the Platform API

} // namespace Helix
#endif
