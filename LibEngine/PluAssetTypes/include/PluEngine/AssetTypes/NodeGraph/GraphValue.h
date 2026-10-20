//
// Created by Plutex on 7/25/26.
//

#ifndef PLUENGINE_GRAPHVALUE_H
#define PLUENGINE_GRAPHVALUE_H

#include "PluEngine/Core.h"
#include "PluEngine/PluTypes.h"
#include "PluEngine/Core/Reflection/TypeTraits.h"
#include "PluSTL/String/String.h"
#include "GraphValue.generated.h"

namespace Plu
{
	// The value types a data pin can carry. Deliberately the same set the animation graph registers as
	// variable types (AnimationGraphVariableFactory::RegisterBuiltInTypes), so a variable node's output
	// plugs straight into a math node's input — and the editor colours both pins identically.
	PLU_ENUM(PyNamespace=Plu)
	enum class EGraphValueType : UInt8
	{
		Float,
		Int,
		Bool,
		Vec3
	};

	// One authored value of a runtime-chosen type: the inline default a wildcard node uses when its
	// input pin is not connected, and the payload of a Constant node.
	//
	// NOT a reflected struct: it goes through the hand-written TypeSerializer<GraphValue> below (same
	// pattern as TypeSerializer<glm::vec3> / <PluUUID> / <CollisionProfileRef>). That is what lets the
	// details panel draw exactly one widget — the one matching Type — instead of one row per member.
	struct PLUASSETTYPES_API GraphValue
	{
		EGraphValueType Type = EGraphValueType::Float;

		float Float  = 0.0f;
		int   Int    = 0;
		bool  Bool   = false;
		Vec3  Vector = Vec3(0.0f);

		GraphValue() = default;
		explicit GraphValue(EGraphValueType type) : Type(type) {}

		// Pin TypeId for a value type — the reflected type-name spelling, because NodePin::CanConnect
		// is a plain string compare against the property type names used everywhere else.
		static const char* PinTypeId(EGraphValueType type)
		{
			switch (type) {
				case EGraphValueType::Float: return "float";
				case EGraphValueType::Int:   return "int";
				case EGraphValueType::Bool:  return "bool";
				case EGraphValueType::Vec3:  return "Vec3";
			}
			return "float";
		}

		[[nodiscard]] const char* PinTypeId() const { return PinTypeId(Type); }

		// Writes this value into `dst` (storage of the type named by `typeId`) when the types match.
		// Mirrors IAnimationGraphVariable::CopyValueTo — the contract EvaluateDataOutput needs.
		[[nodiscard]] bool CopyTo(void* dst, const String& typeId) const
		{
			if (!dst || typeId != PinTypeId(Type)) return false;
			switch (Type) {
				case EGraphValueType::Float: *static_cast<float*>(dst) = Float;  return true;
				case EGraphValueType::Int:   *static_cast<int*>(dst)   = Int;    return true;
				case EGraphValueType::Bool:  *static_cast<bool*>(dst)  = Bool;   return true;
				case EGraphValueType::Vec3:  *static_cast<Vec3*>(dst)  = Vector; return true;
			}
			return false;
		}
	};

	template <>
	struct TypeSerializer<GraphValue>
	{
		static nlohmann::json Serialize(void* dataToSerialize)
		{
			GraphValue& value = *static_cast<GraphValue*>(dataToSerialize);
			nlohmann::json json;
			// Type by name, same refactor-stable reasoning as the generic enum serializer.
			json["type"] = TypeSerializer<EGraphValueType>::Serialize(&value.Type);
			switch (value.Type) {
				case EGraphValueType::Float: json["value"] = value.Float; break;
				case EGraphValueType::Int:   json["value"] = value.Int;   break;
				case EGraphValueType::Bool:  json["value"] = value.Bool;  break;
				case EGraphValueType::Vec3:  json["value"] = { value.Vector.x, value.Vector.y, value.Vector.z }; break;
			}
			return json;
		}

		static void Deserialize(DeserializationContext* deserializationContext, const nlohmann::json& json, void* outValue)
		{
			GraphValue& value = *static_cast<GraphValue*>(outValue);
			if (!json.contains("type") || !json.contains("value")) return;

			TypeSerializer<EGraphValueType>::Deserialize(deserializationContext, json["type"], &value.Type);
			const nlohmann::json& payload = json["value"];
			switch (value.Type) {
				case EGraphValueType::Float: value.Float = payload.get<float>(); break;
				case EGraphValueType::Int:   value.Int   = payload.get<int>();   break;
				case EGraphValueType::Bool:  value.Bool  = payload.get<bool>();  break;
				case EGraphValueType::Vec3:
					value.Vector = Vec3(payload[0].get<float>(), payload[1].get<float>(), payload[2].get<float>());
					break;
			}
		}

		// Only the active type's widget is drawn: Type is owned by the node (it mirrors the node's
		// ValueType into its GraphValue members in BuildPins), so editing it here would desync the pins.
		static bool EditorControl(void* value, const String& name)
		{
			GraphValue& graphValue = *static_cast<GraphValue*>(value);
			switch (graphValue.Type) {
				case EGraphValueType::Float: return ImGui::DragFloat(name.CStr(), &graphValue.Float, 0.01f);
				case EGraphValueType::Int:   return ImGui::DragInt(name.CStr(), &graphValue.Int);
				case EGraphValueType::Bool:  return ImGui::Checkbox(name.CStr(), &graphValue.Bool);
				case EGraphValueType::Vec3:  return ImGui::DragFloat3(name.CStr(), &graphValue.Vector.x, 0.01f);
			}
			return false;
		}
	};
}

#endif //PLUENGINE_GRAPHVALUE_H
