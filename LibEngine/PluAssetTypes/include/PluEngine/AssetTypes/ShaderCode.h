//
// Created by Plutex on 1/19/26.
//

#ifndef PLUENGINE_SHADERCODE_H
#define PLUENGINE_SHADERCODE_H
#include "PluEngine/Core/Objects/EngineObject.h"
#include "ShaderCode.generated.h"
#include "PluEngine/PluUUID.h"
#include "glm/vec2.hpp"
#include "glm/vec3.hpp"
#include "glm/vec4.hpp"
#include "glm/fwd.hpp"
#include "PluEngine/AssetTypes/Texture/Texture.h"
#include "PluEngine/Core/Reflection/TypeTraits.h"

namespace Plu
{
	PLU_STRUCT()
	struct PLUASSETTYPES_API IShaderUniform
	{
		REFLECTION_BODY_ISHADERUNIFORM()

		// Uniforms live in DynamicArray<TOwningPointer<IShaderUniform>> and are deleted through
		// this base; ShaderUniform<T>::Data (textures, strings) needs the derived destructor.
		virtual ~IShaderUniform() = default;

		PLU_PROPERTY()
		String Name;

		PLU_PROPERTY()
		String Type;

		PLU_PROPERTY()
		int ArraySize;
	};

	template<typename T>
	struct PLUASSETTYPES_API ShaderUniform : IShaderUniform
	{
		T Data;
	};

	template<>
	struct TypeSerializer<IShaderUniform>
	{
		static nlohmann::json Serialize(void* dataToSerialize)
		{
			IShaderUniform* data = static_cast<IShaderUniform *>(dataToSerialize);
			nlohmann::json j = {};
			if (data->ArraySize == 0) {
				if (data->Type == "int") {
					j = TypeSerializer<int>::Serialize(&static_cast<ShaderUniform<int>*>(data)->Data);
				} else if (data->Type == "float") {
					j = TypeSerializer<float>::Serialize(&static_cast<ShaderUniform<float>*>(data)->Data);
				} else if (data->Type == "bool") {
					j = TypeSerializer<bool>::Serialize(&static_cast<ShaderUniform<bool>*>(data)->Data);
				} else if (data->Type == "vec2") {
					j = TypeSerializer<glm::vec2>::Serialize(&static_cast<ShaderUniform<glm::vec2>*>(data)->Data);
				} else if (data->Type == "vec3") {
					j = TypeSerializer<glm::vec3>::Serialize(&static_cast<ShaderUniform<glm::vec3>*>(data)->Data);
				} else if (data->Type == "vec4") {
					j = TypeSerializer<glm::vec4>::Serialize(&static_cast<ShaderUniform<glm::vec4>*>(data)->Data);
				} else if (data->Type == "sampler2D") {
					TUsePointer<TextureInfo> texture = static_cast<ShaderUniform<TUsePointer<TextureInfo>>*>(data)->Data;
					j = TypeSerializer<TUsePointer<TextureInfo>>::Serialize(&texture);
				}
				return {
					{"name", data->Name.CStr()},
					{"type", data->Type.CStr()},
					{"value", j}
				};
			}
			return {};
		}
		static void Deserialize(DeserializationContext* deserializationContext, const nlohmann::json& json, void* outValue)
		{
			if (json.is_null()) return;
			String type = json["type"].get<std::string>().c_str();
			String name = json["name"].get<std::string>().c_str();
			if (type == "int") {
				ShaderUniform<int>* uniform = new ShaderUniform<int>();
				uniform->Name = name;
				uniform->Type = type;
				TypeSerializer<int>::Deserialize(deserializationContext, json["value"], &uniform->Data);
				*static_cast<TOwningPointer<IShaderUniform>*>(outValue) = TOwningPointer(uniform);
			} else if (type == "float") {
				ShaderUniform<float>* uniform = new ShaderUniform<float>();
				uniform->Name = name;
				uniform->Type = type;
				TypeSerializer<float>::Deserialize(deserializationContext, json["value"], &uniform->Data);
				*static_cast<TOwningPointer<IShaderUniform>*>(outValue) = TOwningPointer(uniform);
			} else if (type == "vec2") {
				ShaderUniform<Vec2>* uniform = new ShaderUniform<Vec2>();
				uniform->Name = name;
				uniform->Type = type;
				TypeSerializer<Vec2>::Deserialize(deserializationContext, json["value"], &uniform->Data);
				*static_cast<TOwningPointer<IShaderUniform>*>(outValue) = TOwningPointer(uniform);
			} else if (type == "vec3") {
				ShaderUniform<Vec3>* uniform = new ShaderUniform<Vec3>();
				uniform->Name = name;
				uniform->Type = type;
				TypeSerializer<Vec3>::Deserialize(deserializationContext, json["value"], &uniform->Data);
				*static_cast<TOwningPointer<IShaderUniform>*>(outValue) = TOwningPointer(uniform);
			} else if (type == "vec4") {
				ShaderUniform<Vec4>* uniform = new ShaderUniform<Vec4>();
				uniform->Name = name;
				uniform->Type = type;
				TypeSerializer<Vec4>::Deserialize(deserializationContext, json["value"], &uniform->Data);
				*static_cast<TOwningPointer<IShaderUniform>*>(outValue) = TOwningPointer(uniform);
			} else if (type == "bool") {
				ShaderUniform<bool>* uniform = new ShaderUniform<bool>();
				uniform->Name = name;
				uniform->Type = type;
				TypeSerializer<bool>::Deserialize(deserializationContext, json["value"], &uniform->Data);
				*static_cast<TOwningPointer<IShaderUniform>*>(outValue) = TOwningPointer(uniform);
			} else if (type == "sampler2D") {
				ShaderUniform<TUsePointer<TextureInfo>>* uniform = new ShaderUniform<TUsePointer<TextureInfo>>();
				uniform->Name = name;
				uniform->Type = type;
				TypeSerializer<TUsePointer<TextureInfo>>::Deserialize(deserializationContext, json["value"], &uniform->Data);
				*static_cast<TOwningPointer<IShaderUniform>*>(outValue) = TOwningPointer(uniform);
			}
		}
		static bool EditorControl(void* value, const String& name)
		{
			return false;
		}
	};

	//Runtime and Editor specific shader code loader
	PLU_CLASS(Abstract, UUID=Uuid)
	class PLUASSETTYPES_API IShaderCode : public EngineObject
	{
		REFLECTION_BODY_ISHADERCODE()
	protected:
		void HandleLineUniforms(String line);
		DynamicArray<TOwningPointer<IShaderUniform>> mUniforms;
	public:
		PLU_PROPERTY()
		PluUUID Uuid;

		PLU_PROPERTY()
		String Name;

		virtual String GetCode() = 0;
		virtual DynamicArray<TOwningPointer<IShaderUniform>>* GetCodeUniforms() = 0;
		virtual void RenewUniforms() = 0;
	};
}

#endif //PLUENGINE_SHADERCODE_H
