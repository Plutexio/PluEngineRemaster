//
// Created by Plutex on 9/26/26.
//

#include "PluEngine/Effects/Particles/ParticleSystemCompiler.h"

#include "PluEngine/Effects/Particles/ParticleSystem.h"
#include "PluEngine/Effects/Particles/Nodes/ParticleEmitterOutputNode.h"
#include "PluEngine/Effects/Particles/Nodes/ParticleParameterNode.h"
#include "PluEngine/Effects/Particles/Nodes/ParticleSourceNodes.h"
#include "PluEngine/AssetTypes/Curves/Curve.h"
#include "PluEngine/AssetTypes/NodeGraph/GraphEvalContext.h"
#include "PluEngine/Timer.h"

namespace Plu
{
	namespace
	{
		constexpr UInt32 kMaxParticlesHardCap = 16u * 1024u * 1024u;
		constexpr UInt32 kRibbonPerParticleMaxParticles = 4096; // 1M x 16 history samples would be 256 MB

		HashMap<UInt64, CompiledParticleSystem>& Cache()
		{
			static HashMap<UInt64, CompiledParticleSystem> cache;
			return cache;
		}
	}

	// ---- pin resolution ------------------------------------------------------------------------

	struct ParticleCompileContext::PinResolution
	{
		enum class EKind { None, Folded, Parameter, Attribute, Failed };
		EKind Kind = EKind::None;
		float Values[4] = {};
		UInt32 ParamOffset = 0;
		EParticleAttribute Attribute = EParticleAttribute::Age;
	};

	ParticleOp ParticleCompileContext::MakeOp(EParticleOpCode code, EParticleColumn dst,
	                                          const ParticleOperand& a, const ParticleOperand& b)
	{
		ParticleOp op;
		op.Code = code;
		op.Dst = static_cast<UInt8>(dst);
		op.A = a;
		op.B = b;
		return op;
	}

	ParticleOperand ParticleCompileContext::AddConstants(const float* values, UInt32 count)
	{
		ParticleOperand operand;
		operand.Kind = EParticleOperandKind::Constant;
		if (Out.Constants.Size() + count > 0xFFFF) {
			PLU_CORE_WARN("Particle emitter '{}': constant pool overflow, value dropped", Emitter.EmitterName.CStr());
			return operand;
		}
		operand.Index = static_cast<UInt16>(Out.Constants.Size());
		for (UInt32 i = 0; i < count; ++i) Out.Constants.PushBack(values[i]);
		return operand;
	}

	ParticleOperand ParticleCompileContext::Constant(float value)
	{
		return AddConstants(&value, 1);
	}

	ParticleOperand ParticleCompileContext::Random(float min, float max)
	{
		const float pair[2] = { min, max };
		ParticleOperand operand = AddConstants(pair, 2);
		operand.Kind = EParticleOperandKind::RandomRange;
		return operand;
	}

	ParticleOperand ParticleCompileContext::AttributeOperand(EParticleColumn column)
	{
		Require(column);
		ParticleOperand operand;
		operand.Kind = EParticleOperandKind::Attribute;
		operand.SrcAttribute = static_cast<UInt8>(column);
		return operand;
	}

	ParticleOperand ParticleCompileContext::ParameterOperand(UInt32 floatOffset)
	{
		ParticleOperand operand;
		operand.Kind = EParticleOperandKind::Parameter;
		operand.Index = static_cast<UInt16>(floatOffset);
		return operand;
	}

	ParticleOperand ParticleCompileContext::BakeCurve(const Curve& curve, EParticleColumn source, float inMin, float inMax)
	{
		Require(source);
		ParticleCurveLUT lut;
		lut.SampleOffset = Out.CurveSamples.Size();
		lut.SampleCount = static_cast<UInt16>(curve.BakeLUT(0.0f, 1.0f, Out.CurveSamples));
		lut.Channels = 1;
		lut.InMin = inMin;
		lut.InMax = inMax;
		Out.Curves.PushBack(lut);

		ParticleOperand operand;
		operand.Kind = EParticleOperandKind::CurveOverAttribute;
		operand.SrcAttribute = static_cast<UInt8>(source);
		operand.Index = static_cast<UInt16>(Out.Curves.Size() - 1);
		return operand;
	}

