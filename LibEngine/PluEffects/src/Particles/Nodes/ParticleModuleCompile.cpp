//
// Created by Plutex on 9/26/26.
//

// Compile() of every module: turns the authored properties into ops on the compiled emitter. Runs only
// on the main thread, from ParticleSystemCompiler — see the header for the resolution rules.

#include "PluEngine/Effects/Particles/Nodes/ParticleSpawnModules.h"
#include "PluEngine/Effects/Particles/Nodes/ParticleUpdateModules.h"
#include "PluEngine/Effects/Particles/Nodes/ParticleRendererNodes.h"
#include "PluEngine/Effects/Particles/ParticleSystemCompiler.h"
#include "PluEngine/Effects/Particles/ParticleSystem.h"

#include <algorithm>
#include <cmath>

namespace Plu
{
	namespace
	{
		using Op = EParticleOpCode;
		using Col = EParticleColumn;

		Col ColumnAt(Col first, UInt32 offset) { return static_cast<Col>(static_cast<UInt32>(first) + offset); }

		bool IsZeroConstant(const ParticleOperand& operand, const ParticleCompileContext& ctx)
		{
			return operand.Kind == EParticleOperandKind::Constant && operand.Index < ctx.Out.Constants.Size()
			    && ctx.Out.Constants[operand.Index] == 0.0f;
		}
	}

	// ---- spawn ---------------------------------------------------------------------------------

	void SpawnRateModule::Compile(ParticleCompileContext& ctx)
	{
		ctx.Out.SpawnRate += std::max(0.0f, ctx.ResolveConstantFloat(*this, "Rate", Rate));
	}

	void SpawnBurstModule::Compile(ParticleCompileContext& ctx)
	{
		if (ctx.BurstSeen) {
			PLU_CORE_WARN("Particle emitter '{}': more than one Spawn Burst module, only the first is used",
			              ctx.Emitter.EmitterName.CStr());
			return;
		}
		ctx.BurstSeen = true;
		ctx.Out.BurstCount = static_cast<UInt32>(std::max(0, ctx.ResolveConstantInt(*this, "Count", Count)));
		ctx.Out.BurstTime = std::max(0.0f, ctx.ResolveConstantFloat(*this, "Time", Time));
	}

	void InitLifetimeModule::Compile(ParticleCompileContext& ctx)
	{
		ctx.EmitSpawn(ParticleCompileContext::MakeOp(Op::Set, Col::Lifetime, ctx.ResolveFloat(*this, "Lifetime", Lifetime)));
	}

	void InitLocationModule::Compile(ParticleCompileContext& ctx)
	{
		// radius, box extent xyz, cone angle, offset xyz — read by the SpawnPosition op.
		const float params[8] = { Radius, BoxExtent.x, BoxExtent.y, BoxExtent.z, ConeAngle, Offset.x, Offset.y, Offset.z };
		ParticleOp op = ParticleCompileContext::MakeOp(Op::SpawnPosition, Col::PosX, ctx.AddConstants(params, 8));
		op.Flags = static_cast<UInt8>(Shape);
		ctx.EmitSpawn(op);
	}

	void InitVelocityModule::Compile(ParticleCompileContext& ctx)
	{
		Vec3 dir = Direction;
		dir = glm::length(dir) > 1e-6f ? glm::normalize(dir) : Vec3(0.0f, 0.0f, -1.0f);
		const float params[4] = { dir.x, dir.y, dir.z, ConeAngle };
		const ParticleOperand directionParams = ctx.AddConstants(params, 4);
		ctx.EmitSpawn(ParticleCompileContext::MakeOp(Op::SpawnVelocity, Col::VelX,
		                                             ctx.ResolveFloat(*this, "Speed", Speed), directionParams));

		ParticleOperand randomness[3];
		ctx.ResolveVec3(*this, "Randomness", Randomness, randomness);
		for (UInt32 axis = 0; axis < 3; ++axis) {
			if (IsZeroConstant(randomness[axis], ctx)) continue;
			ctx.EmitSpawn(ParticleCompileContext::MakeOp(Op::Add, ColumnAt(Col::VelX, axis), randomness[axis]));
		}
	}

	void InitSizeModule::Compile(ParticleCompileContext& ctx)
	{
		// Height derives from the width column, not from a second draw of the operand, so a random size
		// keeps its aspect ratio.
		ctx.EmitSpawn(ParticleCompileContext::MakeOp(Op::Set, Col::SizeX, ctx.ResolveFloat(*this, "Size", Size)));
		ctx.EmitSpawn(ParticleCompileContext::MakeOp(Op::Mul, Col::SizeY, ctx.AttributeOperand(Col::SizeX), ctx.Constant(AspectRatio)));
	}

	void InitColorModule::Compile(ParticleCompileContext& ctx)
	{
		ParticleOperand color[4];
		ctx.ResolveColor(*this, "Color", Color, color);
		for (UInt32 c = 0; c < 4; ++c)
			ctx.EmitSpawn(ParticleCompileContext::MakeOp(Op::Set, ColumnAt(Col::ColR, c), color[c]));
	}

