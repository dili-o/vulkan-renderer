
#pragma once

#include "Defines.hpp"

// Vendor
#include <memory>
#include <spdlog/fmt/ostr.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

namespace hlx {
struct HLX_API LogSys {
  static void init();
  static void shutdown();
  static std::shared_ptr<spdlog::logger> &GetCoreLogger();

  static void debug(...);

  // Core log macros
#ifdef _DEBUG
#define HDEBUG(...) hlx::LogSys::GetCoreLogger()->debug(__VA_ARGS__)
#else
#define HDEBUG(...)
#endif
#define HTRACE(...) hlx::LogSys::GetCoreLogger()->trace(__VA_ARGS__)
#define HINFO(...) hlx::LogSys::GetCoreLogger()->info(__VA_ARGS__)
#define HWARN(...) hlx::LogSys::GetCoreLogger()->warn(__VA_ARGS__)
#define HERROR(...) hlx::LogSys::GetCoreLogger()->error(__VA_ARGS__)
#define HCRITICAL(...)                                                         \
  hlx::LogSys::GetCoreLogger()->critical(__VA_ARGS__);                     \
  __debugbreak();
#define HCRITICAL_NO_BREAK(...)                                                \
  hlx::LogSys::GetCoreLogger()->critical(__VA_ARGS__)

#define HELIX_SERVICE_INIT_MSG(Type) HINFO(#Type " Initialised")
#define HELIX_SERVICE_SHUTDOWN_MSG(Type) HINFO(#Type " Shutdown")
#define HELIX_SERVICE_RECREATE_MSG(Type) HERROR(#Type " already created!")
};
} // namespace hlx