	void ParticleCompileContext::BakeGradient(const ColorGradient& gradient, EParticleColumn source,
	                                          float inMin, float inMax, ParticleOperand out[4])
	{
		Require(source);
		ParticleCurveLUT lut;
		lut.SampleOffset = Out.CurveSamples.Size();
		lut.SampleCount = static_cast<UInt16>(gradient.BakeLUT(0.0f, 1.0f, Out.CurveSamples));
		lut.Channels = 4;
		lut.InMin = inMin;
		lut.InMax = inMax;
		Out.Curves.PushBack(lut);

		for (UInt32 c = 0; c < 4; ++c) {
			out[c].Kind = EParticleOperandKind::CurveOverAttribute;
			out[c].SrcAttribute = static_cast<UInt8>(source);
			out[c].Channel = static_cast<UInt8>(c);
			out[c].Index = static_cast<UInt16>(Out.Curves.Size() - 1);
		}
	}

	void ParticleCompileContext::EnsureSpeedComputed(bool inSpawnList)
	{
		Require(EParticleColumn::Speed);
		DynamicArray<ParticleOp>& list = inSpawnList ? SpawnBody : UpdateBody;
		if (!list.IsEmpty() && list[list.Size() - 1].Code == EParticleOpCode::ComputeSpeed) return;
		list.PushBack(MakeOp(EParticleOpCode::ComputeSpeed, EParticleColumn::Speed));
	}

	EParticleColumn ParticleCompileContext::ColumnFor(EParticleAttribute attribute)
	{
		switch (attribute) {
			case EParticleAttribute::Age:           return EParticleColumn::Age;
			case EParticleAttribute::NormalizedAge: return EParticleColumn::NormalizedAge;
			case EParticleAttribute::Speed:         return EParticleColumn::Speed;
			case EParticleAttribute::Position:      return EParticleColumn::PosX;
			case EParticleAttribute::Velocity:      return EParticleColumn::VelX;
			case EParticleAttribute::Seed:          return EParticleColumn::Seed;
		}
		return EParticleColumn::NormalizedAge;
	}

	// True when `from` or anything feeding it is a per-particle attribute or a parameter: such a subtree has
	// no single value at compile time.
	bool ParticleCompileContext::ReachesRuntimeSource(ParticleModuleNode& node, GraphNode* from, DynamicArray<UInt64>& visited)
	{
		if (!from) return false;
		if (dynamic_cast<ParticleParameterNode*>(from) || dynamic_cast<ParticleAttributeNode*>(from)) return true;
		if (visited.Contains(from->Uuid.getUUID())) return false;
		visited.PushBack(from->Uuid.getUUID());
		for (const NodePin& pin : from->InputPins) {
			if (pin.Category != EPinCategory::Data) continue;
			const NodeLink* link = Emitter.FindInputLink(from->Uuid, pin.Name);
			if (link && ReachesRuntimeSource(node, Emitter.FindNode(link->FromNode), visited)) return true;
		}
		return false;
	}

