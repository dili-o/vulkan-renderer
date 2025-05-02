#include "Engine.hpp"
#include "Platform/Platform.hpp"
#include "Platform/Process.hpp"
#include "Renderer/RendererTypes.hpp"
#include <tracy/Tracy.hpp>

namespace Helix {

// #define LAUNCH_TRACY_PROFILER

void Engine::init(Game *game) {
  // Initialize services
  log_service.init();

  file_service.init();

  input_service.init();

  MemoryServiceConfiguration mem_config{hmega(500), hmega(300)};
  memory_service.init(&mem_config);

  event_service.init();

  PlatformConfiguration platform_config{1280, 720, "Sandbox"};
  platform_service.init((void *)&platform_config);

  JobServiceConfiguration job_config{};
  job_config.allocator = &memory_service.system_allocator;
  job_config.thread_count = Platform::get_logical_processor_count();
  JobType::Enum job_thread_types[15];
  for (u32 i = 0; i < job_config.thread_count; ++i) {
    job_thread_types[i] = JobType::General;
  }

  if (job_config.thread_count == 1) {
    job_thread_types[0] = (JobType::Enum)(
        job_thread_types[0] | JobType::ResourceLoad | JobType::GpuResource);
  } else if (job_config.thread_count == 2) {

    job_thread_types[0] =
        (JobType::Enum)(job_thread_types[0] | JobType::GpuResource);
    job_thread_types[1] =
        (JobType::Enum)(job_thread_types[1] | JobType::ResourceLoad);
  } else {

    job_thread_types[0] = JobType::GpuResource;
    job_thread_types[1] = JobType::ResourceLoad;
  }

  job_config.type_masks = job_thread_types;
  job_service.init(&job_config);

  RendererConfig renderer_config{};
  renderer_config.backend_type = RENDERER_BACKEND_TYPE_VULKAN;
  renderer_config.application_name = "Sandbox";
  renderer_config.platform = &platform_service;
  renderer_frontend_service.init(&renderer_config);

  ImguiLayerConfiguration imgui_config{};
  imgui_config.type = RENDERER_BACKEND_TYPE_VULKAN;
  imgui_config.frontend = &renderer_frontend_service;
  imgui_config.window_handle = platform_service.platform_handle;
  imgui_frontend_service.init(&imgui_config);

  application_service.init(game);

#ifdef LAUNCH_TRACY_PROFILER
  FrameMark;
  // Begin Tracy Profiler
  if (!launch_tracy_profiler()) {
    HERROR("Unable to start Tracy Profiler");
  }
#endif // LAUNCH_TRACY_PROFILER

  application_service.run();
}

void Engine::shutdown() {
  HTRACE("Shutting Down Services...");
  application_service.shutdown();
  imgui_frontend_service.shutdown();
  renderer_frontend_service.shutdown();
  job_service.shutdown();
  platform_service.shutdown();
  event_service.shutdown();
  memory_service.shutdown();
  input_service.shutdown();
  file_service.shutdown();
  log_service.shutdown();
}

} // namespace Helix
