#include "Core/Job.hpp"

#include "Containers/RingQueue.hpp"
#include "Core/Assert.hpp"
#include "Core/Log.hpp"
#include "Core/Memory.hpp"
#include "Platform/HMutex.hpp"
#include "Platform/Platform.hpp"
#include <cstring>
#include <tracy/Tracy.hpp>
namespace Helix {

void job_type_to_string(JobType::Enum type, char *buffer, u32 buffer_size) {
  buffer[0] = '\0';

  bool first = true;

  if (type & JobType::General) {
    std::strncat(buffer, "General", buffer_size - std::strlen(buffer) - 1);
    first = false;
  }

  if (type & JobType::ResourceLoad) {
    if (!first)
      std::strncat(buffer, " | ", buffer_size - std::strlen(buffer) - 1);
    std::strncat(buffer, "ResourceLoad", buffer_size - std::strlen(buffer) - 1);
    first = false;
  }
  if (type & JobType::GpuResource) {
    if (!first)
      std::strncat(buffer, " | ", buffer_size - std::strlen(buffer) - 1);
    std::strncat(buffer, "GpuResource", buffer_size - std::strlen(buffer) - 1);
  }
}

static JobService *s_job_service{nullptr};

void store_result(PFN_job_on_complete callback, void *result_data,
                  u32 result_data_size) {
  // Create the new entry
  JobResultEntry entry{};
  entry.id = UINT16_MAX;
  entry.param_size = result_data_size;
  entry.callback = callback;

  if (result_data_size > 0) {
    entry.params = halloca(result_data_size, s_job_service->allocator);
    memcpy(entry.params, result_data, result_data_size);
  } else {
    entry.params = nullptr;
  }

  HASSERT(s_job_service->result_mutex.lock());
  for (u16 i = 0; i < MAX_JOB_RESULTS; ++i) {
    if (s_job_service->pending_results[i].id == UINT16_MAX) {
      s_job_service->pending_results[i] = entry;
      s_job_service->pending_results[i].id = i;
      break;
    }
  }
  HASSERT(s_job_service->result_mutex.unlock());
}

u32 job_thread_run(void *params) {
  u8 index = *(u8 *)params;
  JobThread *job_thread = &s_job_service->job_threads[index];
  u64 thread_id = job_thread->thread.get_id();
  char buffer[50];
  job_type_to_string(job_thread->job_type, buffer, 50);
  HTRACE("Starting job thread #{} (id= {:#x}, type= {}, affinity= {}).",
         job_thread->index, thread_id, buffer,
         Platform::get_current_processor_id());

  if (!job_thread->info_mutex.create()) {
    HERROR("Failed to create job thread mutex! Aborting thread.");
    return 0;
  }

  while (true) {
    if (!s_job_service || !s_job_service->running || !job_thread)
      break;

    HASSERT(job_thread->info_mutex.lock());

    // Copy job info
    JobInfo info = job_thread->info;

    HASSERT(job_thread->info_mutex.unlock());

    if (info.entry_point) {
      bool result = info.entry_point(info.param_data, info.result_data);

      if (result && info.on_success) {
        store_result(info.on_success, info.result_data, info.result_data_size);
      } else if (!result && info.on_fail) {
        store_result(info.on_fail, info.result_data, info.result_data_size);
      }

      if (info.param_data)
        s_job_service->allocator->deallocate(info.param_data);
      if (info.result_data)
        s_job_service->allocator->deallocate(info.result_data);

      // Lock and reset the thread's info object
      HASSERT(job_thread->info_mutex.lock());

      std::memset(&job_thread->info, 0, sizeof(JobInfo));

      HASSERT(job_thread->info_mutex.unlock());
    }

    if (s_job_service->running) {
      job_thread->thread.sleep(10);
    } else {
      break;
    }
  }

  job_thread->info_mutex.destroy();
  return 1;
}

JobService *JobService ::instance() { return s_job_service; }

void JobService::init(void *config_) {
  if (s_job_service) {
    HELIX_SERVICE_RECREATE_MSG(JobService);
    return;
  }

  s_job_service = this;

  JobServiceConfiguration *config = (JobServiceConfiguration *)config_;

  allocator = config->allocator;

  low_priority_queue.init(config->allocator, 128);
  medium_priority_queue.init(config->allocator, 512);
  high_priority_queue.init(config->allocator, 128);

  thread_count = config->thread_count;

  // Invalidate all result slots
  for (u16 i = 0; i < MAX_JOB_RESULTS; ++i) {
    pending_results[i].id = UINT16_MAX;
  }

  HDEBUG("Main thread id is: {:#x}",
         Platform::instance()->get_current_thread_id());
  HDEBUG("Spawning {} job threads.", thread_count);

  for (u8 i = 0; i < thread_count; ++i) {
    JobThread &job_thread = job_threads[i];
    job_thread.index = i;
    job_thread.job_type = config->type_masks[i];

    HASSERT_MSG(
        job_thread.thread.create(job_thread_run, &job_thread.index, false),
        "Failed to create job thread");
    std::memset(&job_thread.info, 0, sizeof(JobInfo));
  }

  // Mutexes
  HASSERT(result_mutex.create());
  HASSERT(low_priority_mutex.create());
  HASSERT(medium_priority_mutex.create());
  HASSERT(high_priority_mutex.create());

  running = true;

  HELIX_SERVICE_INIT_MSG(JobService);
}

void JobService::shutdown() {
  // TODO: For every service check if the static pointer is valid

  running = false;
  for (u8 i = 0; i < thread_count; ++i) {
    job_threads[i].thread.destroy();
  }
  low_priority_queue.shutdown();
  medium_priority_queue.shutdown();
  high_priority_queue.shutdown();

  result_mutex.destroy();
  low_priority_mutex.destroy();
  medium_priority_mutex.destroy();
  high_priority_mutex.destroy();

  s_job_service = nullptr;
  HELIX_SERVICE_SHUTDOWN_MSG(JobService);
}

void process_queue(RingQueue<JobInfo> *queue, HMutex *queue_mutex) {
  u8 thread_count = s_job_service->thread_count;

  while (queue->size > 0) {
    JobInfo info;
    if (!queue->peek_head(&info)) {
      break;
    }

    bool thread_found = false;
    for (u8 i = 0; i < thread_count; ++i) {
      JobThread *job_thread = &s_job_service->job_threads[i];
      // Skip threads that don't match the job type
      if ((job_thread->job_type & info.job_type) == 0) {
        continue;
      }

      HASSERT(job_thread->info_mutex.lock());

      if (!job_thread->info.entry_point) {
        // Remove entry from queue
        HASSERT(queue_mutex->lock());
        queue->dequeue(&info);
        HASSERT(queue_mutex->unlock());

        job_thread->info = info;
        HTRACE("Assigning job to thread: {}", job_thread->index);
        thread_found = true;
      }

      HASSERT(job_thread->info_mutex.unlock());

      if (thread_found)
        break;
    }

    // Failed to find a thread, all threads are busy
    if (!thread_found)
      break;
  }
}

void JobService::update() {
  ZoneScoped;
  if (!s_job_service || !running) {
    return;
  }

  process_queue(&high_priority_queue, &high_priority_mutex);
  process_queue(&medium_priority_queue, &medium_priority_mutex);
  process_queue(&low_priority_queue, &low_priority_mutex);

  // Process pending results
  {
    ZoneScopedN("Process Job Results");
    for (u16 i = 0; i < MAX_JOB_RESULTS; ++i) {
      HASSERT(result_mutex.lock());
      JobResultEntry entry = pending_results[i];
      HASSERT(result_mutex.unlock());

      if (entry.id != UINT16_MAX) {
        entry.callback(entry.params);

        if (entry.params) {
          allocator->deallocate(entry.params);
        }

        HASSERT(result_mutex.lock());
        memset(&pending_results[i], 0, sizeof(JobResultEntry));
        pending_results[i].id = UINT16_MAX;
        HASSERT(result_mutex.unlock());
      }
    }
  }
}

void JobService::submit(JobInfo info) {
  RingQueue<JobInfo> *queue = &medium_priority_queue;
  HMutex *queue_mutex = &medium_priority_mutex;

  // If the job is high priority, try to kick it off immediately.
  if (info.job_priority == JobPriority::High) {
    queue = &high_priority_queue;
    queue_mutex = &high_priority_mutex;

    // Check for a free thread that supports the job type first.
    for (u8 i = 0; i < thread_count; ++i) {
      JobThread *job_thread = &job_threads[i];
      if (job_thread->job_type & info.job_type) {
        bool found = false;
        HASSERT(job_thread->info_mutex.lock());
        if (!job_thread->info.entry_point) {
          HTRACE("Job immediately submitted on thread {}", job_thread->index);
          job_thread->info = info;
          found = true;
        }
        HASSERT(job_thread->info_mutex.unlock());
        if (found) {
          return;
        }
      }
    }
  }

  // If this point is reached, all threads are busy (if high) or it can wait a
  // frame. Add to the queue and try again next cycle.
  if (info.job_priority == JobPriority::Low) {
    queue = &low_priority_queue;
    queue_mutex = &low_priority_mutex;
  }

  // NOTE: Locking here in case the job is submitted from another job/thread.
  HASSERT(queue_mutex->lock());
  queue->enqueue(info);
  HASSERT(queue_mutex->unlock());
  HTRACE("Job queued.");
}

JobInfo create_job_info(PFN_job_on_start entry_point,
                        PFN_job_on_complete on_success,
                        PFN_job_on_complete on_fail, void *param_data,
                        u32 param_data_size, u32 result_data_size,
                        JobType::Enum job_type,
                        JobPriority::Enum job_priority) {
  JobInfo job;
  job.entry_point = entry_point;
  job.on_success = on_success;
  job.on_fail = on_fail;
  job.job_type = job_type;
  job.job_priority = job_priority;

  job.param_data_size = param_data_size;
  if (param_data_size) {
    job.param_data = halloca(param_data_size, s_job_service->allocator);
    memcpy(job.param_data, param_data, param_data_size);
  } else {
    job.param_data = 0;
  }

  job.result_data_size = result_data_size;
  if (result_data_size) {
    job.result_data = halloca(result_data_size, s_job_service->allocator);
    memset(job.result_data, 0, result_data_size);
  } else {
    job.result_data = 0;
  }

  return job;
}

} // namespace Helix
