#include "Engine.hpp"
#include "Platform/Platform.hpp"
#include "Renderer/RendererTypes.hpp"

namespace Helix {
void Engine::init(Game *game) {
  // Initialize services
  log_service.init();

  file_service.init();

  input_service.init();

  MemoryServiceConfiguration mem_config{hmega(15), hmega(15)};
  memory_service.init(&mem_config);

  event_service.init();

  PlatformConfiguration platform_config{1280, 720, "Sandbox"};
  platform_service.init((void *)&platform_config);

  RendererConfig renderer_config{};
  renderer_config.backend_type = RENDERER_BACKEND_TYPE_VULKAN;
  renderer_config.application_name = "Sandbox";
  renderer_config.platform = &platform_service;
  renderer_frontend_service.init(&renderer_config);

  application_service.init(game);

  application_service.run();
}

void Engine::shutdown() {
  HTRACE("Shutting Down Services...");
  application_service.shutdown();
  renderer_frontend_service.shutdown();
  platform_service.shutdown();
  event_service.shutdown();
  memory_service.shutdown();
  input_service.shutdown();
  file_service.shutdown();
  log_service.shutdown();
}

} // namespace Helix