	void InitRotationModule::Compile(ParticleCompileContext& ctx)
	{
		ctx.Require(Col::Rotation);
		ctx.EmitSpawn(ParticleCompileContext::MakeOp(Op::Set, Col::Rotation, ctx.ResolveFloat(*this, "Rotation", Rotation)));
	}

	void InitSubUVFrameModule::Compile(ParticleCompileContext& ctx)
	{
		const Int32 total = static_cast<Int32>(std::max<UInt32>(1, ctx.Out.Sprite.SubUVColumns * ctx.Out.Sprite.SubUVRows));
		const Int32 first = std::clamp(ctx.ResolveConstantInt(*this, "FirstFrame", FirstFrame), 0, total - 1);
		const Int32 lastAuthored = ctx.ResolveConstantInt(*this, "LastFrame", LastFrame);
		const Int32 last = lastAuthored < 0 ? total - 1 : std::clamp(lastAuthored, first, total - 1);

		ctx.Require(Col::SubUVFrame);
		ctx.Require(Col::Seed); // the start frame is derived from it, so SubUV Animation can recompute it every tick
		ctx.SubUVStartFirst = static_cast<UInt32>(first);
		ctx.SubUVStartCount = static_cast<UInt32>(last - first + 1);
		// The SubUV op without the animate flag: just the start frame, written once at spawn.
		const float params[4] = { 0.0f, static_cast<float>(total), static_cast<float>(ctx.SubUVStartFirst),
		                          static_cast<float>(ctx.SubUVStartCount) };
		ctx.EmitSpawn(ParticleCompileContext::MakeOp(Op::SubUV, Col::SubUVFrame, ctx.AddConstants(params, 4)));
	}

	// ---- update --------------------------------------------------------------------------------

	void GravityModule::Compile(ParticleCompileContext& ctx)
	{
		ParticleOperand accel[3];
		ctx.ResolveVec3Property(*this, "Gravity", Gravity, accel);
		for (UInt32 axis = 0; axis < 3; ++axis)
			ctx.EmitUpdate(ParticleCompileContext::MakeOp(Op::MulAddDt, ColumnAt(Col::VelX, axis), accel[axis]));
	}

	void AccelerationModule::Compile(ParticleCompileContext& ctx)
	{
		ParticleOperand accel[3];
		ctx.ResolveVec3Property(*this, "Acceleration", Acceleration, accel);
		for (UInt32 axis = 0; axis < 3; ++axis)
			ctx.EmitUpdate(ParticleCompileContext::MakeOp(Op::MulAddDt, ColumnAt(Col::VelX, axis), accel[axis]));
	}

	void DragModule::Compile(ParticleCompileContext& ctx)
	{
		const ParticleOperand drag = ctx.ResolveFloatProperty(*this, "Drag", Drag);
		for (UInt32 axis = 0; axis < 3; ++axis)
			ctx.EmitUpdate(ParticleCompileContext::MakeOp(Op::Damp, ColumnAt(Col::VelX, axis), drag));
	}

	void ColorOverLifeModule::Compile(ParticleCompileContext& ctx)
	{
		// Colour = colour at birth x gradient(normalised age), the four channels in one fused op.
		ParticleOperand gradient[4];
		ctx.BakeGradient(Gradient, Col::NormalizedAge, 0.0f, 1.0f, gradient);
		ParticleOp op = ParticleCompileContext::MakeOp(Op::MulCurve, Col::ColR, ctx.AttributeOperand(Col::BaseColR), gradient[0]);
		op.Flags = 4;
		ctx.EmitUpdate(op);
	}

	void SizeOverLifeModule::Compile(ParticleCompileContext& ctx)
	{
		const ParticleOperand scale = ctx.BakeCurve(Scale, Col::NormalizedAge, 0.0f, 1.0f);
		ParticleOp op = ParticleCompileContext::MakeOp(Op::MulCurve, Col::SizeX, ctx.AttributeOperand(Col::BaseSizeX), scale);
		op.Flags = 0x80 | 2; // one scalar curve scaling both axes
		ctx.EmitUpdate(op);
		ctx.SizeWrittenThisTick = true;
	}

	void SizeBySpeedModule::Compile(ParticleCompileContext& ctx)
	{
		const float maxSpeed = std::max(1e-3f, ctx.ResolveConstantFloat(*this, "MaxSpeed", MaxSpeed));
		ctx.EnsureSpeedComputed(false);
		const ParticleOperand scale = ctx.BakeCurve(Scale, Col::Speed, 0.0f, maxSpeed);
		// After SizeOverLife the size already holds base x life-scale: multiply on top of it. Alone it
		// scales the size at birth; multiplying the current size every tick would compound.
		ParticleOp op = ParticleCompileContext::MakeOp(Op::MulCurve, Col::SizeX,
		                                               ctx.AttributeOperand(ctx.SizeWrittenThisTick ? Col::SizeX : Col::BaseSizeX), scale);
		op.Flags = 0x80 | 2;
		ctx.EmitUpdate(op);
		ctx.SizeWrittenThisTick = true;
	}

