//
// Created by Plutex on 2026-01-21.
//

#ifndef PLUENGINE_TYPETRAITS_H
#define PLUENGINE_TYPETRAITS_H

#include "ReflectionBase.h"
#include "imgui_stdlib.h"
#include "PluEngine/PluTypes.h"
#include "PluEngine/PluUUID.h"
#include "PluEngine/Timer.h"
#include "PluEngine/Core/Objects/EngineObjectManager.h"
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <pybind11/pybind11.h>

#include "ClassPointer.h"
#include "PluEngine/Core/IAssetData.h"
#include "PluEngine/Core/CollisionChannels.h"

namespace Plu
{
	// Resolves a serialized asset UUID through TypeRegistry::assetDataResolver. Declared here and
	// dispatched in TypeTraits.cpp so this header never has to name the asset manager.
	PLUCORE_API TUsePointer<IAssetData> ResolveAssetData(DeserializationContext* dc, const PluUUID& uuid);

	PLUCORE_API bool TUsePointerAssetUI(void* value, String name, TypeInfo* typeInfo);
	PLUCORE_API bool UUIDForAssetUI(void* value, String name, TypeInfo* typeInfo, PropertyInfo* propertyInfo);
	// Preset dropdown for CollisionProfileRef (editor-only; defined in TypeTraits.cpp).
	PLUCORE_API bool CollisionProfileRefEditorControl(void* value, const String& name);
	// Class picker for TClassPointer<T> (editor-only; defined in TypeTraits.cpp): a combo whose popup is
	// ImGuiWidgets::TypeTree rooted at `baseType`. Writes `*type` and returns true only on a confirmed
	// change. Type-erased so the tree widget stays out of this header.
	PLUCORE_API bool ClassPointerEditorControl(TypeInfo** type, TypeInfo* baseType, const String& name);

	// Generic chunked-array editor widget (editor-only; defined in TypeTraits.cpp), used by
	// TypeSerializer<DynamicArray<T>>::EditorControl below. Type-erased on purpose so the actual
	// ImGui tree-building logic doesn't have to live in the header as a template: `arrayId` seeds
	// ImGui's ID stack (the array's address — keeps sibling arrays of the same element type from
	// colliding), `renderElement`/`addElement`/`removeElement` are the only bits that need to know T.
	// Root node shows "name (count)"; elements are grouped into collapsible chunks of 10 so arrays
	// with hundreds/thousands of entries (foliage, ISMC instances) don't dump one flat unscrollable
	// wall of tree nodes into the details panel.
	PLUCORE_API bool ArrayTreeEditorControl(void* arrayId, const String& name, UInt64 count,
	                                     const std::function<bool(UInt64 index)>& renderElement,
	                                     const std::function<void()>& addElement,
	                                     const std::function<void(UInt64 index)>& removeElement);

	template <>
	struct TypeSerializer<int>
	{
		static nlohmann::json Serialize(void* dataToSerialize) { return *static_cast<int*>(dataToSerialize); }
		static void Deserialize(DeserializationContext*, const nlohmann::json& json, void* outValue) { *static_cast<int*>(outValue) = json.get<int>(); }
		static bool EditorControl(void* value, const String& name) { return ImGui::DragInt(name.CStr(), static_cast<int*>(value)); }
	};

	template <>
	struct TypeSerializer<bool>
	{
		static nlohmann::json Serialize(void* dataToSerialize)
		{
			return *static_cast<bool*>(dataToSerialize);
		}

		static void Deserialize(DeserializationContext*, const nlohmann::json& json, void* outValue)
		{
			*static_cast<bool*>(outValue) = json.get<bool>();
		}

		static bool EditorControl(void* value, const String& name)
		{
			return ImGui::Checkbox(name.CStr(), static_cast<bool*>(value));
		}
	};

	template <>
	struct TypeSerializer<Int8>
	{
		static nlohmann::json Serialize(void* dataToSerialize)
		{
			return  *static_cast<Int8*>(dataToSerialize);
		}

		static void Deserialize(DeserializationContext*, const nlohmann::json& json, void* outValue)
		{
			*static_cast<Int8*>(outValue) = json.get<Int8>();
		}

		static bool EditorControl(void* value, const String& name)
		{
			int v = *static_cast<Int8*>(value);
			if (ImGui::DragInt(name.CStr(), &v, 1, INT8_MIN, INT8_MAX)) {
				*static_cast<Int8*>(value) = static_cast<Int8>(v);
				return true;
			}
			return false;
		}
	};

