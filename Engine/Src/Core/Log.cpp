
#include "Log.hpp"
#include "Defines.hpp"

namespace Helix {
std::shared_ptr<spdlog::logger> s_CoreLogger;

static LogService *s_log_service{nullptr};

LogService *LogService::instance() { return s_log_service; }

void LogService::init(void *configuration) {
  if (s_log_service) {
    HELIX_SERVICE_RECREATE_MSG(LogService);
    return;
  }
  s_log_service = this;
  spdlog::set_pattern("%^[%T] %n [%l]: %v%$");
  s_CoreLogger = spdlog::stdout_color_mt("Helix Engine");
  s_CoreLogger->set_level(spdlog::level::trace);
  HELIX_SERVICE_INIT_MSG(LogService);
}

void LogService::shutdown() {
  s_log_service = nullptr;
  HELIX_SERVICE_SHUTDOWN_MSG(LogService);
}

inline std::shared_ptr<spdlog::logger> &LogService::GetCoreLogger() {
  return s_CoreLogger;
}

void HLX_API ReportAssertionFailure(cstring expression, cstring message, cstring file,
                            i32 line) {
  HCRITICAL("Assertion Failure: {}, message: {}, in file: {}, line: {}",
            expression, message, file, line);
}
} // namespace Helix
