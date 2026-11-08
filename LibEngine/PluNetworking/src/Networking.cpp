//
// Created by Plutex on 2026-11-08.
//

#include "PluEngine/Networking/Networking.h"

#include <PluNetwork/Network.h>

namespace Plu::Networking
{
	void Init()
	{
		PLU_PROFILE_SCOPE("Networking::Init");
		PluNetwork::InitializeNetwork();
	}

	void Shutdown()
	{
		PluNetwork::ShutdownNetwork();
	}
}
