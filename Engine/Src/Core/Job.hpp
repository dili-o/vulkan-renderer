#pragma once

#include "Core/Defines.hpp"
#include "Platform/HMutex.hpp"
#include "Platform/HThread.hpp"

namespace hlx {

//                          Entry Data   Result Data
typedef bool (*PFN_job_on_start)(void *, void *);
typedef bool (*PFN_job_on_complete)(void *);

namespace JobType {
enum Enum { General = 1 << 0, ResourceLoad = 1 << 1, GpuResource = 1 << 2 };
}

void job_type_to_string(JobType::Enum type, char *buffer, u32 buffer_size);

namespace JobPriority {
enum Enum { Low, Medium, High };
}

struct JobInfo {
  JobType::Enum job_type{JobType::General};
  JobPriority::Enum job_priority{JobPriority::Medium};
  PFN_job_on_start entry_point{nullptr};
  PFN_job_on_complete on_success{nullptr};
  PFN_job_on_complete on_fail{nullptr};

  // Data to be passed to the entry_point
  void *param_data{nullptr};
  u32 param_data_size{0};

  // Data to be passed to on_success/on_fail
  void *result_data{nullptr};
  u32 result_data_size{0};
};

JobInfo create_job_info(PFN_job_on_start entry_point,
                        PFN_job_on_complete on_success,
                        PFN_job_on_complete on_fail, void *param_data,
                        u32 param_data_size, u32 result_data_size,
                        JobType::Enum job_type = JobType::General,
                        JobPriority::Enum job_priority = JobPriority::Medium);

struct JobThread {
  u8 index;
  HThread thread;
  JobInfo info;
  HMutex info_mutex;

  JobType::Enum job_type;
};

struct JobResultEntry {
  u16 id;
  PFN_job_on_complete callback;
  u32 param_size;
  void *params;
};

#define MAX_JOB_RESULTS 512

struct Allocator;
struct JobSysConfiguration {
  Allocator *allocator{nullptr};
  u8 thread_count{0};
  JobType::Enum *type_masks;
};

struct HLX_API JobSys {
  static void init(const JobSysConfiguration &config);
  static void shutdown();

  static void update();

  static void submit(JobInfo info);
};

} // namespace hlx
