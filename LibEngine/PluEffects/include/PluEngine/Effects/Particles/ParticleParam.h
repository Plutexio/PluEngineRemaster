//
// Created by Plutex on 9/25/26.
//

#ifndef PLUENGINE_PARTICLEPARAM_H
#define PLUENGINE_PARTICLEPARAM_H

#include "PluEngine/Core.h"
#include "PluEngine/PluTypes.h"
#include "PluEngine/AssetTypes/Curves/Curve.h"
#include "PluEngine/Effects/Particles/ParticleAttributes.h"
#include "ParticleParam.generated.h"

namespace Plu
{
	// How a module property produces its per-particle value. The authored value on the module; a data
	// pin wired into the same property overrides it (see ParticleModuleNode::AddParamPin).
	PLU_ENUM(PyNamespace=Plu)
	enum class EParticleParamMode : UInt8
	{
		Constant,
		RandomRange,     // uniform in [Min, Max], drawn once per particle
		CurveOverSource  // Curve / Gradient sampled at the Source attribute
	};

	PLU_STRUCT()
	struct PLUEFFECTS_API ParticleParamFloat
	{
		REFLECTION_BODY_PARTICLEPARAMFLOAT()

		PLU_PROPERTY()
		EParticleParamMode Mode = EParticleParamMode::Constant;
		PLU_PROPERTY()
		float Value = 1.0f;
		PLU_PROPERTY()
		float Min = 0.0f;
		PLU_PROPERTY()
		float Max = 1.0f;
		PLU_PROPERTY()
		Curve ValueCurve = Curve::Ramp(1.0f, 1.0f);
		// Curve input; the curve is sampled over [0,1] for NormalizedAge, over [0,SourceRange] otherwise.
		PLU_PROPERTY()
		EParticleAttribute Source = EParticleAttribute::NormalizedAge;
		PLU_PROPERTY()
		float SourceRange = 1.0f;

		ParticleParamFloat() = default;
		explicit ParticleParamFloat(float value) : Value(value) {}
	};

	// Per-component random range for RandomRange (Min/Max per axis).
	PLU_STRUCT()
	struct PLUEFFECTS_API ParticleParamVec3
	{
		REFLECTION_BODY_PARTICLEPARAMVEC3()

		PLU_PROPERTY()
		EParticleParamMode Mode = EParticleParamMode::Constant;
		PLU_PROPERTY()
		Vec3 Value = Vec3(0.0f);
		PLU_PROPERTY()
		Vec3 Min = Vec3(0.0f);
		PLU_PROPERTY()
		Vec3 Max = Vec3(0.0f);

		ParticleParamVec3() = default;
		explicit ParticleParamVec3(const Vec3& value) : Value(value) {}
	};

	PLU_STRUCT()
	struct PLUEFFECTS_API ParticleParamColor
	{
		REFLECTION_BODY_PARTICLEPARAMCOLOR()

		PLU_PROPERTY()
		EParticleParamMode Mode = EParticleParamMode::Constant;
		PLU_PROPERTY()
		Vec4 Value = Vec4(1.0f);
		PLU_PROPERTY()
		Vec4 Min = Vec4(1.0f);
		PLU_PROPERTY()
		Vec4 Max = Vec4(1.0f);
		PLU_PROPERTY()
		ColorGradient Gradient = ColorGradient::Constant(Vec4(1.0f));
		PLU_PROPERTY()
		EParticleAttribute Source = EParticleAttribute::NormalizedAge;
		PLU_PROPERTY()
		float SourceRange = 1.0f;

		ParticleParamColor() = default;
		explicit ParticleParamColor(const Vec4& value) : Value(value) {}
	};

	// Mode combo, then only what the mode uses: a colour picker (Constant), two pickers (RandomRange), or
	// the gradient + source attribute (CurveOverSource). The generic reflected editor would draw every field
	// and Value/Min/Max as four plain drags. Returns true on change. No-op outside the editor build.
	PLUEFFECTS_API bool ParticleParamColorEditorControl(ParticleParamColor& param, const String& label);

	// Hand-written only for the editor control (same pattern as TypeSerializer<Curve>): JSON stays the generic
	// reflected-struct form, so saved assets are unaffected.
	template <>
	struct TypeSerializer<ParticleParamColor>
	{
		static nlohmann::json Serialize(void* data)
		{
			return TypeRegistry::GetInstance()->serializeForTypeInfo(ParticleParamColor::GetStaticClass(), data);
		}
		static void Deserialize(DeserializationContext* dc, const nlohmann::json& json, void* out)
		{
			TypeRegistry::GetInstance()->deserializeForTypeInfo(dc, json, ParticleParamColor::GetStaticClass(), out);
		}
		static bool EditorControl(void* value, const String& name)
		{
			return ParticleParamColorEditorControl(*static_cast<ParticleParamColor*>(value), name);
		}
	};
}

#endif //PLUENGINE_PARTICLEPARAM_H