	template <>
	struct TypeSerializer<Int16>
	{
		static nlohmann::json Serialize(void* dataToSerialize)
		{
			return *static_cast<Int16*>(dataToSerialize);
		}

		static void Deserialize(DeserializationContext*, const nlohmann::json& json, void* outValue)
		{
			*static_cast<Int16*>(outValue) = json.get<Int16>();
		}

		static bool EditorControl(void* value, const String& name)
		{
			int v = *static_cast<Int16*>(value);
			if (ImGui::DragInt(name.CStr(), &v, 1, INT16_MIN, INT16_MAX)) {
				*static_cast<Int16*>(value) = static_cast<Int16>(v);
				return true;
			}
			return false;
		}
	};

	template <>
	struct TypeSerializer<Int64>
	{
		static nlohmann::json Serialize(void* dataToSerialize)
		{
			return *static_cast<Int64*>(dataToSerialize);
		}

		static void Deserialize(DeserializationContext*, const nlohmann::json& json, void* outValue)
		{
			*static_cast<Int64*>(outValue) = json.get<Int64>();
		}

		static bool EditorControl(void* value, const String& name)
		{
			return ImGui::DragScalar(
				name.CStr(),
				ImGuiDataType_S64,
				value
			);
		}
	};

	template <>
	struct TypeSerializer<UInt8>
	{
		static nlohmann::json Serialize(void* dataToSerialize)
		{
			return *static_cast<UInt8*>(dataToSerialize);
		}

		static void Deserialize(DeserializationContext*, const nlohmann::json& json, void* outValue)
		{
			*static_cast<UInt8*>(outValue) = json.get<UInt8>();
		}

		static bool EditorControl(void* value, const String& name)
		{
			int v = *static_cast<UInt8*>(value);
			if (ImGui::DragInt(name.CStr(), &v, 1, 0, UINT8_MAX)) {
				*static_cast<UInt8*>(value) = static_cast<UInt8>(v);
				return true;
			}
			return false;
		}
	};

	template <>
	struct TypeSerializer<UInt16>
	{
		static nlohmann::json Serialize(void* dataToSerialize)
		{
			return  *static_cast<UInt16*>(dataToSerialize);
		}

		static void Deserialize(DeserializationContext*, const nlohmann::json& json, void* outValue)
		{
			*static_cast<UInt16*>(outValue) = json.get<UInt16>();
		}

		static bool EditorControl(void* value, const String& name)
		{
			int v = *static_cast<UInt16*>(value);
			if (ImGui::DragInt(name.CStr(), &v, 1, 0, UINT16_MAX)) {
				*static_cast<UInt16*>(value) = static_cast<UInt16>(v);
				return true;
			}
			return false;
		}
	};

	template <>
	struct TypeSerializer<UInt32>
	{
		static nlohmann::json Serialize(void* dataToSerialize)
		{
			return  *static_cast<UInt32*>(dataToSerialize);
		}

		static void Deserialize(DeserializationContext*, const nlohmann::json& json, void* outValue)
		{
			*static_cast<UInt32*>(outValue) = json.get<UInt32>();
		}

		static bool EditorControl(void* value, const String& name)
		{
			return ImGui::DragScalar(
				name.CStr(),
				ImGuiDataType_U32,
				value
			);
		}
	};

	template <>
	struct TypeSerializer<UInt64>
	{
		static nlohmann::json Serialize(void* dataToSerialize)
		{
			return *static_cast<UInt64*>(dataToSerialize);
		}

		static void Deserialize(DeserializationContext*, const nlohmann::json& json, void* outValue)
		{
			*static_cast<UInt64*>(outValue) = json.get<UInt64>();
		}

		static bool EditorControl(void* value, const String& name)
		{
			return ImGui::DragScalar(
				name.CStr(),
				ImGuiDataType_U64,
				value
			);
		}
	};

	template <>
	struct TypeSerializer<float>
	{
		static nlohmann::json Serialize(void* dataToSerialize)
		{
			return *static_cast<float*>(dataToSerialize) ;
		}

		static void Deserialize(DeserializationContext*, const nlohmann::json& json, void* outValue)
		{
			*static_cast<float*>(outValue) = json.get<float>();
		}

