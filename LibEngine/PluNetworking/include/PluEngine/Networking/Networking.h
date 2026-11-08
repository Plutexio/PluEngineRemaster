//
// Created by Plutex on 2026-11-08.
//

#ifndef PLUENGINE_NETWORKING_H
#define PLUENGINE_NETWORKING_H

#include "PluEngine/Core.h"

namespace Plu::Networking
{
	// Brings up the OS socket layer (WSAStartup on Windows). Called once from Application::EngineInit.
	PLUNETWORKING_API void Init();
	// Counterpart of Init(), called from Application::EngineShutdown.
	PLUNETWORKING_API void Shutdown();
}

#endif //PLUENGINE_NETWORKING_H
