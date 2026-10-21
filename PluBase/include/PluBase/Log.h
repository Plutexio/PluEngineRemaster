//
// Created by Plutex on 12/30/25.
//

#ifndef PLUBASE_LOG_H
#define PLUBASE_LOG_H

#include "PluBase/Base.h"
#include <spdlog/spdlog.h>
#include <spdlog/fmt/ostr.h>

namespace Plu {

	class PLUBASE_API Log {
	public:
		// Two loggers: the core one for library/engine code (PLU_CORE_*), the client one
		// for the application on top (PLU_*). Call once, before the first log line.
		static void Init(const char* coreLoggerName = "ENGINE", const char* clientLoggerName = "APP");

		static std::shared_ptr<spdlog::logger>& GetCoreLogger() { return s_CoreLogger; }
		static std::shared_ptr<spdlog::logger>& GetClientLogger() { return s_ClientLogger; }
	private:
		static std::shared_ptr<spdlog::logger> s_CoreLogger;
		static std::shared_ptr<spdlog::logger> s_ClientLogger;
	};

}

#define PLU_CORE_TRACE(...)    ::Plu::Log::GetCoreLogger()->trace(__VA_ARGS__)
#define PLU_CORE_INFO(...)     ::Plu::Log::GetCoreLogger()->info(__VA_ARGS__)
#define PLU_CORE_WARN(...)     ::Plu::Log::GetCoreLogger()->warn(__VA_ARGS__)
#define PLU_CORE_ERROR(...)    ::Plu::Log::GetCoreLogger()->error(__VA_ARGS__)
#define PLU_CORE_CRITICAL(...) ::Plu::Log::GetCoreLogger()->critical(__VA_ARGS__)

#define PLU_TRACE(...)         ::Plu::Log::GetClientLogger()->trace(__VA_ARGS__)
#define PLU_INFO(...)          ::Plu::Log::GetClientLogger()->info(__VA_ARGS__)
#define PLU_WARN(...)          ::Plu::Log::GetClientLogger()->warn(__VA_ARGS__)
#define PLU_ERROR(...)         ::Plu::Log::GetClientLogger()->error(__VA_ARGS__)
#define PLU_CRITICAL(...)      ::Plu::Log::GetClientLogger()->critical(__VA_ARGS__)

#endif //PLUBASE_LOG_H