		static bool EditorControl(void* value, const String& name)
		{
			return ImGui::DragFloat(name.CStr(), static_cast<float*>(value), 0.1f);
		}
	};

	template <>
	struct TypeSerializer<double>
	{
		static nlohmann::json Serialize(void* dataToSerialize)
		{
			return *static_cast<double*>(dataToSerialize);
		}

		static void Deserialize(DeserializationContext*, const nlohmann::json& json, void* outValue)
		{
			*static_cast<double*>(outValue) = json.get<double>();
		}

		static bool EditorControl(void* value, const String& name)
		{
			return ImGui::DragScalar(
				name.CStr(),
				ImGuiDataType_Double,
				value,
				0.1f
			);
		}
	};

	template <>
	struct TypeSerializer<String>
	{
		static nlohmann::json Serialize(void* dataToSerialize) { return static_cast<String*>(dataToSerialize)->CStr(); }
		static void Deserialize(DeserializationContext*, const nlohmann::json& json, void* outValue) { *static_cast<String*>(outValue) = json.get<std::string>().c_str(); }
		static bool EditorControl(void* value, const String& name)
		{
			std::string str = std::string(static_cast<String *>(value)->CStr());
			if (ImGui::InputText(name.CStr(), &str)) {
				*static_cast<String *>(value) = str.c_str();
				return true;
			}
			return false;
		}
	};

	// CollisionProfileRef serializes as just the preset name; the editor renders a dropdown of
	// the project's presets (logic in TypeTraits.cpp, editor-only).
	template <>
	struct TypeSerializer<CollisionProfileRef>
	{
		static nlohmann::json Serialize(void* value) { return static_cast<CollisionProfileRef*>(value)->Name.CStr(); }
		static void Deserialize(DeserializationContext*, const nlohmann::json& json, void* outValue)
		{
			if (json.is_string())
				static_cast<CollisionProfileRef*>(outValue)->Name = json.get<std::string>().c_str();
		}
		static bool EditorControl(void* value, const String& name)
		{
#ifdef PLU_ENGINE_EDITOR_BUILD
			return CollisionProfileRefEditorControl(value, name);
#else
			ImGui::Text("No Editor Utils in engine! Cannot show collision preset UI");
			return false;
#endif
		}
	};

	template <>
	struct TypeSerializer<Path>
	{
		static nlohmann::json Serialize(void* dataToSerialize) { return static_cast<Path*>(dataToSerialize)->CStr(); }
		static void Deserialize(DeserializationContext*, const nlohmann::json& json, void* outValue) { *static_cast<Path*>(outValue) = json.get<std::string>().c_str(); }
		static bool EditorControl(void* value, const String& name)
		{
			std::string str = std::string(static_cast<Path *>(value)->CStr());
			if (ImGui::InputText(name.CStr(), &str)) {
				*static_cast<Path *>(value) = str.c_str();
				return true;
			}
			return false;
		}
	};

	template <>
	struct TypeSerializer<StringW>
	{
		static nlohmann::json Serialize(void* dataToSerialize) { return static_cast<StringW*>(dataToSerialize)->CStr(); }
		static void Deserialize(DeserializationContext*, const nlohmann::json& json, void* outValue) { *static_cast<StringW*>(outValue) = json.get<std::wstring>().c_str(); }
		static bool EditorControl(void* value, const String& name)
		{
			std::string str = std::string(static_cast<StringW *>(value)->ToNarrow().CStr());
			if (ImGui::InputText(name.CStr(), &str)) {
				*static_cast<StringW *>(value) = StringW::FromNarrow(str.c_str());
				return true;
			}
			return false;
		}
	};

	template <>
	struct TypeSerializer<PathW>
	{
		static nlohmann::json Serialize(void* dataToSerialize) { return static_cast<PathW*>(dataToSerialize)->CStr(); }
		static void Deserialize(DeserializationContext*, const nlohmann::json& json, void* outValue) { *static_cast<PathW*>(outValue) = json.get<std::wstring>().c_str(); }
		static bool EditorControl(void* value, const String& name)
		{
			std::string str = std::string(static_cast<PathW *>(value)->ToString().ToNarrow().CStr());
			if (ImGui::InputText(name.CStr(), &str)) {
				*static_cast<PathW *>(value) = StringW::FromNarrow(str.c_str());
				return true;
			}
			return false;
		}
	};

