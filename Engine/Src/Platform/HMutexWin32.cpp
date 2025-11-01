#include "Platform/HMutex.hpp"

#ifdef HELIX_PLATFORM_WINDOWS

#include "Core/Log.hpp"
#include <windows.h>

namespace hlx {
// TODO: Critical sections
bool HMutex::create() {

  internal_data = CreateMutex(0, 0, 0);
  if (!internal_data) {
    HERROR("Unable to create mutex.");
    return false;
  }
  return true;
}

void HMutex::destroy() {
  if (internal_data) {
    CloseHandle(internal_data);
    internal_data = 0;
  }
}

bool HMutex::lock() {
  if (!internal_data) {
    HERROR("HMutex invalid data is null");
    return false;
  }

  DWORD result = WaitForSingleObject(internal_data, INFINITE);
  switch (result) {
  // The thread got ownership of the mutex
  case WAIT_OBJECT_0:
    return true;

    // The thread got ownership of an abandoned mutex.
  case WAIT_ABANDONED:
    HERROR("Mutex lock failed.");
    return false;
  }
  return true;
}

bool HMutex::unlock() {
  if (!internal_data) {
    HERROR("HMutex invalid data is null");
    return false;
  }
  i32 result = ReleaseMutex(internal_data);

  return result != 0; // 0 is a failure
}

} // namespace hlx

#endif // HELIX_PLATFORM_WINDOWS