	void RotationRateModule::Compile(ParticleCompileContext& ctx)
	{
		ctx.Require(Col::Rotation);
		const ParticleOperand rate = ctx.ResolveFloatProperty(*this, "Rate", Rate);
		const float randomness = std::abs(ctx.ResolveConstantFloat(*this, "RateRandomness", RateRandomness));
		if (randomness <= 0.0f) {
			ctx.EmitUpdate(ParticleCompileContext::MakeOp(Op::MulAddDt, Col::Rotation, rate));
			return;
		}
		// Random spin: drawn once at spawn into its own column — a random operand in the update list would be
		// re-drawn every tick. A wired Rate (parameter / attribute) is sampled at spawn as well.
		ctx.Require(Col::RotationRate);
		ctx.EmitSpawn(ParticleCompileContext::MakeOp(Op::Set, Col::RotationRate, rate));
		ctx.EmitSpawn(ParticleCompileContext::MakeOp(Op::Add, Col::RotationRate, ctx.Random(-randomness, randomness)));
		ctx.EmitUpdate(ParticleCompileContext::MakeOp(Op::MulAddDt, Col::Rotation, ctx.AttributeOperand(Col::RotationRate)));
	}

	void SubUVAnimationModule::Compile(ParticleCompileContext& ctx)
	{
		ctx.Require(Col::SubUVFrame);
		const float frames = static_cast<float>(std::max<UInt32>(1, ctx.Out.Sprite.SubUVColumns * ctx.Out.Sprite.SubUVRows));
		// first / count stay 0 unless an Init SubUV Frame module patches them in (ParticleSystemCompiler).
		const float params[4] = { std::max(0.0f, ctx.ResolveConstantFloat(*this, "FramesPerSecond", FramesPerSecond)), frames, 0.0f, 0.0f };
		const ParticleOperand constants = ctx.AddConstants(params, 4);
		ctx.SubUVAnimationConstants = static_cast<Int32>(constants.Index);
		ParticleOp op = ParticleCompileContext::MakeOp(Op::SubUV, Col::SubUVFrame, constants);
		op.Flags = 1; // animate
		ctx.EmitUpdate(op);
	}

	void KillWhenSlowModule::Compile(ParticleCompileContext& ctx)
	{
		ctx.Require(Col::Custom0);
		ctx.EnsureSpeedComputed(false);
		ctx.EmitUpdate(ParticleCompileContext::MakeOp(Op::KillSlow, Col::Lifetime,
		                                              ctx.Constant(ctx.ResolveConstantFloat(*this, "Speed", Speed))));
	}

	// ---- renderers -----------------------------------------------------------------------------

	void SpriteRendererModule::Compile(ParticleCompileContext& ctx)
	{
		// One Sprite and one Ribbon renderer may coexist; a second of the same kind replaces the first.
		if (ctx.SpriteRendererSeen)
			PLU_CORE_WARN("Particle emitter '{}': more than one Sprite Renderer, the last one wins", ctx.Emitter.EmitterName.CStr());
		ctx.SpriteRendererSeen = true;

		ParticleRenderParams& render = ctx.Out.Sprite;
		render = ParticleRenderParams();
		render.Kind = EParticleRendererKind::Sprite;
		render.Blend = Blend;
		render.Facing = Facing;
		render.TextureUuid = Texture ? Texture->Uuid.getUUID() : 0;
		render.SubUVColumns = static_cast<UInt16>(std::max(1, SubUVColumns));
		render.SubUVRows = static_cast<UInt16>(std::max(1, SubUVRows));
		// Velocity stretch reads the always-present velocity columns, not a previous position: it keeps
		// its shape while paused (dt = 0) and does not depend on the frame rate.
		render.StretchFactor = StretchFactor;
	}

	void RibbonRendererModule::Compile(ParticleCompileContext& ctx)
	{
		if (ctx.RibbonRendererSeen)
			PLU_CORE_WARN("Particle emitter '{}': more than one Ribbon Renderer, the last one wins", ctx.Emitter.EmitterName.CStr());
		ctx.RibbonRendererSeen = true;

		ParticleRenderParams& render = ctx.Out.Ribbon;
		render = ParticleRenderParams();
		render.Kind = EParticleRendererKind::Ribbon;
		render.Blend = Blend;
		render.RibbonMode = Mode;
		render.TextureUuid = Texture ? Texture->Uuid.getUUID() : 0;
		render.RibbonWidth = Width;
		render.RibbonHistoryLength = static_cast<UInt16>(std::clamp(HistoryLength, 2, 256));
	}
}
