
#pragma once

#include "Defines.hpp"
#include "Service.hpp"

// Vendor
#include <memory>
#include <spdlog/fmt/ostr.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

namespace Helix {
struct HLX_API LogService : public Service {

  static LogService *instance();

  virtual void init(void *configuration = nullptr) override;
  virtual void shutdown() override;
  static std::shared_ptr<spdlog::logger> &GetCoreLogger();

  // Core log macros
#ifdef _DEBUG
#define HDEBUG(...) LogService::GetCoreLogger()->debug(__VA_ARGS__)
#else
#define HDEBUG(...)
#endif
#define HTRACE(...) LogService::GetCoreLogger()->trace(__VA_ARGS__)
#define HINFO(...) LogService::GetCoreLogger()->info(__VA_ARGS__)
#define HWARN(...) LogService::GetCoreLogger()->warn(__VA_ARGS__)
#define HERROR(...) LogService::GetCoreLogger()->error(__VA_ARGS__)
#define HCRITICAL(...)                                                         \
  LogService::GetCoreLogger()->critical(__VA_ARGS__);                          \
  __debugbreak();
#define HCRITICAL_NO_BREAK(...)                                                \
  LogService::GetCoreLogger()->critical(__VA_ARGS__)

#define HELIX_SERVICE_INIT_MSG(Type) HINFO(#Type " Initialised")
#define HELIX_SERVICE_SHUTDOWN_MSG(Type) HINFO(#Type " Shutdown")
#define HELIX_SERVICE_RECREATE_MSG(Type) HERROR(#Type " already created!")
};
} // namespace Helix
