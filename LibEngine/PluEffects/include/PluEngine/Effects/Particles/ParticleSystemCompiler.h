//
// Created by Plutex on 9/26/26.
//

#ifndef PLUENGINE_PARTICLESYSTEMCOMPILER_H
#define PLUENGINE_PARTICLESYSTEMCOMPILER_H

#include "PluEngine/Core.h"
#include "PluEngine/PluTypes.h"
#include "PluEngine/Effects/Particles/CompiledParticleSystem.h"
#include "PluEngine/Effects/Particles/ParticleParam.h"

namespace Plu
{
	struct ParticleSystem;
	struct ParticleEmitter;
	struct GraphNode;
	struct ParticleModuleNode;
	struct Curve;
	struct ColorGradient;

	// What a module's Compile() writes into. One context per emitter compile.
	//
	// Module Compile() implementations run in chain order and append ops to the spawn or update list. The
	// compiler adds the fixed prologue/epilogue around them (defaults, age advance, position integration,
	// base-value snapshots) once it knows which columns the chain needs.
	struct PLUEFFECTS_API ParticleCompileContext
	{
		ParticleSystem& System;
		ParticleEmitter& Emitter;
		CompiledParticleSystem& Program; // ParameterLayout is already filled
		CompiledEmitter& Out;

		ParticleCompileContext(ParticleSystem& system, ParticleEmitter& emitter,
		                       CompiledParticleSystem& program, CompiledEmitter& out)
			: System(system), Emitter(emitter), Program(program), Out(out) {}

		// ---- emitting ---------------------------------------------------------------------------
		void EmitSpawn(const ParticleOp& op) { SpawnBody.PushBack(op); }
		void EmitUpdate(const ParticleOp& op) { UpdateBody.PushBack(op); }
		// Marks a column as needed (allocates it in the store).
		void Require(EParticleColumn column) { Out.UsedAttributes |= ParticleColumnBit(column); }
		// Emits a ComputeSpeed into the given list (Speed changes as velocity does, so consumers ask
		// right before they read it).
		void EnsureSpeedComputed(bool inSpawnList);

		static ParticleOp MakeOp(EParticleOpCode code, EParticleColumn dst,
		                         const ParticleOperand& a = ParticleOperand(), const ParticleOperand& b = ParticleOperand());

		// ---- operands ---------------------------------------------------------------------------
		ParticleOperand Constant(float value);
		ParticleOperand AttributeOperand(EParticleColumn column);
		ParticleOperand Random(float min, float max);
		// Curve sampled at `source`; the curve's [0,1] time spans source values [inMin, inMax].
		ParticleOperand BakeCurve(const Curve& curve, EParticleColumn source, float inMin, float inMax);
		void BakeGradient(const ColorGradient& gradient, EParticleColumn source, float inMin, float inMax,
		                  ParticleOperand out[4]);
		// Appends raw floats to the constant pool and returns their operand (Kind Constant, Index = first).
		ParticleOperand AddConstants(const float* values, UInt32 count);

		// ---- resolving a module property ---------------------------------------------------------
		// Order of precedence for every property: a wired data pin (folded to a constant, or a direct
		// parameter/attribute wire), otherwise the authored value.
		ParticleOperand ResolveFloat(ParticleModuleNode& node, const char* pin, const ParticleParamFloat& param);
		void ResolveVec3(ParticleModuleNode& node, const char* pin, const ParticleParamVec3& param, ParticleOperand out[3]);
		void ResolveColor(ParticleModuleNode& node, const char* pin, const ParticleParamColor& param, ParticleOperand out[4]);
		// Plain float / Vec3 properties that carry an auto-generated data pin (BuildDataPinsFromReflection).
		ParticleOperand ResolveFloatProperty(ParticleModuleNode& node, const char* pin, float authored);
		void ResolveVec3Property(ParticleModuleNode& node, const char* pin, const Vec3& authored, ParticleOperand out[3]);
		// Values the program stores as plain numbers (rates, thresholds): only a folded constant is allowed;
		// a parameter/attribute wire warns and falls back to the authored value.
		float ResolveConstantFloat(ParticleModuleNode& node, const char* pin, float authored);
		int ResolveConstantInt(ParticleModuleNode& node, const char* pin, int authored);

		// Column an authoring-level attribute reads from (Position/Velocity return the X column).
		EParticleColumn ColumnFor(EParticleAttribute attribute);

		// ---- compile-time state shared between modules ---------------------------------------------
		DynamicArray<ParticleOp> SpawnBody;
		DynamicArray<ParticleOp> UpdateBody;
		bool SizeWrittenThisTick = false; // SizeOverLife already wrote Size: SizeBySpeed multiplies on top
		bool SpriteRendererSeen = false;
		bool RibbonRendererSeen = false;
		bool BurstSeen = false;

	private:
		struct PinResolution;
		PinResolution ResolvePin(ParticleModuleNode& node, const char* pin, const char* typeId);
		bool ReachesRuntimeSource(ParticleModuleNode& node, GraphNode* from, DynamicArray<UInt64>& visited);
		ParticleOperand ParameterOperand(UInt32 floatOffset);
	};

	// Compiles a ParticleSystem asset into a CompiledParticleSystem. MAIN THREAD ONLY: it walks the
	// authored graphs and calls the graph evaluator (EvaluateDataOutput) to fold constant subtrees —
	// exactly once per parameter, here, never from the executor.
	class PLUEFFECTS_API ParticleSystemCompiler
	{
	public:
		// Compiles unconditionally into `out`.
		static void Compile(ParticleSystem& system, CompiledParticleSystem& out);

		// Cached by system uuid + ParticleSystem::CompileRevision. The reference stays valid until the next
		// call (a compile may rehash the cache): copy out of it, never store the address.
		static const CompiledParticleSystem& GetCompiled(ParticleSystem& system);
		static void Invalidate(const PluUUID& systemUuid);
	};
}

#endif //PLUENGINE_PARTICLESYSTEMCOMPILER_H