	ParticleCompileContext::PinResolution ParticleCompileContext::ResolvePin(ParticleModuleNode& node, const char* pin,
	                                                                       const char* typeId)
	{
		PinResolution result;
		const NodeLink* link = Emitter.FindInputLink(node.Uuid, pin);
		if (!link) return result; // nothing wired: the authored value applies

		GraphNode* source = Emitter.FindNode(link->FromNode);
		if (!source) { result.Kind = PinResolution::EKind::Failed; return result; }

		// Direct wire from a parameter: an operand, never a constant (the value changes per instance).
		if (auto* parameterNode = dynamic_cast<ParticleParameterNode*>(source)) {
			const CompiledParameterSlot* slot = nullptr;
			if (parameterNode->Parameter) {
				for (const CompiledParameterSlot& candidate : Program.ParameterLayout)
					if (candidate.Name == parameterNode->Parameter->Name) { slot = &candidate; break; }
			}
			if (!slot) {
				PLU_CORE_WARN("Particle emitter '{}': '{}' pin '{}' is wired to an unbound parameter, using the authored value",
				              Emitter.EmitterName.CStr(), node.GetDisplayName().CStr(), pin);
				result.Kind = PinResolution::EKind::Failed;
				return result;
			}
			result.Kind = PinResolution::EKind::Parameter;
			result.ParamOffset = slot->FloatOffset;
			return result;
		}

		if (auto* attributeNode = dynamic_cast<ParticleAttributeNode*>(source)) {
			result.Kind = PinResolution::EKind::Attribute;
			result.Attribute = attributeNode->Attribute;
			return result;
		}

		// Anything else must be a subtree of value nodes. v1 folds it once — but only when it cannot reach a
		// parameter or attribute: `Parameter * 2` has no compile-time value, and pretending otherwise would
		// bake the asset default into the program. That case is what a later expression VM removes.
		DynamicArray<UInt64> visited;
		if (ReachesRuntimeSource(node, source, visited)) {
			PLU_CORE_WARN("Particle emitter '{}': '{}' pin '{}' depends on a parameter or particle attribute through a "
			              "value expression. Only a direct wire is supported, using the authored value",
			              Emitter.EmitterName.CStr(), node.GetDisplayName().CStr(), pin);
			result.Kind = PinResolution::EKind::Failed;
			return result;
		}

		GraphEvalContext evalContext;
		evalContext.Graph = &Emitter;
		const String type(typeId);
		bool ok = false;
		if (type == "float") { float v = 0; ok = source->EvaluateDataOutput(evalContext, link->FromPin, type, &v); result.Values[0] = v; }
		else if (type == "int") { int v = 0; ok = source->EvaluateDataOutput(evalContext, link->FromPin, type, &v); result.Values[0] = static_cast<float>(v); }
		else if (type == "bool") { bool v = false; ok = source->EvaluateDataOutput(evalContext, link->FromPin, type, &v); result.Values[0] = v ? 1.0f : 0.0f; }
		else if (type == "Vec3") {
			Vec3 v(0.0f); ok = source->EvaluateDataOutput(evalContext, link->FromPin, type, &v);
			result.Values[0] = v.x; result.Values[1] = v.y; result.Values[2] = v.z;
		}
		if (!ok) {
			PLU_CORE_WARN("Particle emitter '{}': '{}' pin '{}' could not be evaluated, using the authored value",
			              Emitter.EmitterName.CStr(), node.GetDisplayName().CStr(), pin);
			result.Kind = PinResolution::EKind::Failed;
			return result;
		}
		result.Kind = PinResolution::EKind::Folded;
		return result;
	}

	// ---- property resolution -----------------------------------------------------------------

	ParticleOperand ParticleCompileContext::ResolveFloat(ParticleModuleNode& node, const char* pin, const ParticleParamFloat& param)
	{
		PinResolution r = ResolvePin(node, pin, "float");
		switch (r.Kind) {
			case PinResolution::EKind::Folded:    return Constant(r.Values[0]);
			case PinResolution::EKind::Parameter: return ParameterOperand(r.ParamOffset);
			case PinResolution::EKind::Attribute:
				if (r.Attribute == EParticleAttribute::Position || r.Attribute == EParticleAttribute::Velocity) break; // Vec3, not float
				if (r.Attribute == EParticleAttribute::Speed) EnsureSpeedComputed(false);
				return AttributeOperand(ColumnFor(r.Attribute));
			default: break;
		}

		switch (param.Mode) {
			case EParticleParamMode::Constant:    return Constant(param.Value);
			case EParticleParamMode::RandomRange: return Random(param.Min, param.Max);
			case EParticleParamMode::CurveOverSource: {
				EParticleAttribute source = param.Source;
				if (source == EParticleAttribute::Position || source == EParticleAttribute::Velocity) {
					PLU_CORE_WARN("Particle emitter '{}': '{}' curve source must be a scalar attribute, using NormalizedAge",
					              Emitter.EmitterName.CStr(), node.GetDisplayName().CStr());
					source = EParticleAttribute::NormalizedAge;
				}
				if (source == EParticleAttribute::Speed) EnsureSpeedComputed(false);
				// Normalised attributes span [0,1]; the others span [0, SourceRange].
				const bool normalised = source == EParticleAttribute::NormalizedAge || source == EParticleAttribute::Seed;
				return BakeCurve(param.ValueCurve, ColumnFor(source), 0.0f, normalised ? 1.0f : param.SourceRange);
			}
		}
		return Constant(param.Value);
	}

