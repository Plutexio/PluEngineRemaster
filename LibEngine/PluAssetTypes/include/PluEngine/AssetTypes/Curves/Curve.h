//
// Created by Plutex on 9/25/26.
//

#ifndef PLUENGINE_CURVE_H
#define PLUENGINE_CURVE_H

#include "PluEngine/Core.h"
#include "PluEngine/PluTypes.h"
#include "PluEngine/Core/Reflection/TypeTraits.h"
#include "Array/Array.h"
#include "String/String.h"
#include "Curve.generated.h"

namespace Plu
{
	PLU_ENUM(PyNamespace=Plu)
	enum class ECurveInterp : UInt8
	{
		Constant,
		Linear,
		Cubic
	};

	// Generic scalar curve (time -> value). Knows nothing about particles; anything that wants a
	// value-over-something-normalised authoring surface can use it.
	PLU_STRUCT()
	struct PLUASSETTYPES_API CurveKey
	{
		REFLECTION_BODY_CURVEKEY()

		PLU_PROPERTY()
		float Time = 0.0f;
		PLU_PROPERTY()
		float Value = 0.0f;
		// Only used by ECurveInterp::Cubic (Hermite tangents, value units per time unit).
		PLU_PROPERTY()
		float InTangent = 0.0f;
		PLU_PROPERTY()
		float OutTangent = 0.0f;
	};

	PLU_STRUCT()
	struct PLUASSETTYPES_API Curve
	{
		REFLECTION_BODY_CURVE()

		// Kept sorted by Time (call SortKeys after editing).
		PLU_PROPERTY()
		DynamicArray<CurveKey> Keys;
		PLU_PROPERTY()
		ECurveInterp Interp = ECurveInterp::Linear;

		static constexpr UInt32 kLinearLUTSamples = 64;
		static constexpr UInt32 kCubicLUTSamples = 128;

		// Flat curve at `value` (two keys, so editors always have something to drag).
		static Curve Constant(float value);
		// Straight line from `a` at t=0 to `b` at t=1.
		static Curve Ramp(float a, float b);

		void SortKeys();
		// Clamped outside the key range; 0 for an empty curve.
		[[nodiscard]] float Evaluate(float time) const;
		// Samples Evaluate uniformly over [inMin, inMax] into `outSamples` (appended). Returns the
		// sample count, which is kLinearLUTSamples, or kCubicLUTSamples for cubic curves.
		UInt32 BakeLUT(float inMin, float inMax, DynamicArray<float>& outSamples) const;
	};

	PLU_STRUCT()
	struct PLUASSETTYPES_API ColorKey
	{
		REFLECTION_BODY_COLORKEY()

		PLU_PROPERTY()
		float Time = 0.0f;
		PLU_PROPERTY()
		Vec4 Color = Vec4(1.0f);
	};

	PLU_STRUCT()
	struct PLUASSETTYPES_API ColorGradient
	{
		REFLECTION_BODY_COLORGRADIENT()

		PLU_PROPERTY()
		DynamicArray<ColorKey> Keys;

		static ColorGradient Constant(const Vec4& color);

		void SortKeys();
		// Linear RGBA interpolation, clamped outside the key range; white for an empty gradient.
		[[nodiscard]] Vec4 Evaluate(float time) const;
		// Appends kLinearLUTSamples RGBA samples (4 floats each) over [inMin, inMax].
		UInt32 BakeLUT(float inMin, float inMax, DynamicArray<float>& outSamples) const;
	};
	// ---- editor widgets ---------------------------------------------------------------------------
	// Defined in Curve.cpp; the ImGui bodies exist only in editor builds (elsewhere they draw nothing and
	// report no change). Return true when the value changed — the caller dirties the asset and bumps the
	// owner's compile revision.

	// Polyline preview + draggable key points (right-click adds/removes), Interp combo, and Time/Value
	// (plus tangents for cubic) of the selected key. Keys live in normalised [0,1] time.
	PLUASSETTYPES_API bool CurveEditorControl(Curve& curve, const String& label);
	// Colour bar + draggable stops (right-click adds/removes) + ColorEdit4/Time of the selected stop.
	PLUASSETTYPES_API bool GradientEditorControl(ColorGradient& gradient, const String& label);
	// Non-interactive previews sized `width` x `height`, drawn at the ImGui cursor (node bodies use 60x24).
	PLUASSETTYPES_API void DrawCurveThumbnail(const Curve& curve, float width, float height);
	PLUASSETTYPES_API void DrawGradientThumbnail(const ColorGradient& gradient, float width, float height);

	// Hand-written serializers (same pattern as TypeSerializer<GraphValue>): JSON stays the generic
	// reflected-struct form, only the editor control is custom — one widget instead of a tree of members.
	template <>
	struct TypeSerializer<Curve>
	{
		static nlohmann::json Serialize(void* data)
		{
			return TypeRegistry::GetInstance()->serializeForTypeInfo(Curve::GetStaticClass(), data);
		}
		static void Deserialize(DeserializationContext* dc, const nlohmann::json& json, void* out)
		{
			TypeRegistry::GetInstance()->deserializeForTypeInfo(dc, json, Curve::GetStaticClass(), out);
		}
		static bool EditorControl(void* value, const String& name)
		{
			return CurveEditorControl(*static_cast<Curve*>(value), name);
		}
	};

	template <>
	struct TypeSerializer<ColorGradient>
	{
		static nlohmann::json Serialize(void* data)
		{
			return TypeRegistry::GetInstance()->serializeForTypeInfo(ColorGradient::GetStaticClass(), data);
		}
		static void Deserialize(DeserializationContext* dc, const nlohmann::json& json, void* out)
		{
			TypeRegistry::GetInstance()->deserializeForTypeInfo(dc, json, ColorGradient::GetStaticClass(), out);
		}
		static bool EditorControl(void* value, const String& name)
		{
			return GradientEditorControl(*static_cast<ColorGradient*>(value), name);
		}
	};
}

#endif //PLUENGINE_CURVE_H