	template<>
	struct TypeSerializer<PluUUID>
	{
		static nlohmann::json Serialize(void* dataToSerialize) { return static_cast<PluUUID*>(dataToSerialize)->getUUID(); }
		static void Deserialize(DeserializationContext*, const nlohmann::json& json, void* outValue)
		{
			*static_cast<PluUUID*>(outValue) = json.get<UInt64>();
		}
		static bool EditorControl(void* value, const String&)
		{
			return false;
		}
	};

	template<typename T>
	struct TypeSerializer<DynamicArray<T>>
	{
		static nlohmann::json Serialize(void* dataToSerialize)
		{
			nlohmann::json json = nlohmann::json::array();
			DynamicArray<T>* array = static_cast<DynamicArray<T> *>(dataToSerialize);
			for (T& item : *array) {
				json.push_back(TypeSerializer<T>::Serialize(&item));
			}
			return json;
		}
		static void Deserialize(DeserializationContext* deserializationContext, const nlohmann::json& json, void* outValue)
		{
			DynamicArray<T>* array = static_cast<DynamicArray<T> *>(outValue);
			array->Clear();
			array->Reserve(json.size());
			for (auto item : json) {
				T newItem = T();
				TypeSerializer<T>::Deserialize(deserializationContext, item, &newItem);
				array->PushBack(newItem);
			}
		}
		static bool EditorControl(void* value, const String& name)
		{
#ifdef PLU_ENGINE_EDITOR_BUILD
			DynamicArray<T>* array = static_cast<DynamicArray<T> *>(value);
			return ArrayTreeEditorControl(value, name, array->Size(),
				[array](UInt64 index) { return TypeSerializer<T>::EditorControl(&(*array)[index], String::FromInt(index)); },
				[array]() { array->EmplaceBack(); },
				[array](UInt64 index) { array->RemoveAt(index); });
#else
			return false;
#endif
		}
	};

	template<typename T>
	struct TypeSerializer<TUsePointer<T>>
	{
		static nlohmann::json Serialize(void* dataToSerialize)
		{
			if (T::GetStaticClass()->IsDerivedOfOrSame(IAssetData::GetStaticClass())) {
				TypeInfo* assetToSerialize = T::GetStaticClass();
				if (!dataToSerialize) {
					return 0;
				}
				if (!static_cast<TUsePointer<T>*>(dataToSerialize)->GetRaw()) {
					return 0;
				}
				if (assetToSerialize->GetTypeUuidProp()) {
					return TypeSerializer<PluUUID>::Serialize(assetToSerialize->GetTypeUuidProp()->GetPtr(static_cast<TUsePointer<T>*>(dataToSerialize)->GetRaw()));
				}
			}
			if (!dataToSerialize) {
				return {};
			}
			if (!static_cast<TUsePointer<T>*>(dataToSerialize)->GetRaw()) {
				return {};
			}
			return TypeSerializer<T>::Serialize(static_cast<TUsePointer<T>*>(dataToSerialize)->GetRaw());
		}

		static void Deserialize(DeserializationContext* dc, const nlohmann::json& json, void* outValue)
		{
			if (T::GetStaticClass()->IsDerivedOfOrSame(IAssetData::GetStaticClass())) {
				TypeInfo* assetToSerialize = T::GetStaticClass();
				if (assetToSerialize->GetTypeUuidProp()) {
					PluUUID uuid;
					TypeSerializer<PluUUID>::Deserialize(dc, json, &uuid);
					if (uuid.getUUID() == 0) {
						return;
					}
					TUsePointer<IAssetData> asset = ResolveAssetData(dc, uuid);
					if (asset) {
						*static_cast<TUsePointer<T>*>(outValue) = StaticCast<T>(asset);
					}
					return;
				}
			}
			TypeSerializer<T>::Deserialize(dc, json, outValue);
		}

