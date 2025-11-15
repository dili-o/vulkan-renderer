#include "Platform/HThread.hpp"

#ifdef HELIX_PLATFORM_WINDOWS

#include "Core/Log.hpp"
#include "Platform/Platform.hpp"
#include <windows.h>

namespace hlx {
bool HThread::create(PFN_thread_start start_function_ptr, void *params,
                     bool auto_detach) {
  if (!start_function_ptr) {
    HERROR("start_function_ptr is null");
    return false;
  }

  internal_data =
      CreateThread(0,
                   0, // Default stack size
                   (LPTHREAD_START_ROUTINE)start_function_ptr, // function ptr
                   params, // param to pass to thread
                   0, (DWORD *)&thread_id);
  HDEBUG("Starting process on thread id: {:#x}", thread_id);
  if (!internal_data) {
    HERROR("Windows failed to create thread");
    return false;
  }
  if (auto_detach) {
    CloseHandle(internal_data);
  }
  return true;
}

void HThread::destroy() {
  if (internal_data) {
    DWORD exit_code;
    GetExitCodeThread(internal_data, &exit_code);
    CloseHandle((HANDLE)internal_data);
    internal_data = 0;
    thread_id = 0;
  }
}

void HThread::detach() {
  if (internal_data) {
    CloseHandle(internal_data);
    internal_data = 0;
  }
}

void HThread::cancel() {
  if (internal_data) {
    TerminateThread(internal_data, 0);
    internal_data = 0;
  }
}

void HThread::wait() {
  if (internal_data) {
    DWORD exit_code = WaitForSingleObject(internal_data, INFINITE);
    if (exit_code != WAIT_OBJECT_0)
      HERROR("Windows failed to wait for thread");
  }
  HERROR("Invalid thread internal_data");
}

void HThread::wait_timout(u64 wait_ms) {
  if (internal_data) {
    DWORD exit_code = WaitForSingleObject(internal_data, wait_ms);
    if (exit_code == WAIT_TIMEOUT) {
      HERROR("Wait timeout interval elapsed");
    } else if (exit_code != WAIT_OBJECT_0) {
      HERROR("Windows failed to wait for thread");
    }
  }
  HERROR("Invalid thread internal_data");
}

bool HThread::is_active() {
  if (internal_data) {
    DWORD exit_code = WaitForSingleObject(internal_data, 0);
    if (exit_code == WAIT_TIMEOUT) {
      return true;
    }
  }
  return false;
}

void HThread::sleep(u64 ms) { Platform::sleep(ms); }

u64 HThread::get_id() { return (u64)GetCurrentThreadId(); }
} // namespace hlx

#endif // HELIX_PLATFORM_WINDOWS
