#include "Engine.hpp"
#include "Core/Event.hpp"
#include "Core/FileWatcher.hpp"
#include "Core/Input.hpp"
#include "Core/Job.hpp"
#include "Core/Log.hpp"
#include "Core/Memory.hpp"
#include "Platform/File.hpp"
#include "Platform/Platform.hpp"
#include "Renderer/ImguiFrontend.hpp"
#include "Renderer/RendererFrontEnd.hpp"

namespace Helix {

struct EngineBackend {
  LogService log_service;
  FileService file_service;
  FileWatcherService file_watcher_service;
  InputService input_service;
  MemoryService memory_service;
  Platform platform_service;
  JobService job_service;
  EventService event_service;
  RendererFrontEnd renderer_frontend_service;
  ImguiFrontend imgui_frontend_service;
};

static EngineBackend s_backend;

void Engine::init() {
  s_backend.log_service.init();

  s_backend.file_service.init();

  s_backend.file_watcher_service.init();

  s_backend.input_service.init();

  MemoryServiceConfiguration mem_config{hmega(1000), hmega(1000)};
  s_backend.memory_service.init(&mem_config);

  s_backend.event_service.init();

  PlatformConfiguration platform_config{1280, 720, "Sandbox"};
  s_backend.platform_service.init((void *)&platform_config);

  JobServiceConfiguration job_config{};
  job_config.allocator = &s_backend.memory_service.system_allocator;
  job_config.thread_count = Platform::get_logical_processor_count() - 1;
  JobType::Enum job_thread_types[15];
  for (u32 i = 0; i < job_config.thread_count; ++i) {
    job_thread_types[i] = JobType::General;
  }

  if (job_config.thread_count == 1) {
    job_thread_types[0] =
        (JobType::Enum)(job_thread_types[0] | JobType::ResourceLoad |
                        JobType::GpuResource);
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
  s_backend.job_service.init(&job_config);

  RendererConfig renderer_config{};
  renderer_config.backend_type = RENDERER_BACKEND_TYPE_VULKAN;
  renderer_config.application_name = "Sandbox";
  renderer_config.platform = &s_backend.platform_service;
  renderer_config.max_frames_in_flight = 2;
  s_backend.renderer_frontend_service.init(&renderer_config);

  ImguiLayerConfiguration imgui_config{};
  imgui_config.type = RENDERER_BACKEND_TYPE_VULKAN;
  imgui_config.frontend = &s_backend.renderer_frontend_service;
  imgui_config.window_handle = s_backend.platform_service.platform_handle;
  imgui_config.max_frame_in_flight = renderer_config.max_frames_in_flight;
  s_backend.imgui_frontend_service.init(&imgui_config);
}

void Engine::shutdown() {
  HTRACE("Shutting Down Services...");
  s_backend.imgui_frontend_service.shutdown();
  s_backend.renderer_frontend_service.shutdown();
  s_backend.job_service.shutdown();
  s_backend.platform_service.shutdown();
  s_backend.event_service.shutdown();
  s_backend.memory_service.shutdown();
  s_backend.input_service.shutdown();
  s_backend.file_watcher_service.shutdown();
  s_backend.file_service.shutdown();
  s_backend.log_service.shutdown();
}

} // namespace Helix