	void ParticleCompileContext::ResolveVec3(ParticleModuleNode& node, const char* pin, const ParticleParamVec3& param, ParticleOperand out[3])
	{
		PinResolution r = ResolvePin(node, pin, "Vec3");
		if (r.Kind == PinResolution::EKind::Folded) {
			for (UInt32 c = 0; c < 3; ++c) out[c] = Constant(r.Values[c]);
			return;
		}
		if (r.Kind == PinResolution::EKind::Parameter) {
			for (UInt32 c = 0; c < 3; ++c) out[c] = ParameterOperand(r.ParamOffset + c);
			return;
		}
		if (r.Kind == PinResolution::EKind::Attribute &&
		    (r.Attribute == EParticleAttribute::Position || r.Attribute == EParticleAttribute::Velocity)) {
			const UInt8 base = static_cast<UInt8>(ColumnFor(r.Attribute));
			for (UInt32 c = 0; c < 3; ++c) out[c] = AttributeOperand(static_cast<EParticleColumn>(base + c));
			return;
		}

		for (UInt32 c = 0; c < 3; ++c) {
			if (param.Mode == EParticleParamMode::RandomRange) out[c] = Random(param.Min[c], param.Max[c]);
			else out[c] = Constant(param.Value[c]);
		}
	}

	void ParticleCompileContext::ResolveColor(ParticleModuleNode& node, const char* pin, const ParticleParamColor& param, ParticleOperand out[4])
	{
		// A Color wire can only come from a Color parameter (nothing else produces a Vec4).
		const NodeLink* link = Emitter.FindInputLink(node.Uuid, pin);
		if (link) {
			if (auto* parameterNode = dynamic_cast<ParticleParameterNode*>(Emitter.FindNode(link->FromNode))) {
				for (const CompiledParameterSlot& slot : Program.ParameterLayout) {
					if (parameterNode->Parameter && slot.Name == parameterNode->Parameter->Name && slot.FloatCount == 4) {
						for (UInt32 c = 0; c < 4; ++c) out[c] = ParameterOperand(slot.FloatOffset + c);
						return;
					}
				}
			}
			PLU_CORE_WARN("Particle emitter '{}': '{}' pin '{}' must be wired to a Color parameter, using the authored value",
			              Emitter.EmitterName.CStr(), node.GetDisplayName().CStr(), pin);
		}

		switch (param.Mode) {
			case EParticleParamMode::Constant:
				for (UInt32 c = 0; c < 4; ++c) out[c] = Constant(param.Value[c]);
				break;
			case EParticleParamMode::RandomRange:
				for (UInt32 c = 0; c < 4; ++c) out[c] = Random(param.Min[c], param.Max[c]);
				break;
			case EParticleParamMode::CurveOverSource: {
				EParticleAttribute source = param.Source;
				if (source == EParticleAttribute::Position || source == EParticleAttribute::Velocity) source = EParticleAttribute::NormalizedAge;
				if (source == EParticleAttribute::Speed) EnsureSpeedComputed(false);
				const bool normalised = source == EParticleAttribute::NormalizedAge || source == EParticleAttribute::Seed;
				BakeGradient(param.Gradient, ColumnFor(source), 0.0f, normalised ? 1.0f : param.SourceRange, out);
				break;
			}
		}
	}

	ParticleOperand ParticleCompileContext::ResolveFloatProperty(ParticleModuleNode& node, const char* pin, float authored)
	{
		return ResolveFloat(node, pin, ParticleParamFloat(authored));
	}

	void ParticleCompileContext::ResolveVec3Property(ParticleModuleNode& node, const char* pin, const Vec3& authored, ParticleOperand out[3])
	{
		ResolveVec3(node, pin, ParticleParamVec3(authored), out);
	}

