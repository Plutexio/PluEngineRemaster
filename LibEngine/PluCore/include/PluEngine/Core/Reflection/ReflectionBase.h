//
// Created by Plutex on 1/3/26.
//

#ifndef PLUENGINE_REFLECTIONBASE_H
#define PLUENGINE_REFLECTIONBASE_H

#include <functional>
#include <optional>

#include <pybind11/pybind11.h>
#include "imgui.h"
#include "PluSTL/PluSTL_FWD.h"
#include "PluEngine/Core.h"
#include "PluBase/Log.h"

#include "ReflectionBase.h"
#include "PluEngine/PluTypes.h"
#include "PluEngine/Core/Events/EventDispatcher.h"
#include "PluEngine/Core/Widgets/SearchBar.h"

namespace Plu
{
	class SceneManager;
	class EngineAssetManager;
	struct ApplicationInfo;
	struct IAssetData;
	class PluUUID;
	struct PropertyInfo;
	class EngineObjectManager;
}

namespace Plu
{
	class IShaderManager;
	struct TypeInfo;

	struct DeserializationContext
	{
		TUsePointer<IShaderManager> shaderManager;
		TUsePointer<EngineAssetManager> assetManager;
		TUsePointer<SceneManager> scenesManager;
	};

	using SerializeFn   = nlohmann::json (*)(void* ptr);
	using DeserializeFn = void (*)(DeserializationContext* deserializationContext, const nlohmann::json& j, void* outValue);
	using EditorFn      = bool (*)(void* ptr, const String& name);

	using SetterFn      = void (*)(void* ptr, const void* buf);
	using GetterFn      = void (*)(void* ptr, void* buf);
	
	using CopyFn        = void (*)(const void* src, void* dst);

	using ConstructScratchFn = void* (*)();
	using DestroyScratchFn   = void  (*)(void* ptr);

	using FromPythonFn = void (*)(pybind11::handle source, void* outValue);
	using ToPythonFn = pybind11::object (*)(void* value);

	template<typename T> T FromString(const String& str);

	struct PLUCORE_API IPropertyInfo
	{
		String PropertyName;
		String PropertyTypeName;

		IPropertyInfo(String propertyName, String propertyTypeName);
		virtual ~IPropertyInfo() = default;

		[[nodiscard]] virtual JSON Serialize(void* objectInstance) const = 0;
		virtual void Deserialize(DeserializationContext* deserializationContext, const JSON& json, void* objectInstance) const = 0;
		virtual bool EditorControl(void* objectInstance, const String& label, TypeInfo* ownerType) const = 0;
		virtual void Copy(const void* srcInstance, void* dstInstance) const = 0;

		[[nodiscard]] virtual void* GetPtr(void* objectInstance) const { return nullptr; }
	};

	// A PLU_PROPERTY of a C++ type, emitted by the reflection generator.
	struct PLUCORE_API PropertyInfo : IPropertyInfo
	{
		UInt64 PropertyOffset;
		UInt64 PropertySize;

		SerializeFn   SerializePtr = nullptr;
		DeserializeFn DeserializePtr = nullptr;
		EditorFn      EditorControlPtr = nullptr;

		SetterFn SetterPtr = nullptr;
		GetterFn GetterPtr = nullptr;
		CopyFn   CopyPtr = nullptr;

		ConstructScratchFn ConstructScratch = nullptr;
		DestroyScratchFn   DestroyScratch = nullptr;

		TypeInfo* UuidForClass = nullptr;

		PropertyInfo(String propertyName, UInt64 propertyOffset, UInt64 propertySize, String propertyTypeName);

		[[nodiscard]] JSON Serialize(void* objectInstance) const override;
		void Deserialize(DeserializationContext* deserializationContext, const JSON& json, void* objectInstance) const override;
		bool EditorControl(void* objectInstance, const String& label, TypeInfo* ownerType) const override;
		void Copy(const void* srcInstance, void* dstInstance) const override;
		[[nodiscard]] void* GetPtr(void* objectInstance) const override;
	};

	struct PLUCORE_API PythonPropertyInfo : IPropertyInfo
	{
		pybind11::object Descriptor;
		pybind11::object ValueType;
		String Tooltip;
		std::optional<double> Min;
		std::optional<double> Max;