		static bool EditorControl(void* value, const String& name)
		{
			if (T::GetStaticClass()->IsDerivedOfOrSame(IAssetData::GetStaticClass())) {
#ifdef PLU_ENGINE_EDITOR_BUILD
				return TUsePointerAssetUI(value, name, T::GetStaticClass());
#else
				ImGui::Text("No Editor Utils in engine! Cannot show asset selection UI");
				return false;
#endif
			}

			if (T::GetStaticClass()->Type == TypeType::STRUCT) {
				return TypeSerializer<T>::EditorControl(value, name);
			}

			TUsePointer<EngineObjectManager> objectManager = TypeRegistry::GetInstance()->GetObjectManager();

			// Candidate list cached per field instance (keyed by the field's
			// address), not in one shared static — otherwise the selection and
			// candidate list leak between every TUsePointer<T> field in the editor.
			static HashMap<UInt64, DynamicArray<EngineObjectHandle>> candidatesPerField;
			const UInt64 fieldKey = reinterpret_cast<UInt64>(value);
			if (!candidatesPerField.Contains(fieldKey)) {
				candidatesPerField[fieldKey] = objectManager->GetAllObjectsOfClass(T::GetStaticClass());
			}
			DynamicArray<EngineObjectHandle>& candidates = candidatesPerField[fieldKey];

			// The field's current value is the single source of truth for what is
			// selected — never an ephemeral per-frame UI index.
			TUsePointer<EngineObject> current = *static_cast<TUsePointer<EngineObject>*>(value);
			String preview = "Nothing Selected!";
			if (current) {
				preview = current->GetDisplayName();
			}

			bool changed = false;
			if (ImGui::BeginCombo(name.CStr(), preview.CStr(), 0))
            {
                // Refresh lives inside the dropdown to keep the row uncluttered.
                if (ImGui::SmallButton("Refresh")) {
                    candidates = objectManager->GetAllObjectsOfClass(T::GetStaticClass());
                }
                ImGui::SameLine();

                static ImGuiTextFilter filter;
                if (ImGui::IsWindowAppearing())
                {
                    ImGui::SetKeyboardFocusHere();
                    filter.Clear();
                }
                ImGui::SetNextItemShortcut(ImGuiMod_Ctrl | ImGuiKey_F);
                filter.Draw("##Filter", -FLT_MIN);

                for (int n = 0; n < candidates.Size(); n++)
                {
                	TUsePointer<EngineObject> obj = objectManager->GetObjectAsUser<EngineObject>(candidates[n]);
                	if (!obj) continue;
                    PropertyInfo* nameProp = obj->GetClass()->FindProperty("Name");
                    String objName = nameProp ? *static_cast<String *>(nameProp->GetPtr(obj.GetRaw())) : obj->GetDisplayName();
                    const bool is_selected = (current.GetRaw() == obj.GetRaw());
                    if (filter.PassFilter(objName.CStr()))
                        if (ImGui::Selectable(objName.CStr(), is_selected)) {
                        	*static_cast<TUsePointer<EngineObject>*>(value) = obj;
                        	changed = true;
                        }
                }
                ImGui::EndCombo();
            }
			return changed;
		}
	};

	template <typename T>
	struct TypeSerializer<TOwningPointer<T>>
	{
		static nlohmann::json Serialize(void* dataToSerialize) { return TypeSerializer<TUsePointer<T>>::Serialize(dataToSerialize); }
		static void Deserialize(DeserializationContext* deserializationContext, const nlohmann::json& json, void* outValue) { TypeSerializer<TUsePointer<T>>::Deserialize(deserializationContext, json, outValue); }
		static bool EditorControl(void* value, const String& name)
		{
			return TypeSerializer<TUsePointer<T>>::EditorControl(value, name);
		}
	};

	template <>
	struct TypeSerializer<TypeInfo*>
	{
		static nlohmann::json SerializeFields(TypeInfo* type, void* object)
		{
			nlohmann::json json;
			json["fields"] = nlohmann::json::array();
			for (auto property : type->Properties) {
				nlohmann::json prop;
				prop["name"] = property->PropertyName.CStr();
				prop["value"] = property->SerializePtr(property->GetPtr(object));
				json["fields"].push_back(prop);
			}
			return json;
		}
		static void SerializeTree(TypeInfo* type, void* object, nlohmann::json& json)
		{
			if (type->BaseType) {
				SerializeTree(type->BaseType, object, json);
			}
			nlohmann::json props = SerializeFields(type, object);
			for (const auto& prop : props["fields"]) {
				json["fields"].push_back(prop);
			}
		}
		static nlohmann::json Serialize(TypeInfo* dataToSerialize, void* object)
		{
			nlohmann::json json;
			json["typeName"] = dataToSerialize->TypeName.CStr();
			json["fields"] = nlohmann::json::array();
			SerializeTree(dataToSerialize, object, json);
			return json;
		}

