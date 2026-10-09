//
// Created by Plutex on 10/9/26.
//

#ifndef PLUENGINE_RENDERPARTICLECOMMANDS_H
#define PLUENGINE_RENDERPARTICLECOMMANDS_H

#include "PluEngine/Core.h"
#include "PluEngine/PluTypes.h"
#include "PluEngine/Core/Objects/EngineObjectHandle.h"
#include "PluEngine/Effects/Particles/CompiledParticleSystem.h"
#include "Queue/Queue.h"

namespace Plu
{
	enum class EParticleCommandType : UInt8
	{
		// Create a detached (fire-and-forget) system in SceneHandle's world.
		Spawn,
		// Soft stop: no new particles, live ones finish, then the system is destroyed.
		Deactivate,
		// Hard stop: the system and its particles are destroyed now.
		Destroy,
		// The world is being unloaded: destroy every detached system it still has on the render thread.
		ReleaseWorld
	};

	// Main -> render one-shot particle commands for systems no component describes — the ones spawned by
	// SceneWorld::SpawnParticleSystem. Unlike the snapshot's spawner state this is a FIFO queue drained
	// exactly once by the render thread, so a command is never dropped nor replayed (MULTITHREADING.md).
	struct ParticleCommand
	{
		EParticleCommandType Type = EParticleCommandType::Spawn;
		EngineObjectHandle SceneHandle;
		// Spawn / Deactivate / Destroy: the id SpawnParticleSystem returned.
		UInt64 EffectId = 0;

		// Spawn only.
		UInt64 SystemUuid = 0;
		Vec3 Location = Vec3(0.0f);
		Quaternion Rotation = Quaternion(1.0f, 0.0f, 0.0f, 0.0f);
		// Seconds of emission before the system deactivates itself (soft, like Deactivate); 0 = no limit.
		float Lifetime = 0.0f;
		// Set by PushParticleSpawnCommand: a copy of the compiled program, attached only when the render
		// thread has not been sent this system at this revision yet. Otherwise the render-side cache has it.
		bool HasProgram = false;
		CompiledParticleSystem Program;
	};

	// Main thread. Queues a Spawn command and attaches `program` when this revision of the system has not
	// been sent before (the render thread caches programs by system uuid).
	PLURENDER_API void PushParticleSpawnCommand(ParticleCommand&& command, const CompiledParticleSystem& program);
	// Any thread. Queues a Deactivate / Destroy / ReleaseWorld command.
	PLURENDER_API void PushParticleCommand(ParticleCommand&& command);
	// Render thread. Moves every queued command into `out`, in push order.
	PLURENDER_API void DrainParticleCommands(Queue<ParticleCommand>& out);
}

#endif //PLUENGINE_RENDERPARTICLECOMMANDS_H