	float ParticleCompileContext::ResolveConstantFloat(ParticleModuleNode& node, const char* pin, float authored)
	{
		PinResolution r = ResolvePin(node, pin, "float");
		if (r.Kind == PinResolution::EKind::Folded) return r.Values[0];
		if (r.Kind == PinResolution::EKind::Parameter || r.Kind == PinResolution::EKind::Attribute) {
			PLU_CORE_WARN("Particle emitter '{}': '{}' pin '{}' is stored as a plain number in the program; "
			              "a parameter/attribute wire is not supported there, using the authored value",
			              Emitter.EmitterName.CStr(), node.GetDisplayName().CStr(), pin);
		}
		return authored;
	}

	int ParticleCompileContext::ResolveConstantInt(ParticleModuleNode& node, const char* pin, int authored)
	{
		PinResolution r = ResolvePin(node, pin, "int");
		if (r.Kind == PinResolution::EKind::Folded) return static_cast<int>(r.Values[0]);
		if (r.Kind == PinResolution::EKind::Parameter || r.Kind == PinResolution::EKind::Attribute) {
			PLU_CORE_WARN("Particle emitter '{}': '{}' pin '{}' is stored as a plain number in the program; "
			              "a parameter/attribute wire is not supported there, using the authored value",
			              Emitter.EmitterName.CStr(), node.GetDisplayName().CStr(), pin);
		}
		return authored;
	}

	// ---- emitter / system compile -------------------------------------------------------------

