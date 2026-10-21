//
// Created by Plutex on 12/30/25.
//

#include "PluBase/Log.h"
#include <spdlog/sinks/stdout_color_sinks.h>

namespace Plu {

	std::shared_ptr<spdlog::logger> Log::s_CoreLogger;
	std::shared_ptr<spdlog::logger> Log::s_ClientLogger;

	void Log::Init(const char* coreLoggerName, const char* clientLoggerName) {
		// Format: [Time] [Thread] LoggerName: Message
		spdlog::set_pattern("%^[%T] [%t] %n: %v%$");
		//spdlog::set_pattern("[%H:%M:%S %z] [%n] [%^---%L---%$] [thread %t] %v");

		s_CoreLogger = spdlog::stdout_color_mt(coreLoggerName);
		s_CoreLogger->set_level(spdlog::level::trace);
		PLU_CORE_INFO("{0} logger initialized", coreLoggerName);

		s_ClientLogger = spdlog::stdout_color_mt(clientLoggerName);
		s_ClientLogger->set_level(spdlog::level::trace);
		PLU_CORE_INFO("{0} logger initialized", clientLoggerName);
	}
}
