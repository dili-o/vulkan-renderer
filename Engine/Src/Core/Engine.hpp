#pragma once

#include "Core/Application.hpp"
#include "Core/Event.hpp"
#include "Core/Input.hpp"
#include "Core/Job.hpp"
#include "Core/Log.hpp"
#include "Core/Memory.hpp"
#include "Platform/File.hpp"
#include "Platform/Platform.hpp"
#include "Renderer/ImguiFrontend.hpp"
#include "Renderer/RendererFrontEnd.hpp"
namespace Helix {

struct Engine {
  void init(Game *game);
  void shutdown();

  LogService log_service;
  FileService file_service;
  InputService input_service;
  MemoryService memory_service;
  Platform platform_service;
  JobService job_service;
  Application application_service;
  EventService event_service;
  RendererFrontEnd renderer_frontend_service;
  ImguiFrontend imgui_frontend_service;
};
} // namespace Helix