		// Reads the "name" of one entry of the "fields" array without materialising a std::string
		// copy — get_ref hands back the string stored in the DOM. A scene load does this once per
		// field per object, so the copy is worth avoiding. Returns nullptr for a malformed entry,
		// which FindProperty turns into a skipped field rather than a throw.
		static const char* FieldName(const nlohmann::json& field)
		{
			if (!field.is_object()) return nullptr;
			const auto it = field.find("name");
			if (it == field.end() || !it->is_string()) return nullptr;
			return it->get_ref<const nlohmann::json::string_t&>().c_str();
		}

		static void* Deserialize(DeserializationContext* deserializationContext, const nlohmann::json& json, TypeInfo* type)
		{
			PLU_PROFILE_SCOPE("TypeInfo* Deserializer");
			void* newObj = type->Construct();
			if (!json.contains("fields")) return newObj;
			for (const auto& field : json["fields"]) {
				PropertyInfo* prop = type->FindProperty(FieldName(field));
				if (!prop) continue;
				void* propValue = prop->GetPtr(newObj);
				prop->DeserializePtr(deserializationContext, field["value"], propValue);
			}
			return newObj;
		}
		static void* Deserialize(DeserializationContext* deserializationContext, const nlohmann::json& json, TypeInfo* type, void* obj)
		{
			PLU_PROFILE_SCOPE("TypeInfo* Deserializer");
			if (!json.contains("fields")) return obj;
			for (const auto& field : json["fields"]) {
				PropertyInfo* prop = type->FindProperty(FieldName(field));
				if (!prop) continue;
				void* propValue = prop->GetPtr(obj);
				prop->DeserializePtr(deserializationContext, field["value"], propValue);
			}
			return obj;
		}
	private:
		static void GatherParents(Plu::TypeInfo* typeInfo, DynamicArray<Plu::TypeInfo*>* parents)
		{
			if (!typeInfo) return;
			parents->PushBack(typeInfo);
			GatherParents(typeInfo->BaseType, parents);
		}
	public:
		static bool EditorControl(TypeInfo* value, void* obj)
		{
			bool changedAnything = false;
			DynamicArray<TypeInfo*> parents;
			GatherParents(value, &parents);
			for (auto parent : parents) {
				for (PropertyInfo* prop : parent->Properties)
				{
					// Fast path: the property lives directly in the object and has
					// no accessor indirection — edit it in place. No raw byte copy,
					// which would be UB for non-trivial types (String, DynamicArray,
					// TUsePointer, …) whose assignment operator frees the field's
					// own buffer through an aliased shallow copy.
					if (!prop->GetterPtr && !prop->SetterPtr) {
						void* fieldPtr = prop->GetPtr(obj);
						bool changed = false;
#ifdef PLU_ENGINE_EDITOR_BUILD
						if (prop->UuidForClass) {
							changed = UUIDForAssetUI(fieldPtr, prop->PropertyName, value, prop);
						} else {
							changed = prop->EditorControlPtr(fieldPtr, prop->PropertyName);
						}
#else
						changed = prop->EditorControlPtr(fieldPtr, prop->PropertyName);
#endif
						if (changed && !changedAnything) {
							changedAnything = true;
						}
						continue;
					}

					// Accessor path: getter/setter expose a (possibly computed)
					// value, so we need a scratch instance. The setter/getter
					// contract is that buf points at a fully constructed T, so we
					// must give it one — bytes from malloc are not a valid object.
					if (!prop->ConstructScratch || !prop->DestroyScratch) {
						// No scratch lifecycle available; skip rather than risk UB.
						continue;
					}
					void* propValue = prop->ConstructScratch();
					if (prop->GetterPtr) {
						prop->GetterPtr(obj, propValue);
					}
					bool changed = false;
#ifdef PLU_ENGINE_EDITOR_BUILD
					if (prop->UuidForClass)
					{
						changed = UUIDForAssetUI(propValue, prop->PropertyName, value, prop);
					} else
					{
						changed = prop->EditorControlPtr(propValue, prop->PropertyName);
					}
#else
					changed = prop->EditorControlPtr(propValue, prop->PropertyName);
#endif
					if (changed && !changedAnything) {
						changedAnything = true;
					}
					if (changed && prop->SetterPtr) {
						prop->SetterPtr(obj, propValue);
					}
					prop->DestroyScratch(propValue);
				}
			}
			return changedAnything;
		}
	};

