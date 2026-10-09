//
// Created by Plutex on 10/9/26.
//

#include "PluEngine/Render/RenderParticleCommands.h"

#include "HashMap/HashMap.h"

#include <mutex>

namespace
{
	// One mutex for the queue AND the sent-revision map, so "attach the program or not" is decided in push
	// order: the render thread then always sees a program before the first command that relies on it.
	std::mutex gCommandsMutex;
	Plu::Queue<Plu::ParticleCommand> gCommands;
	// System uuid -> the revision whose program was last attached. Never pruned: the render-side cache is
	// never pruned either, and both live as long as the process.
	Plu::HashMap<UInt64, UInt32> gSentProgramRevisions;
}

void Plu::PushParticleSpawnCommand(ParticleCommand&& command, const CompiledParticleSystem& program)
{
	command.Type = EParticleCommandType::Spawn;
	command.SystemUuid = program.SystemUuid.getUUID();

	std::lock_guard<std::mutex> lock(gCommandsMutex);
	const UInt32* sent = gSentProgramRevisions.Find(command.SystemUuid);
	if (!sent || *sent != program.Revision) {
		command.HasProgram = true;
		command.Program = program;
		gSentProgramRevisions.InsertOrAssign(command.SystemUuid, program.Revision);
	}
	gCommands.PushBack(static_cast<ParticleCommand&&>(command));
}

void Plu::PushParticleCommand(ParticleCommand&& command)
{
	std::lock_guard<std::mutex> lock(gCommandsMutex);
	gCommands.PushBack(static_cast<ParticleCommand&&>(command));
}

void Plu::DrainParticleCommands(Queue<ParticleCommand>& out)
{
	out.Clear();
	std::lock_guard<std::mutex> lock(gCommandsMutex);
	out.Swap(gCommands);
}
