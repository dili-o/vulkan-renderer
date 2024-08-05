#include "Log.hpp"

namespace Helix{
	std::shared_ptr<spdlog::logger> s_CoreLogger;

	LogService* s_log_service = nullptr;

	LogService* LogService::instance(){
		return s_log_service;
	}

	void LogService::init(void* configuration){
		s_log_service = this;
		spdlog::set_pattern("%^[%T] %n [%l]: %v%$");
		s_CoreLogger = spdlog::stdout_color_mt("Helix Engine");
		s_CoreLogger->set_level(spdlog::level::trace);
		HINFO("Logger Initialised");
	}

	inline std::shared_ptr<spdlog::logger>& LogService::GetCoreLogger() {
		return s_CoreLogger;
	}

	void ReportAssertionFailure(cstring expression, cstring message, cstring file, i32 line) {
		HCRITICAL("Assertion Failure: {}, message: {}, in file: {}, line: {}", expression, message, file, line);
	}
}
