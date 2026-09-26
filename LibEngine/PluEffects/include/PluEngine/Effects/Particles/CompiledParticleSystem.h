//
// Created by Plutex on 9/26/26.
//

#ifndef PLUENGINE_COMPILEDPARTICLESYSTEM_H
#define PLUENGINE_COMPILEDPARTICLESYSTEM_H

#include "PluEngine/Core.h"
#include "PluEngine/PluTypes.h"
#include "PluEngine/PluUUID.h"
#include "PluEngine/Effects/Particles/ParticleAttributes.h"
#include "Array/Array.h"
#include "String/String.h"

// Flat, pointer-free (apart from the containers) description of a ParticleSystem that the executor
// runs. Built once per CompileRevision on the main thread by ParticleSystemCompiler, then only read.
// Nothing in here refers back to the asset graph, so it can be copied to the render thread.
namespace Plu
{
	// Particles are processed in blocks of this many: one dispatch per op per block.
	constexpr UInt32 kParticleBlockSize = 4096;

	// SoA columns. Every column is a float (Seed and RibbonId are bit-cast), so an operand never needs
	// a type tag. The first kAlwaysColumnCount exist for every emitter; the rest are allocated on demand
	// (bit in CompiledEmitter::UsedAttributes, a UInt32 — so at most 32 columns in total).
	enum class EParticleColumn : UInt8
	{
		// Always present.
		PosX, PosY, PosZ,
		VelX, VelY, VelZ,
		Age, Lifetime, NormalizedAge,
		ColR, ColG, ColB, ColA,
		SizeX, SizeY,
		// On demand.
		Rotation,
		Seed,
		SubUVFrame,
		PrevPosX, PrevPosY, PrevPosZ,   // ribbons (velocity stretch reads Vel*)
		RibbonId, RibbonWidth,
		Speed,                           // |velocity|, refreshed by the ComputeSpeed op
		BaseSizeX, BaseSizeY,            // size at birth: SizeOverLife / SizeBySpeed scale it
		BaseColR, BaseColG, BaseColB, BaseColA,
		Custom0,                         // KillWhenSlow's "armed" flag
		Custom1,                         // spare
		Count
	};
	constexpr UInt32 kParticleColumnCount = static_cast<UInt32>(EParticleColumn::Count);
	constexpr UInt32 kAlwaysColumnCount = static_cast<UInt32>(EParticleColumn::Rotation);
	static_assert(kParticleColumnCount <= 32, "UsedAttributes is a UInt32 bit mask");

	using ParticleAttributeMask = UInt32;
	constexpr ParticleAttributeMask ParticleColumnBit(EParticleColumn column) { return 1u << static_cast<UInt32>(column); }
	constexpr ParticleAttributeMask kAlwaysColumnMask = (1u << kAlwaysColumnCount) - 1u;

	enum class EParticleOperandKind : UInt8
	{
		Constant,           // Constants[Index]
		Attribute,          // per-particle column SrcAttribute
		Parameter,          // float offset Index into the per-spawner parameter block
		CurveOverAttribute, // Curves[Index], channel Channel, sampled at column SrcAttribute
		RandomRange         // uniform in [Constants[Index], Constants[Index + 1]], drawn per particle
	};

	struct ParticleOperand
	{
		EParticleOperandKind Kind = EParticleOperandKind::Constant;
		UInt8  SrcAttribute = 0; // EParticleColumn
		UInt8  Channel = 0;      // CurveOverAttribute: interleaved LUT channel
		UInt8  Pad = 0;
		UInt16 Index = 0;
	};

	enum class EParticleOpCode : UInt8
	{
		Set,               // Dst = A
		Add,               // Dst += A
		MulAddDt,          // Dst += A * dt
		Damp,              // Dst *= exp(-A * dt)
		Mul,               // Dst = A * B
		MulCurve,          // Dst[k] = A.col[k] * B(curve)[channel k], k < Flags&0x7F; Flags&0x80 = one scalar channel for all
		Copy,              // Dst = column A.SrcAttribute
		AgeAdvance,        // Age += dt; NormalizedAge = Age / Lifetime
		IntegratePosition, // Pos += Vel * dt
		ComputeSpeed,      // Speed = |Vel|
		KillSlow,          // Custom0 arms once Speed > A; Lifetime = 0 once armed and Speed <= A
		SubUV,             // SubUVFrame from Age / NormalizedAge; params at Constants[A.Index..]
		SpawnPosition,     // shape Flags; params at Constants[A.Index..], spawner transform applied
		SpawnVelocity,     // speed operand A; direction params at Constants[B.Index..]
		Count
	};

