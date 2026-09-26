//
// Created by Plutex on 9/25/26.
//

#ifndef PLUENGINE_PARTICLEPARAMETER_H
#define PLUENGINE_PARTICLEPARAMETER_H

#include "PluEngine/Core.h"
#include "PluEngine/PluTypes.h"
#include "PluEngine/Core/Reflection/TypeTraits.h"
#include "Pointers/TOwningPointer.h"
#include "Pointers/TUsePointer.h"
#include "String/String.h"
#include "ParticleParameter.generated.h"

#include <functional>

namespace Plu
{
	// A user parameter of a ParticleSystem (e.g. "TracerColor"). Lives on the system, not on an emitter,
	// so one parameter can drive several emitters. Same shape as IAnimationGraphVariable.
	PLU_STRUCT(Abstract)
	struct PLUEFFECTS_API IParticleParameter
	{
		REFLECTION_BODY_IPARTICLEPARAMETER()

		virtual ~IParticleParameter() = default;

		virtual void* GetData() = 0;
		[[nodiscard]] virtual JSON Serialize() = 0;
		virtual void DeSerialize(const JSON& jsonData, DeserializationContext* deserializationContext) = 0;
#ifdef PLU_ENGINE_EDITOR_BUILD
		virtual bool DrawEditorControl(const String& label) = 0;
#endif
		[[nodiscard]] virtual TOwningPointer<IParticleParameter> Clone() const = 0;

		// Copies the value into dst when expectedPinTypeId matches PinTypeId (data-pin read path).
		virtual bool CopyValueTo(void* dst, const String& expectedPinTypeId) const = 0;

		// Number of floats the value occupies in the flat per-spawner parameter block that reaches the
		// render thread (Int and Bool are stored as float too, so an operand never needs a type tag).
		[[nodiscard]] virtual UInt8 GetFloatCount() const = 0;
		virtual void WriteFloats(float* dst) const = 0;

		String Name;
		String TypeName;
		// Reflected type spelling a parameter node exposes ("float", "Vec3", ...) — NodePin::CanConnect
		// is an exact string compare. Supplied by the factory, not serialized.
		String PinTypeId;
	};

	namespace ParticleParameterDetail
	{
		inline UInt8 FloatCountOf(const float*) { return 1; }
		inline UInt8 FloatCountOf(const int*)   { return 1; }
		inline UInt8 FloatCountOf(const bool*)  { return 1; }
		inline UInt8 FloatCountOf(const Vec3*)  { return 3; }
		inline UInt8 FloatCountOf(const Vec4*)  { return 4; }

		inline void WriteValue(const float& v, float* dst) { dst[0] = v; }
		inline void WriteValue(const int& v, float* dst)   { dst[0] = static_cast<float>(v); }
		inline void WriteValue(const bool& v, float* dst)  { dst[0] = v ? 1.0f : 0.0f; }
		inline void WriteValue(const Vec3& v, float* dst)  { dst[0] = v.x; dst[1] = v.y; dst[2] = v.z; }
		inline void WriteValue(const Vec4& v, float* dst)  { dst[0] = v.x; dst[1] = v.y; dst[2] = v.z; dst[3] = v.w; }
	}

	template <typename T>
	struct ParticleParameter : public IParticleParameter
	{
	private:
		T mValue{};
	public:
		void DeSerialize(const JSON& jsonData, DeserializationContext* deserializationContext) override
		{
			TypeSerializer<T>::Deserialize(deserializationContext, jsonData, &mValue);
		}

#ifdef PLU_ENGINE_EDITOR_BUILD
		bool DrawEditorControl(const String& label) override
		{
			return TypeSerializer<T>::EditorControl(&mValue, label);
		}
#endif

		void* GetData() override { return &mValue; }

		[[nodiscard]] JSON Serialize() override { return TypeSerializer<T>::Serialize(GetData()); }

		[[nodiscard]] TOwningPointer<IParticleParameter> Clone() const override
		{
			TOwningPointer<ParticleParameter<T>> clone = CreateOwning<ParticleParameter<T>>();
			clone->Name      = Name;
			clone->TypeName  = TypeName;
			clone->PinTypeId = PinTypeId;
			clone->mValue    = mValue;
			return clone;
		}

		bool CopyValueTo(void* dst, const String& expectedPinTypeId) const override
		{
			if (expectedPinTypeId != PinTypeId) return false;
			*static_cast<T*>(dst) = mValue;
			return true;
		}

		[[nodiscard]] UInt8 GetFloatCount() const override { return ParticleParameterDetail::FloatCountOf(&mValue); }
		void WriteFloats(float* dst) const override { ParticleParameterDetail::WriteValue(mValue, dst); }
	};

	struct PLUEFFECTS_API ParticleParameterFactory
	{
		using FactoryFunc = std::function<TOwningPointer<IParticleParameter>()>;
		struct ParameterTypeInfo {
			FactoryFunc CTor;
			Vec3 Color;
			String PinTypeName;
		};
		static HashMap<String, ParameterTypeInfo>& GetFactoryMap();

		// Float/Int/Bool/Vec3/Color. Called once from Application::EngineInit() so Editor and Runtime
		// share the factory — otherwise a Runtime loader drops every parameter of a loaded system.
		static void RegisterBuiltInTypes();

		template <typename T>
		static void RegisterType(const String& TypeName, const String& PinTypeName, Vec3 Color)
		{
			ParameterTypeInfo info;
			info.Color       = Color;
			info.PinTypeName = PinTypeName;
			info.CTor = [TypeName, PinTypeName]() -> TOwningPointer<IParticleParameter> {
				TOwningPointer<ParticleParameter<T>> parameter = CreateOwning<ParticleParameter<T>>();
				parameter->TypeName  = TypeName;
				parameter->PinTypeId = PinTypeName;
				return parameter;
			};
			GetFactoryMap()[TypeName] = info;
		}

		static ParameterTypeInfo* GetParameterTypeInfo(const String& TypeName)
		{
			if (!GetFactoryMap().Contains(TypeName)) return nullptr;
			return &GetFactoryMap()[TypeName];
		}

		static TOwningPointer<IParticleParameter> CreateParameter(const String& TypeName)
		{
			if (!GetFactoryMap().Contains(TypeName)) return nullptr;
			return GetFactoryMap()[TypeName].CTor();
		}
	};
}

#endif //PLUENGINE_PARTICLEPARAMETER_H
