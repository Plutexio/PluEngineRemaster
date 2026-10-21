//
// Created by Plutex on 1/3/26.
//

#ifndef PLUENGINE_REFLECTIONBASE_H
#define PLUENGINE_REFLECTIONBASE_H

#include <functional>

#include "imgui.h"
#include "json.hpp"
#include "json_fwd.hpp"
#include "PluSTL/PluSTL_FWD.h"
#include "PluEngine/Core.h"
#include "PluBase/Log.h"
#pragma warning(push, 0)
#include "pybind11/pybind11.h"
#pragma warning(pop)

#include "ReflectionBase.h"
#include "PluEngine/PluTypes.h"
#include "PluEngine/Core/Events/EventDispatcher.h"

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

	// Typed field-to-field assignment, used to duplicate an object without going through JSON.
	// A no-op for property types that are not copy-assignable (the generator guards it), so a
	// clone leaves such a field at whatever the freshly constructed object put there.
	using CopyFn        = void (*)(const void* src, void* dst);

	// Lifecycle for a heap scratch instance of the property's type. Only needed
	// (and only emitted) for accessor-based properties, where the editor must
	// hand the getter/setter a fully constructed T to read/write through.
	using ConstructScratchFn = void* (*)();
	using DestroyScratchFn   = void  (*)(void* ptr);

	template<typename T> T FromString(const String& str);

	struct PLUCORE_API PropertyInfo
	{
		String PropertyName;
		UInt64 PropertyOffset;
		UInt64 PropertySize;
		String PropertyTypeName;

		SerializeFn   SerializePtr;
		DeserializeFn DeserializePtr;
		EditorFn      EditorControlPtr;

		SetterFn SetterPtr;
		GetterFn GetterPtr;
		CopyFn   CopyPtr = nullptr;

		ConstructScratchFn ConstructScratch = nullptr;
		DestroyScratchFn   DestroyScratch = nullptr;

		bool IsPersistent = true;
		bool IsVisibleInEditor = false;

		TypeInfo* UuidForClass = nullptr;

		void* GetPtr(void* objectInstance) const;
	};

	// Copies every reflected property of `type` (own and inherited) from src to dst, field to field.
	// Both must really be instances of `type`.
	//
	// This is the JSON-free path used to duplicate scene objects: it does the same job as
	// serialize-then-deserialize, without building a JSON DOM, allocating key strings or looking any
	// property up by name.
	//
	// Pointer-valued properties are copied shallowly, which is correct for the ones that exist today
	// — asset handles (TUsePointer<StaticMesh>, <Skeleton>, …) and TClassPointer are meant to be
	// shared between a source and its copy. A property pointing at another object *of the same
	// scene* would need remapping to the copy's counterpart; there is no such reflected property in
	// the engine right now, so if you add one, teach this function about it.
	PLUCORE_API void CopyReflectedProperties(TypeInfo* type, const void* src, void* dst);

	enum class TypeType
	{
		CLASS,
		STRUCT,
		ENUM,
		INTERFACE,
		UNKNOWN
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

		DynamicArray<PropertyInfo*> Properties;

		// Flattened name -> property lookup across the whole inheritance chain, built lazily on the
		// first FindProperty. Deserialization does one lookup per field per object, so a linear scan
		// up the hierarchy (the old behaviour) made scene loading O(fields * properties) in string
		// comparisons. Rebuilt whenever any type gains a property — a base type gaining one changes
		// the flattened set of every type below it, so the validity stamp is registry-global rather
		// than per-type.
		HashMap<String, PropertyInfo*> PropertiesByName;
		UInt64 PropertiesByNameGeneration = 0;

		ConstructorFunc Constructor = [this]()->void* {
			String info = "Class is probably Abstract! No Constructor for ";
			info += TypeName;
			info += "! Try removing the Abstract property in PLU_CLASS().";
			PLU_CORE_ASSERT(false, info.CStr())
			return nullptr;
		};
		void AddProperty(PropertyInfo* propertyInfo);
		[[nodiscard]] void* Construct() const;
		[[nodiscard]] PropertyInfo* FindProperty(const String& propertyName);
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
		// The two subsystems reflection itself needs, held directly rather than through
		// ApplicationInfo: that struct lives in the application layer, above everything, so naming
		// its type here would make the bottom of the stack depend on the top. Application sets both.
		TUsePointer<EngineObjectManager> mObjectManager;
		TUsePointer<EngineAssetManager> mAssetManager;
		friend class Application;
	public:
		// Installed by the asset layer at startup (InstallAssetReflectionHooks). Reflection sits
		// below the asset system, so it reaches it through these instead of naming
		// EngineAssetManager — which would make the bottom of the stack depend on a layer above it.
		// All three are null in a build without the asset layer; every caller checks.
		std::function<TUsePointer<IAssetData>(DeserializationContext*, const PluUUID&)> assetDataResolver;
		std::function<bool(void*, String, TypeInfo*)> assetPointerControl;
		std::function<bool(void*, String, TypeInfo*, PropertyInfo*)> assetUuidControl;

		// Bridges to TypeSerializer<TypeInfo*>::EditorControl (TypeTraits.h) without ReflectionBase.h
		// including that much heavier header back (ImGui/pybind11 type casters/physics channels) —
		// wired once from editor-only startup code (see EditorApp::OnInit). Returns whether anything
		// changed, same contract as every other EditorControl, so nested struct/class edits still
		// propagate "changed" up to callers that dirty-mark assets on it.
		std::function<bool(TypeInfo*, void*)> editorControlForTypeInfo;
		// Deserializes INTO the existing instance (the field inside the owning object). It must not
		// construct a new one: TypeSerializer<T>::Deserialize only has the field's address, so a
		// returned object could never reach the field — it was silently dropped and leaked.
		std::function<void(DeserializationContext*, const JSON&, TypeInfo*, void*)> deserializeForTypeInfo;
		std::function<JSON(TypeInfo*, void*)> serializeForTypeInfo;
		TUsePointer<EngineObjectManager> GetObjectManager();
		TUsePointer<EngineAssetManager> GetAssetManager();
		static TypeRegistry* GetInstance();
		void AddType(TypeInfo* typeInfo);
		TypeInfo* GetTypeOfName(const String& typeName);
		HashMap<String, TypeInfo*>* GetTypeMap();

		EventDispatcher TypeRegistryEventDispatcher;

		// Frees every registered TypeInfo/PropertyInfo/EnumInfo. Call once at the very end of
		// engine shutdown — nothing may touch reflection (or objects using it) afterwards.
		void Cleanup();

		template<typename T>
		requires std::is_enum_v<T>
		void AddEnum(EnumInfo* enumInfo)
		{
			PLU_CORE_TRACE("Enum {} added to global TypeRegistry", enumInfo->EnumName.CStr());
			mEnumMap.Insert(typeid(T).name(), enumInfo);
		}

		template<typename T>
		requires std::is_enum_v<T>
		EnumInfo* GetEnumByT()
		{
			return mEnumMap[typeid(T).name()];
		}
	};

	String PLUCORE_API GetTypeNameFromPython(pybind11::type type);

	PLU_FUNCTION()
	void PLUCORE_API RegisterPluClass(pybind11::type type);

	template<typename T>
	struct TypeSerializer
	{
		static nlohmann::json Serialize(void* dataToSerialize)
		{
			if constexpr (std::is_pointer_v<T>) {
				return {"NO POINTER SERIALIZATION"};
			} else if constexpr (std::is_enum_v<T>) {
				// Serialise enums by their value NAME (refactor-stable: survives
				// reordering / inserting enumerators). The name<->value mapping
				// comes from the runtime EnumInfo registry, so this works in every
				// translation unit regardless of where the enum is declared.
				const UInt64 value = static_cast<UInt64>(*static_cast<T*>(dataToSerialize));
				EnumInfo* enumInfo = TypeRegistry::GetInstance()->GetEnumByT<T>();
				if (enumInfo) {
					for (EnumValue* ev : enumInfo->EnumValues) {
						if (ev->Value == value) {
							return ev->ValueName.CStr();
						}
					}
				}
				// Unknown / unregistered value — fall back to the raw integer so no
				// data is lost (deserialization accepts numbers too).
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

					// EnumIntSize is the size of the underlying type in BYTES
					// (generator emits sizeof(BaseType)). Read exactly that many.
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
					// enumValue can be null when the stored value is not a named
					// enumerator (uninitialised data, bit flags, …) — never deref it.
					const char* previewLabel = enumValue ? enumValue->ValueName.CStr() : "<invalid>";
					if (ImGui::BeginCombo(name.CStr(), previewLabel, ImGuiComboFlags_WidthFitPreview))
					{
						static ImGuiTextFilter filter;
						if (ImGui::IsWindowAppearing())
						{
							ImGui::SetKeyboardFocusHere();
							filter.Clear();
						}
						ImGui::SetNextItemShortcut(ImGuiMod_Ctrl | ImGuiKey_F);
						filter.Draw("##Filter", -FLT_MIN);

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
}

#endif //PLUENGINE_REFLECTIONBASE_H