		PythonPropertyInfo(String propertyName, String propertyTypeName);
		~PythonPropertyInfo() override;

		[[nodiscard]] JSON Serialize(void* objectInstance) const override;
		void Deserialize(DeserializationContext* deserializationContext, const JSON& json, void* objectInstance) const override;
		bool EditorControl(void* objectInstance, const String& label, TypeInfo* ownerType) const override;
		void Copy(const void* srcInstance, void* dstInstance) const override;
	};

	PLUCORE_API void CopyReflectedProperties(TypeInfo* type, const void* src, void* dst);

	enum class TypeType
	{
		CLASS,
		STRUCT,
		ENUM,
		INTERFACE,
		UNKNOWN
	};

	struct PLUCORE_API PrimitiveTypeInfo
	{
		String TypeName;

		EditorFn EditorControlPtr = nullptr;
		SerializeFn SerializePtr = nullptr;
		DeserializeFn DeserializePtr = nullptr;

		ConstructScratchFn ConstructScratchPtr = nullptr;
		DestroyScratchFn DestroyScratchPtr = nullptr;

		FromPythonFn FromPythonPtr = nullptr;
		ToPythonFn ToPythonPtr = nullptr;
	};

	struct PLUCORE_API TypeInfo
	{
		using ConstructorFunc = std::function<void*()>;
		using SerializeJsonFunc = std::function<nlohmann::json(void*)>;
		using DeSerializeJsonFunc = std::function<void*(DeserializationContext*, const nlohmann::json&)>;

		UInt64 TypeSize;
		String TypeName;
		TypeType Type;
		TypeInfo* BaseType = nullptr; //We only support inheritance through single parent. I'm lazy :)
		PropertyInfo* TypeUuidProp = nullptr;
		[[nodiscard]] PropertyInfo* GetTypeUuidProp() const;

		DynamicArray<IPropertyInfo*> Properties;

		HashMap<String, IPropertyInfo*> PropertiesByName;
		UInt64 PropertiesByNameGeneration = 0;

		ConstructorFunc Constructor = [this]()->void* {
			String info = "Class is probably Abstract! No Constructor for ";
			info += TypeName;
			info += "! Try removing the Abstract property in PLU_CLASS().";
			PLU_CORE_ASSERT(false, info.CStr())
			return nullptr;
		};
		void AddProperty(IPropertyInfo* propertyInfo);
		// Deletes this type's own properties (not inherited ones). Used when a Python class is
		// reloaded and its PluEngine.Property list is collected again.
		void ClearProperties();
		[[nodiscard]] void* Construct() const;
		[[nodiscard]] IPropertyInfo* FindProperty(const String& propertyName);
		bool IsAbstract;

		bool IsPythonType = false;
		pybind11::type PythonType = pybind11::object();

		[[nodiscard]] bool IsChildOf(TypeInfo* potentialParent); //Base type is ?
		[[nodiscard]] bool IsDerivedOf(TypeInfo* potentialParent); //Can scan more types
		[[nodiscard]] bool IsDerivedOfOrSame(TypeInfo* potentialParent); //Can scan more types

		SerializeJsonFunc SerializeToJson = [this](void* obj)->nlohmann::json {
			String info = "No JSON serialization for type: ";
			info += TypeName;
			info += "! Try rerunning ReflectionGenerator.py";
			PLU_CORE_ASSERT(false, info.CStr())
			return {};
		};
		DeSerializeJsonFunc DeserializeFromJson = [this](DeserializationContext* dc, const nlohmann::json& j)->void* {
			String info = "No JSON deserialization for type: ";
			info += TypeName;
			info += "! Try rerunning ReflectionGenerator.py";
			PLU_CORE_ASSERT(false, info.CStr())
			return {};
		};
		nlohmann::json SerializeToJSON(void* obj) const;
		void* DeSerializeFromJSON(DeserializationContext* dc, const nlohmann::json &j) const;

		TypeInfo(UInt64 size, String typeName, TypeType type);
		~TypeInfo();
	};

	struct PLUCORE_API EnumValue
	{
		String ValueName;
		UInt64 Value;
	};

	struct PLUCORE_API EnumInfo
	{
		String EnumName;
		DynamicArray<EnumValue*> EnumValues;
		UInt64 EnumIntSize = 0;

