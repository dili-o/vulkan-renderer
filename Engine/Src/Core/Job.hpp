#pragma once

#include "Containers/RingQueue.hpp"
#include "Core/Defines.hpp"
#include "Core/Service.hpp"
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

struct JobServiceConfiguration {
  Allocator *allocator{nullptr};
  u8 thread_count{0};
  JobType::Enum *type_masks;
};

struct HLX_API JobService : public Service {
  virtual void init(void *config = nullptr) override;
  virtual void shutdown() override;
  HELIX_DECLARE_SERVICE(JobService);

  void update();

  void submit(JobInfo info);

  bool running;
  u8 thread_count;
  JobThread job_threads[32];

  RingQueue<JobInfo> low_priority_queue{};
  RingQueue<JobInfo> medium_priority_queue{};
  RingQueue<JobInfo> high_priority_queue{};

  // Mutexes for each RingQueue since a job could be started from another job
  HMutex low_priority_mutex{};
  HMutex medium_priority_mutex{};
  HMutex high_priority_mutex{};

  JobResultEntry pending_results[MAX_JOB_RESULTS];
  HMutex result_mutex;

  Allocator *allocator;
};

} // namespace hlx