	namespace
	{
		void CompileEmitter(ParticleSystem& system, ParticleEmitter& emitter, CompiledParticleSystem& program, CompiledEmitter& out)
		{
			out.EmitterUuid = emitter.Uuid;
			out.Name = emitter.EmitterName;
			out.MaxParticles = std::min<UInt32>(std::max<UInt32>(emitter.MaxParticles, 1u), kMaxParticlesHardCap);
			out.Duration = emitter.Duration;
			out.Loop = emitter.Loop;
			out.Enabled = emitter.Enabled;
			out.UsedAttributes = kAlwaysColumnMask;

			// Chain: start at the output node and follow the Flow "In" link backwards, then reverse. A
			// cycle or a missing terminator yields an empty, disabled emitter — never a crash.
			ParticleModuleNode* output = emitter.FindOutputNode();
			if (!output) {
				PLU_CORE_WARN("Particle emitter '{}' has no Emitter Output node, it will not emit", emitter.EmitterName.CStr());
				out.Enabled = false;
				return;
			}

			DynamicArray<ParticleModuleNode*> chain;
			DynamicArray<UInt64> visited;
			visited.PushBack(output->Uuid.getUUID());
			GraphNode* current = emitter.GetLinkSource(output->Uuid, "In");
			while (current) {
				if (visited.Contains(current->Uuid.getUUID())) {
					PLU_CORE_WARN("Particle emitter '{}': module chain contains a cycle, it will not emit", emitter.EmitterName.CStr());
					out.Enabled = false;
					return;
				}
				visited.PushBack(current->Uuid.getUUID());
				auto* module = dynamic_cast<ParticleModuleNode*>(current);
				if (!module) break;
				chain.PushBack(module);
				current = emitter.GetLinkSource(module->Uuid, "In");
			}
			// chain is output->input; execution order is the reverse.
			for (UInt32 i = 0, j = chain.Size(); i + 1 < j; ++i, --j) std::swap(chain[i], chain[j - 1]);

			ParticleCompileContext ctx(system, emitter, program, out);

			// Renderer modules first: SubUVAnimation needs the atlas size, whatever its chain position.
			for (ParticleModuleNode* module : chain) if (module->GetStage() == EParticleModuleStage::Renderer) module->Compile(ctx);
			for (ParticleModuleNode* module : chain) if (module->GetStage() != EParticleModuleStage::Renderer) module->Compile(ctx);

			// Init SubUV Frame + SubUV Animation: the animation starts at each particle's random frame.
			if (ctx.SubUVAnimationConstants >= 0 && ctx.SubUVStartCount > 0) {
				out.Constants[static_cast<UInt32>(ctx.SubUVAnimationConstants) + 2] = static_cast<float>(ctx.SubUVStartFirst);
				out.Constants[static_cast<UInt32>(ctx.SubUVAnimationConstants) + 3] = static_cast<float>(ctx.SubUVStartCount);
			}

			// A fused MulCurve reads and writes Flags&0x7F consecutive columns starting at A / Dst, but its
			// operands name only the first. Every one of them must exist, or the executor skips the whole op
			// (a colour gradient would only ever touch R, i.e. nothing).
			const auto requireFusedColumns = [&ctx](const DynamicArray<ParticleOp>& ops) {
				for (const ParticleOp& fused : ops) {
					if (fused.Code != EParticleOpCode::MulCurve) continue;
					const UInt32 channels = fused.Flags & 0x7F;
					for (UInt32 k = 0; k < channels; ++k) {
						ctx.Require(static_cast<EParticleColumn>(fused.Dst + k));
						if (fused.A.Kind == EParticleOperandKind::Attribute)
							ctx.Require(static_cast<EParticleColumn>(fused.A.SrcAttribute + k));
					}
				}
			};
			requireFusedColumns(ctx.SpawnBody);
			requireFusedColumns(ctx.UpdateBody);

			const auto column = [](EParticleColumn c) { return c; };
			auto op = [](EParticleOpCode code, EParticleColumn dst, ParticleOperand a = ParticleOperand(), ParticleOperand b = ParticleOperand()) {
				return ParticleCompileContext::MakeOp(code, dst, a, b);
			};
			const auto has = [&](EParticleColumn c) { return (out.UsedAttributes & ParticleColumnBit(c)) != 0; };

			// Spawn = defaults, then the chain, then snapshots of the values at birth.
			out.SpawnOps.PushBack(op(EParticleOpCode::Set, EParticleColumn::Lifetime, ctx.Constant(1.0f)));
			out.SpawnOps.PushBack(op(EParticleOpCode::Set, EParticleColumn::SizeX, ctx.Constant(0.1f)));
			out.SpawnOps.PushBack(op(EParticleOpCode::Set, EParticleColumn::SizeY, ctx.Constant(0.1f)));
			for (UInt32 c = 0; c < 4; ++c)
				out.SpawnOps.PushBack(op(EParticleOpCode::Set, static_cast<EParticleColumn>(static_cast<UInt32>(EParticleColumn::ColR) + c), ctx.Constant(1.0f)));
			{
				// Default placement: the spawner's origin (an InitLocation module overrides it).
				const float defaults[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
				ParticleOp place = op(EParticleOpCode::SpawnPosition, EParticleColumn::PosX, ctx.AddConstants(defaults, 8));
				place.Flags = static_cast<UInt8>(EParticleSpawnShape::Point);
				out.SpawnOps.PushBack(place);
			}
			if (has(EParticleColumn::Seed))
				out.SpawnOps.PushBack(op(EParticleOpCode::Set, EParticleColumn::Seed, ctx.Random(0.0f, 1.0f)));
			for (const ParticleOp& body : ctx.SpawnBody) out.SpawnOps.PushBack(body);
			for (UInt32 c = 0; c < 2; ++c) {
				const EParticleColumn base = static_cast<EParticleColumn>(static_cast<UInt32>(EParticleColumn::BaseSizeX) + c);
				const EParticleColumn size = static_cast<EParticleColumn>(static_cast<UInt32>(EParticleColumn::SizeX) + c);
				if (has(base)) out.SpawnOps.PushBack(op(EParticleOpCode::Copy, base, ctx.AttributeOperand(size)));
			}
			for (UInt32 c = 0; c < 4; ++c) {
				const EParticleColumn base = static_cast<EParticleColumn>(static_cast<UInt32>(EParticleColumn::BaseColR) + c);
				const EParticleColumn color = static_cast<EParticleColumn>(static_cast<UInt32>(EParticleColumn::ColR) + c);
				if (has(base)) out.SpawnOps.PushBack(op(EParticleOpCode::Copy, base, ctx.AttributeOperand(color)));
			}
			for (UInt32 c = 0; c < 3; ++c) {
				const EParticleColumn prev = static_cast<EParticleColumn>(static_cast<UInt32>(EParticleColumn::PrevPosX) + c);
				const EParticleColumn pos = static_cast<EParticleColumn>(static_cast<UInt32>(EParticleColumn::PosX) + c);
				if (has(prev)) out.SpawnOps.PushBack(op(EParticleOpCode::Copy, prev, ctx.AttributeOperand(pos)));
			}

			// Update = previous position, age, the chain, then move.
			for (UInt32 c = 0; c < 3; ++c) {
				const EParticleColumn prev = static_cast<EParticleColumn>(static_cast<UInt32>(EParticleColumn::PrevPosX) + c);
				const EParticleColumn pos = static_cast<EParticleColumn>(static_cast<UInt32>(EParticleColumn::PosX) + c);
				if (has(prev)) out.UpdateOps.PushBack(op(EParticleOpCode::Copy, prev, ctx.AttributeOperand(pos)));
			}
			out.UpdateOps.PushBack(op(EParticleOpCode::AgeAdvance, EParticleColumn::Age));
			for (const ParticleOp& body : ctx.UpdateBody) out.UpdateOps.PushBack(body);
			out.UpdateOps.PushBack(op(EParticleOpCode::IntegratePosition, EParticleColumn::PosX));

			if (out.HasRibbon() && out.Ribbon.RibbonMode == EParticleRibbonMode::PerParticle
			    && out.MaxParticles > kRibbonPerParticleMaxParticles) {
				PLU_CORE_WARN("Particle emitter '{}': per-particle ribbons keep a position history per particle; MaxParticles "
				              "clamped from {} to {}", emitter.EmitterName.CStr(), out.MaxParticles, kRibbonPerParticleMaxParticles);
				out.MaxParticles = kRibbonPerParticleMaxParticles;
			}
			(void)column;
		}
	}