	template <>
	struct TypeSerializer<glm::vec2>
	{
		static nlohmann::json Serialize(void* dataToSerialize)
		{
			auto& v = *static_cast<glm::vec2*>(dataToSerialize);
			return { v.x, v.y };
		}

		static void Deserialize(DeserializationContext*, const nlohmann::json& json, void* outValue)
		{
			auto& v = *static_cast<glm::vec2*>(outValue);
			v.x = json[0].get<float>();
			v.y = json[1].get<float>();
		}

		static bool EditorControl(void* value, const String& name)
		{
			return ImGui::DragFloat2(name.CStr(), &static_cast<glm::vec2*>(value)->x, 0.1f);
		}
	};

	template <>
	struct TypeSerializer<glm::vec3>
	{
		static nlohmann::json Serialize(void* dataToSerialize)
		{
			auto& v = *static_cast<glm::vec3*>(dataToSerialize);
			return { v.x, v.y, v.z };
		}

		static void Deserialize(DeserializationContext*, const nlohmann::json& json, void* outValue)
		{
			auto& v = *static_cast<glm::vec3*>(outValue);
			v.x = json[0].get<float>();
			v.y = json[1].get<float>();
			v.z = json[2].get<float>();
		}

		static bool EditorControl(void* value, const String& name)
		{
			if (name.Contains("color") || name.Contains("colour") || name.Contains("Color") || name.Contains("Colour")) {
				return ImGui::ColorEdit3(name.CStr(), &static_cast<glm::vec3*>(value)->x);
			} else {
				return ImGui::DragFloat3(name.CStr(), &static_cast<glm::vec3*>(value)->x, 0.1f);
			}
		}
	};

	template <>
	struct TypeSerializer<glm::vec4>
	{
		static nlohmann::json Serialize(void* dataToSerialize)
		{
			auto& v = *static_cast<glm::vec4*>(dataToSerialize);
			return { v.x, v.y, v.z, v.w };
		}

		static void Deserialize(DeserializationContext*, const nlohmann::json& json, void* outValue)
		{
			auto& v = *static_cast<glm::vec4*>(outValue);
			v.x = json[0].get<float>();
			v.y = json[1].get<float>();
			v.z = json[2].get<float>();
			v.w = json[3].get<float>();
		}

		static bool EditorControl(void* value, const String& name)
		{
			if (name.Contains("color") || name.Contains("colour") || name.Contains("Color") || name.Contains("Colour")) {
				return ImGui::ColorEdit4(name.CStr(), &static_cast<glm::vec4*>(value)->x);
			} else {
				return ImGui::DragFloat4(name.CStr(), &static_cast<glm::vec4*>(value)->x, 0.1f);
			}
		}
	};

	template <>
	struct TypeSerializer<glm::quat>
	{
		static nlohmann::json Serialize(void* dataToSerialize)
		{
			auto& q = *static_cast<glm::quat*>(dataToSerialize);
			return { q.x, q.y, q.z, q.w };
		}

		static void Deserialize(DeserializationContext*, const nlohmann::json& json, void* outValue)
		{
			auto& q = *static_cast<glm::quat*>(outValue);
			q.x = json[0].get<float>();
			q.y = json[1].get<float>();
			q.z = json[2].get<float>();
			q.w = json[3].get<float>();
		}
		static bool EditorControl(void* value, const String& name)
		{
			auto& q = *static_cast<glm::quat*>(value);

			glm::vec3 euler = glm::degrees(glm::eulerAngles(q));

			if (ImGui::DragFloat3(name.CStr(), &euler.x, 0.5f))
			{
				q = glm::quat(glm::radians(euler));
				q = glm::normalize(q);
				return true;
			}
			return false;
		}
	};

	template <typename T>
	requires EngineObjectConc<T>
	struct TypeSerializer<TClassPointer<T>>
	{
		static nlohmann::json Serialize(void* dataToSerialize)
		{
			TClassPointer<T>* classPtr = static_cast<TClassPointer<T> *>(dataToSerialize);
			// An unset class pointer is legal (default-constructed) and round-trips as null.
			if (!classPtr->GetRawType()) {
				return nullptr;
			}
			return classPtr->GetRawType()->TypeName.CStr();
		}