		EnumInfo(String enumName, UInt64 intSize) : EnumName(enumName), EnumIntSize(intSize) {};

		void AddValue(String enumName, UInt64 value);
	};

	class PLUCORE_API TypeRegistry
	{
		HashMap<String, TypeInfo*> mTypeMap;
		HashMap<String, EnumInfo*> mEnumMap;
		HashMap<String, PrimitiveTypeInfo*> mPrimitiveTypeMap;
		TUsePointer<EngineObjectManager> mObjectManager;
		TUsePointer<EngineAssetManager> mAssetManager;
		friend class Application;
	public:
		std::function<TUsePointer<IAssetData>(DeserializationContext*, const PluUUID&)> assetDataResolver;
		std::function<bool(void*, String, TypeInfo*)> assetPointerControl;
		std::function<bool(void*, String, TypeInfo*, PropertyInfo*)> assetUuidControl;
		std::function<bool(TypeInfo*, void*)> editorControlForTypeInfo;
		std::function<void(DeserializationContext*, const JSON&, TypeInfo*, void*)> deserializeForTypeInfo;
		std::function<JSON(TypeInfo*, void*)> serializeForTypeInfo;
		TUsePointer<EngineObjectManager> GetObjectManager();
		TUsePointer<EngineAssetManager> GetAssetManager();
		static TypeRegistry* GetInstance();
		void AddType(TypeInfo* typeInfo);
		TypeInfo* GetTypeOfName(const String& typeName);
		HashMap<String, TypeInfo*>* GetTypeMap();

		EventDispatcher TypeRegistryEventDispatcher;

		void Cleanup();

		template<typename T>
		requires std::is_enum_v<T>
		void AddEnum(EnumInfo* enumInfo)
		{
			mEnumMap.Insert(typeid(T).name(), enumInfo);
		}

		template<typename T>
		requires std::is_enum_v<T>
		EnumInfo* GetEnumByT()
		{
			return mEnumMap[typeid(T).name()];
		}

		template<typename T>
		void AddPrimitiveType(const String& name);

		PrimitiveTypeInfo* FindPrimitiveByName(const String& name);
	};

	String PLUCORE_API GetTypeNameFromPython(pybind11::type type);

	PLU_FUNCTION()
	void PLUCORE_API RegisterPluClass(pybind11::type type);

	void PLUCORE_API GatherPythonProperties(const pybind11::type& pythonType, TypeInfo* type);

	template<typename T>
	struct TypeSerializer
	{
		static nlohmann::json Serialize(void* dataToSerialize)
		{
			if constexpr (std::is_pointer_v<T>) {
				return {"NO POINTER SERIALIZATION"};
			} else if constexpr (std::is_enum_v<T>) {
				const UInt64 value = static_cast<UInt64>(*static_cast<T*>(dataToSerialize));
				EnumInfo* enumInfo = TypeRegistry::GetInstance()->GetEnumByT<T>();
				if (enumInfo) {
					for (EnumValue* ev : enumInfo->EnumValues) {
						if (ev->Value == value) {
							return ev->ValueName.CStr();
						}
					}
				}
				return value;
			} else {
				if (TypeRegistry::GetInstance()->serializeForTypeInfo) {
					return TypeRegistry::GetInstance()->serializeForTypeInfo(T::GetStaticClass(), dataToSerialize);
				} else {
					return {"NO TYPE SERIALIZATION"};
				}
			}
		}

		static void Deserialize(DeserializationContext* deserializationContext, const nlohmann::json& json, void* outValue)
		{
			if constexpr (std::is_pointer_v<T>) {
				PLU_CORE_ERROR("NO POINTER DESERIALIZATION!");
			} else if constexpr (std::is_enum_v<T>) {
				EnumInfo* enumInfo = TypeRegistry::GetInstance()->GetEnumByT<T>();
				if (json.is_string()) {
					const String name = json.get<std::string>().c_str();
					if (enumInfo) {
						for (EnumValue* ev : enumInfo->EnumValues) {
							if (ev->ValueName == name) {
								*static_cast<T*>(outValue) = static_cast<T>(ev->Value);
								return;
							}
						}
					}
					PLU_CORE_ERROR("Unknown enum value '{}' during deserialization", name.CStr());
				} else if (json.is_number()) {
					// Legacy / fallback numeric form.
					*static_cast<T*>(outValue) = static_cast<T>(json.get<UInt64>());
				} else {
					PLU_CORE_ERROR("Enum JSON is neither a string nor a number!");
				}
			} else {
				if (TypeRegistry::GetInstance()->deserializeForTypeInfo) {
					TypeRegistry::GetInstance()->deserializeForTypeInfo(deserializationContext, json, T::GetStaticClass(), outValue);
				} else {
					PLU_CORE_ERROR("NO TYPE DESERIALIZATION! ({})", T::GetStaticClass()->TypeName.CStr());
				}
			}
		}

