#pragma once

#include "Core/Defines.hpp"

namespace hlx {

// A function pointer to be invoked when a thread starts
typedef u32 (*PFN_thread_start)(void *);

struct HThread {
public:
  bool create(PFN_thread_start start_function_ptr, void *params,
              bool auto_detach);

  void destroy();

  // Detaches the thread, automatically releasing the resources upon thread
  // completion
  void detach();

  // Cancels work on the thread
  void cancel();

  bool is_active();

  void wait();

  void wait_timout(u64 ms);

  void sleep(u64 ms);

  u64 get_id();

private:
  void *internal_data{nullptr};
  u64 thread_id{UINT64_MAX};
};

} // namespace hlx