		static void Deserialize(DeserializationContext*, const nlohmann::json& json, void* outValue)
		{
			TClassPointer<T>* classPtr = static_cast<TClassPointer<T> *>(outValue);
			if (json.is_null()) {
				*classPtr = TClassPointer<T>();
				return;
			}
			if (!json.is_string()) {
				return;
			}
			// A class that no longer exists, or no longer derives from T, keeps the field's default
			// instead of tripping TClassPointer's assert.
			TypeInfo* type = TypeRegistry::GetInstance()->GetTypeOfName(json.get<std::string>().c_str());
			if (!type || !type->IsDerivedOfOrSame(T::GetStaticClass())) {
				PLU_CORE_WARN("TClassPointer<{}>: class '{}' not found or not derived from it, keeping the default",
				              T::GetStaticClass()->TypeName.CStr(), json.get<std::string>());
				return;
			}
			*classPtr = type;
		}

		static DynamicArray<TypeInfo*> GatherDerivedTypes()
		{
			DynamicArray<TypeInfo*> types;
			for (const auto& type : *TypeRegistry::GetInstance()->GetTypeMap()) {
				if (type.second->IsDerivedOfOrSame(T::GetStaticClass())) {
					types.PushBack(type.second);
				}
			}
			return types;
		}

		static bool EditorControl(void* value, const String& name)
		{
#ifdef PLU_ENGINE_EDITOR_BUILD
			TClassPointer<T>* classPtr = static_cast<TClassPointer<T> *>(value);
			TypeInfo* type = classPtr->GetRawType();
			if (!ClassPointerEditorControl(&type, T::GetStaticClass(), name)) {
				return false;
			}
			*classPtr = type;
			return true;
#else
			ImGui::Text("No Editor Utils in engine! Cannot show class picker UI");
			return false;
#endif
		}
	};
	//Here Serializer
}

namespace pybind11::detail {

	template <typename Allocator>
	class type_caster<Plu::BasicString<char, Allocator>> {
	public:
		using StringType = Plu::BasicString<char, Allocator>;
		PYBIND11_TYPE_CASTER(StringType, const_name("str"));

		// Python str → C++ BasicString
		bool load(handle src, bool) {
			PyObject* tmp = PyUnicode_AsUTF8String(src.ptr());
			if (!tmp) return false;
			const char* utf8 = PyBytes_AS_STRING(tmp);
			value = StringType(utf8);
			Py_DECREF(tmp);
			return true;
		}

		// C++ BasicString → Python str
		static handle cast(const StringType& src, return_value_policy, handle) {
			return PyUnicode_FromStringAndSize(src.CStr(), src.Length());
		}
	};

	// DynamicArray<T> ↔ python list. Does what pybind11's stock list_caster does, but that one
	// expects std-style member names (push_back/reserve), which PluSTL containers do not have.
	template <typename T, typename Allocator>
	class type_caster<DynamicArray<T, Allocator>> {
	public:
		using ArrayType = DynamicArray<T, Allocator>;
		using ValueConv = make_caster<T>;
		PYBIND11_TYPE_CASTER(ArrayType, const_name("List[") + ValueConv::name + const_name("]"));

		// Python sequence → C++ DynamicArray (str/bytes are sequences too, but never a list here)
		bool load(handle src, bool convert) {
			if (!isinstance<sequence>(src) || isinstance<str>(src) || isinstance<bytes>(src))
				return false;
			auto seq = reinterpret_borrow<sequence>(src);
			value.Clear();
			value.Reserve(static_cast<size_t>(seq.size()));
			for (const handle& item : seq) {
				ValueConv conv;
				if (!conv.load(item, convert)) return false;
				value.PushBack(cast_op<T&&>(std::move(conv)));
			}
			return true;
		}

		// C++ DynamicArray → Python list
		static handle cast(const ArrayType& src, return_value_policy policy, handle parent) {
			const return_value_policy itemPolicy = return_value_policy_override<T>::policy(policy);
			list result(static_cast<ssize_t>(src.Size()));
			ssize_t index = 0;
			for (const T& item : src) {
				object converted = reinterpret_steal<object>(ValueConv::cast(item, itemPolicy, parent));
				if (!converted) return handle();
				PyList_SET_ITEM(result.ptr(), index++, converted.release().ptr());
			}
			return result.release();
		}
	};

}

#endif //PLUENGINE_TYPETRAITS_H