	void ParticleSystemCompiler::Compile(ParticleSystem& system, CompiledParticleSystem& out)
	{
		PLU_PROFILE_SCOPE("Particles/Compile");
		out = CompiledParticleSystem();
		out.SystemUuid = system.Uuid;
		out.Revision = system.CompileRevision;

		// Parameter layout first: the emitters' Parameter operands bake these offsets.
		UInt32 offset = 0;
		for (TOwningPointer<IParticleParameter>& parameter : system.Parameters) {
			if (!parameter) continue;
			CompiledParameterSlot slot;
			slot.Name = parameter->Name;
			slot.FloatOffset = static_cast<UInt16>(offset);
			slot.FloatCount = parameter->GetFloatCount();
			if (parameter->PinTypeId == "int") slot.Type = EParticleParameterType::Int;
			else if (parameter->PinTypeId == "bool") slot.Type = EParticleParameterType::Bool;
			else if (parameter->PinTypeId == "Vec3") slot.Type = EParticleParameterType::Vec3;
			else if (parameter->PinTypeId == "Vec4") slot.Type = EParticleParameterType::Color;
			else slot.Type = EParticleParameterType::Float;

			float values[4] = {};
			parameter->WriteFloats(values);
			for (UInt32 i = 0; i < slot.FloatCount; ++i) out.ParameterDefaults.PushBack(values[i]);
			offset += slot.FloatCount;
			out.ParameterLayout.PushBack(slot);
		}
		out.ParameterFloatCount = offset;

		for (TOwningPointer<ParticleEmitter>& emitter : system.Emitters) {
			if (!emitter) continue;
			out.Emitters.PushBack(CompiledEmitter());
			CompileEmitter(system, *emitter, out, out.Emitters[out.Emitters.Size() - 1]);
		}
	}

	const CompiledParticleSystem& ParticleSystemCompiler::GetCompiled(ParticleSystem& system)
	{
		auto& cache = Cache();
		CompiledParticleSystem* found = cache.Find(system.Uuid.getUUID());
		if (found && found->Revision == system.CompileRevision) return *found;

		CompiledParticleSystem compiled;
		Compile(system, compiled);
		cache.InsertOrAssign(system.Uuid.getUUID(), compiled);
		return *cache.Find(system.Uuid.getUUID());
	}

	void ParticleSystemCompiler::Invalidate(const PluUUID& systemUuid)
	{
		Cache().Remove(systemUuid.getUUID());
	}
}
