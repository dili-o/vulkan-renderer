#pragma once

#include "Platform.hpp"
#include "Service.hpp"

#include <memory>
#include <spdlog/spdlog.h>
#include <spdlog/fmt/ostr.h>
#include <spdlog/sinks/stdout_color_sinks.h>


namespace Helix {
    struct LogService : public Service {

        static LogService*              instance();

        void                            init(void* configuration = nullptr) override;
        static std::shared_ptr<spdlog::logger>& GetCoreLogger();

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
#define HCRITICAL(...) LogService::GetCoreLogger()->critical(__VA_ARGS__); __debugbreak();
    };
}