	struct ParticleOp
	{
		EParticleOpCode Code = EParticleOpCode::Set;
		UInt8 Dst = 0;   // EParticleColumn
		UInt8 Flags = 0;
		ParticleOperand A, B, C;
	};

	// A curve baked to a uniform lookup table. The LUT samples the curve over its normalised [0,1] time;
	// InMin/InMax is the range of the SOURCE attribute that maps onto that full [0,1].
	struct ParticleCurveLUT
	{
		UInt32 SampleOffset = 0; // into CompiledEmitter::CurveSamples
		UInt16 SampleCount = 0;
		UInt8  Channels = 1;     // interleaved (1 = scalar curve, 4 = RGBA gradient)
		UInt8  Pad = 0;
		float  InMin = 0.0f;
		float  InMax = 1.0f;
	};

	enum class EParticleParameterType : UInt8 { Float, Int, Bool, Vec3, Color };

	struct CompiledParameterSlot
	{
		String Name;
		EParticleParameterType Type = EParticleParameterType::Float;
		UInt16 FloatOffset = 0;
		UInt8  FloatCount = 1;
	};

	enum class EParticleRendererKind : UInt8 { None, Sprite, Ribbon };

	// Render description, plain data: the GL side reads it, never the other way round (PluEffects sits
	// below PluRender).
	struct ParticleRenderParams
	{
		EParticleRendererKind Kind = EParticleRendererKind::None;
		EParticleBlendMode Blend = EParticleBlendMode::Additive;
		EParticleFacingMode Facing = EParticleFacingMode::CameraFacing;
		EParticleRibbonMode RibbonMode = EParticleRibbonMode::PerEmitter;
		UInt64 TextureUuid = 0;
		UInt16 SubUVColumns = 1;
		UInt16 SubUVRows = 1;
		float StretchFactor = 0.05f;
		float RibbonWidth = 0.05f;
		UInt16 RibbonHistoryLength = 16;
	};

	struct CompiledEmitter
	{
		PluUUID EmitterUuid;
		String  Name;
		ParticleAttributeMask UsedAttributes = kAlwaysColumnMask;
		DynamicArray<ParticleOp> SpawnOps;   // over [firstNew, newEnd)
		DynamicArray<ParticleOp> UpdateOps;  // over [0, aliveCount)
		DynamicArray<float> Constants;
		DynamicArray<ParticleCurveLUT> Curves;
		DynamicArray<float> CurveSamples;
		float  SpawnRate = 0.0f;
		UInt32 BurstCount = 0;
		float  BurstTime = 0.0f;
		float  Duration = 1.0f;
		bool   Loop = true;
		UInt32 MaxParticles = 65536;
		bool   Enabled = true;
		bool   SimulateInLocalSpace = false; // reserved; v1 always false
		// One slot per renderer kind, both drawn from the same particles (a sprite head on a ribbon trail).
		// A slot is in use when its Kind matches it; an emitter with neither is drawn as points.
		ParticleRenderParams Sprite;
		ParticleRenderParams Ribbon;

		[[nodiscard]] bool HasSprite() const { return Sprite.Kind == EParticleRendererKind::Sprite; }
		[[nodiscard]] bool HasRibbon() const { return Ribbon.Kind == EParticleRendererKind::Ribbon; }
	};

	struct CompiledParticleSystem
	{
		PluUUID SystemUuid;
		UInt32  Revision = 0;
		DynamicArray<CompiledEmitter> Emitters;
		// From ParticleSystem::Parameters, in order. Offsets are baked into the ops' Parameter operands.
		DynamicArray<CompiledParameterSlot> ParameterLayout;
		UInt32 ParameterFloatCount = 0;      // floats in one block of parameter values
		DynamicArray<float> ParameterDefaults; // the asset's defaults, same layout
	};

	inline const char* ParticleOpName(EParticleOpCode code)
	{
		static const char* const kNames[] = {
			"Set", "Add", "MulAddDt", "Damp", "Mul", "MulCurve", "Copy", "AgeAdvance", "IntegratePosition",
			"ComputeSpeed", "KillSlow", "SubUV", "SpawnPosition", "SpawnVelocity"
		};
		return kNames[static_cast<UInt32>(code)];
	}
}

#endif //PLUENGINE_COMPILEDPARTICLESYSTEM_H
