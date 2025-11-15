
#include "Log.hpp"
#include "Defines.hpp"

namespace hlx {
std::shared_ptr<spdlog::logger> s_CoreLogger;
static bool is_initialized{false};

void LogSys::init() {
  if (is_initialized) {
    HELIX_SERVICE_RECREATE_MSG(LogSys);
    return;
  }

  spdlog::set_pattern("%^[%T] %n [%l]: %v%$");
  s_CoreLogger = spdlog::stdout_color_mt("Helix Engine");
  s_CoreLogger->set_level(spdlog::level::trace);
  is_initialized = true;
  HELIX_SERVICE_INIT_MSG(LogSys);
}

void LogSys::shutdown() {
  if(!is_initialized) {
    return;
  }
  
  is_initialized = false;
  HELIX_SERVICE_SHUTDOWN_MSG(LogSys);
}

inline std::shared_ptr<spdlog::logger> &LogSys::GetCoreLogger() {
  return s_CoreLogger;
}

void HLX_API ReportAssertionFailure(cstring expression, cstring message, cstring file,
                            i32 line) {
  HCRITICAL("Assertion Failure: {}, message: {}, in file: {}, line: {}",
            expression, message, file, line);
}
} // namespace hlx