		static bool EditorControl(void* value, const String& name)
		{
			if constexpr (std::is_pointer_v<T>) {

			} else {
				if constexpr (std::is_enum_v<T>) {
					EnumValue* enumValue = nullptr;
					EnumInfo* enumInfo = TypeRegistry::GetInstance()->GetEnumByT<T>();
					if (!enumInfo) return false;

					UInt64 currentValue = 0;
					switch (enumInfo->EnumIntSize) {
						case 1: currentValue = *static_cast<UInt8*>(value);  break;
						case 2: currentValue = *static_cast<UInt16*>(value); break;
						case 4: currentValue = *static_cast<UInt32*>(value); break;
						case 8: currentValue = *static_cast<UInt64*>(value); break;
						default:
							PLU_CORE_ERROR("Not supported Enum Int Size with {}", enumInfo->EnumIntSize);
							return false;
					}
					for (EnumValue *valueItr: enumInfo->EnumValues) {
						if (valueItr->Value == currentValue) {
							enumValue = valueItr;
							break;
						}
					}
					const char* previewLabel = enumValue ? enumValue->ValueName.CStr() : "<invalid>";
					if (ImGui::BeginCombo(name.CStr(), previewLabel, ImGuiComboFlags_WidthFitPreview))
					{
						static ImGuiTextFilter filter;
						ImGuiWidgets::PopupSearchBar(&filter);

						bool changed = false;

						for (int n = 0; n < enumInfo->EnumValues.Size(); n++)
						{
							const bool is_selected = enumValue == enumInfo->EnumValues[n];
							if (filter.PassFilter(enumInfo->EnumValues[n]->ValueName.CStr()))
								if (ImGui::Selectable(enumInfo->EnumValues[n]->ValueName.CStr(), is_selected)) {
									*static_cast<T*>(value) = static_cast<T>(enumInfo->EnumValues.At(n)->Value);
									changed = true;
								}
						}
						ImGui::EndCombo();
						return changed;
					}
				} else {
					if (TypeRegistry::GetInstance()->editorControlForTypeInfo) {
						bool changed = false;
						if (ImGui::TreeNode(name.CStr())) {
							changed = TypeRegistry::GetInstance()->editorControlForTypeInfo(T::GetStaticClass(), value);
							ImGui::TreePop();
						}
						return changed;
					} else {
						ImGui::Text("Unsupported type %s!", T::GetStaticClass()->TypeName.CStr());
					}
				}
			}
			return false;
		}
	};

	template<typename T>
	void TypeRegistry::AddPrimitiveType(const String& name)
	{
		PrimitiveTypeInfo* typeInfo = new PrimitiveTypeInfo();
		typeInfo->TypeName = name;
		typeInfo->EditorControlPtr = TypeSerializer<T>::EditorControl;
		typeInfo->DeserializePtr = TypeSerializer<T>::Deserialize;
		typeInfo->SerializePtr = TypeSerializer<T>::Serialize;

		typeInfo->ConstructScratchPtr = []() -> void* {return new T();};
		typeInfo->DestroyScratchPtr = [](void* p) {delete static_cast<T*>(p);};

		typeInfo->ToPythonPtr = [](void* value) -> pybind11::object {return pybind11::cast(*static_cast<T*>(value));};
		typeInfo->FromPythonPtr = [](pybind11::handle source, void* outValue) {*static_cast<T*>(outValue) = source.cast<T>();};

		mPrimitiveTypeMap.Insert(name, typeInfo);
	}

#define REFLECTION_PRIMITIVE_TYPE(x) TypeRegistry::GetInstance()->AddPrimitiveType<x>(#x);
}

#endif //PLUENGINE_REFLECTIONBASE_H