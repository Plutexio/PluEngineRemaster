//
// Created by Plutex on 1/3/26.
//

#include "PluEngine/Core/Reflection/ReflectionBase.h"

#include "PluBase/Log.h"
#include "PluEngine/Core/Reflection/TypeTraits.h"
#include "PluEngine/Core/Objects/EngineObjectManager.h"

namespace Plu
{
	PrimitiveTypeInfo * TypeRegistry::FindPrimitiveByName(const String &name)
	{
		return mPrimitiveTypeMap.Contains(name) ? mPrimitiveTypeMap[name] : nullptr;
	}

	String GetTypeNameFromPython(pybind11::type type)
	{
		std::string name = pybind11::str(type.attr("__name__"));
		return name.c_str();
	}

	void RegisterPluClass(pybind11::type type)
	{
		String name = GetTypeNameFromPython(type);
		pybind11::tuple bases = type.attr("__bases__");

		pybind11::handle b = bases[0];
		TypeInfo* base = TypeRegistry::GetInstance()->GetTypeOfName(std::string(pybind11::str(pybind11::reinterpret_borrow<pybind11::type>(b).attr("__name__"))).c_str());
		if (!base) {
			PLU_CORE_ERROR("RegisterPluClass: base type of Python class '{}' is not known to reflection", name.CStr());
			return;
		}
		if (!base->IsDerivedOfOrSame(EngineObject::GetStaticClass())) return;


		TypeInfo* existing = TypeRegistry::GetInstance()->GetTypeOfName(name);
		if (existing && !existing->IsPythonType) {
			PLU_CORE_ERROR("RegisterPluClass: python class '{}' collides with a C++ reflected type of the same name", name.CStr());
			return;
		}
		if (existing) {
			existing->BaseType = base;
			existing->PythonType = type;
			existing->IsAbstract = false;
			existing->ClearProperties();
			GatherPythonProperties(type, existing);
			PLU_CORE_INFO("Class from python {} -> {} (reloaded)", name.CStr(), base->TypeName.CStr());
		} else {
			TypeInfo* newType = new TypeInfo{0, name, TypeType::CLASS};
			newType->BaseType = base;
			newType->Constructor = nullptr;
			newType->IsPythonType = true;
			newType->PythonType = type;
			newType->IsAbstract = false;
			TypeRegistry::GetInstance()->AddType(newType);
			GatherPythonProperties(type, newType);
			PLU_CORE_INFO("Class from python {} -> {}", name.CStr(), base->TypeName.CStr());
		}
		String nameToSend = name;
		TypeRegistry::GetInstance()->TypeRegistryEventDispatcher.Dispatch("NewPythonType", &nameToSend);
	}

	void GatherPythonProperties(const pybind11::type& pythonType, TypeInfo *type)
	{
		PLU_PROFILE_SCOPE("GatherPythonProperties");
		for (pybind11::handle item : pythonType.attr("__dict__").attr("items")()) {
			pybind11::tuple pair = pybind11::reinterpret_borrow<pybind11::tuple>(item);
			pybind11::handle descriptor = pair[1];
			if (!pybind11::hasattr(descriptor, "__plu_property__")) continue;

			pybind11::object nameObj = descriptor.attr("Name");
			if (nameObj.is_none()) {
				PLU_CORE_WARN("{}: a PluEngine.Property was added after the class was created and is ignored",
				              type->TypeName.CStr());
				continue;
			}

			pybind11::object valueType = descriptor.attr("Type");
			auto* property = new PythonPropertyInfo(
				pybind11::cast<std::string>(nameObj).c_str(),
				pybind11::cast<std::string>(valueType.attr("__name__")).c_str());
			property->Descriptor = pybind11::reinterpret_borrow<pybind11::object>(descriptor);
			property->ValueType = valueType;
			property->Tooltip = pybind11::cast<std::string>(descriptor.attr("Tooltip")).c_str();

			pybind11::object minObj = descriptor.attr("Min");
			pybind11::object maxObj = descriptor.attr("Max");
			if (!minObj.is_none()) property->Min = minObj.cast<double>();
			if (!maxObj.is_none()) property->Max = maxObj.cast<double>();

			type->AddProperty(property);
			PLU_CORE_TRACE("Python property {}.{} : {}", type->TypeName.CStr(),
			               property->PropertyName.CStr(), property->PropertyTypeName.CStr());
		}
	}

	IPropertyInfo::IPropertyInfo(String propertyName, String propertyTypeName)
		: PropertyName(std::move(propertyName)), PropertyTypeName(std::move(propertyTypeName))
	{
	}

	PropertyInfo::PropertyInfo(String propertyName, UInt64 propertyOffset, UInt64 propertySize, String propertyTypeName)
		: IPropertyInfo(std::move(propertyName), std::move(propertyTypeName)),
		  PropertyOffset(propertyOffset), PropertySize(propertySize)
	{
	}

	void * PropertyInfo::GetPtr(void *objectInstance) const
	{
		return static_cast<char*>(objectInstance) + PropertyOffset;
	}

	JSON PropertyInfo::Serialize(void *objectInstance) const
	{
		return SerializePtr(GetPtr(objectInstance));
	}

	void PropertyInfo::Deserialize(DeserializationContext *deserializationContext, const JSON &json, void *objectInstance) const
	{
		DeserializePtr(deserializationContext, json, GetPtr(objectInstance));
	}

	void PropertyInfo::Copy(const void *srcInstance, void *dstInstance) const
	{
		if (!CopyPtr) return;
		CopyPtr(GetPtr(const_cast<void*>(srcInstance)), GetPtr(dstInstance));
	}

	bool PropertyInfo::EditorControl(void *objectInstance, const String &label, TypeInfo *ownerType) const
	{
		auto drawControl = [&](void* value) -> bool {
#ifdef PLU_ENGINE_EDITOR_BUILD
			if (UuidForClass) {
				return UUIDForAssetUI(value, label, ownerType, const_cast<PropertyInfo*>(this));
			}
#endif
			return EditorControlPtr(value, label);
		};

		if (!GetterPtr && !SetterPtr) {
			return drawControl(GetPtr(objectInstance));
		}

		if (!ConstructScratch || !DestroyScratch) {
			return false;
		}
		void* scratch = ConstructScratch();
		if (GetterPtr) {
			GetterPtr(objectInstance, scratch);
		}
		const bool changed = drawControl(scratch);
		if (changed && SetterPtr) {
			SetterPtr(objectInstance, scratch);
		}
		DestroyScratch(scratch);
		return changed;
	}

	PythonPropertyInfo::PythonPropertyInfo(String propertyName, String propertyTypeName)
		: IPropertyInfo(std::move(propertyName), std::move(propertyTypeName))
	{
	}

	PythonPropertyInfo::~PythonPropertyInfo()
	{
		// Same as TypeInfo::PythonType in TypeRegistry::Cleanup: reflection is torn down after
		// Py_Finalize, and a decref then would touch a dead interpreter.
		if (!Py_IsInitialized()) {
			Descriptor.release();
			ValueType.release();
		}
	}

	JSON PythonPropertyInfo::Serialize(void *objectInstance) const
	{
		PrimitiveTypeInfo* primitive = TypeRegistry::GetInstance()->FindPrimitiveByName(PropertyTypeName);

		pybind11::object self = pybind11::cast(static_cast<EngineObject*>(objectInstance), pybind11::return_value_policy::reference);

		void* temp = primitive->ConstructScratchPtr();
		primitive->FromPythonPtr(self.attr(PropertyName.CStr()), temp);
		return primitive->SerializePtr(temp);
	}

	void PythonPropertyInfo::Deserialize(DeserializationContext *deserializationContext, const JSON &json, void *objectInstance) const
	{
		PrimitiveTypeInfo* primitive = TypeRegistry::GetInstance()->FindPrimitiveByName(PropertyTypeName);

		pybind11::object self = pybind11::cast(static_cast<EngineObject*>(objectInstance), pybind11::return_value_policy::reference);

		void* temp = primitive->ConstructScratchPtr();
		primitive->DeserializePtr(deserializationContext, json, temp);
		pybind11::setattr(self, PropertyName.CStr(), primitive->ToPythonPtr(temp));
	}

	bool PythonPropertyInfo::EditorControl(void *objectInstance, const String &label, TypeInfo *ownerType) const
	{
		PrimitiveTypeInfo* primitive = TypeRegistry::GetInstance()->FindPrimitiveByName(PropertyTypeName);

		pybind11::object self = pybind11::cast(static_cast<EngineObject*>(objectInstance), pybind11::return_value_policy::reference);

		void* temp = primitive->ConstructScratchPtr();
		primitive->FromPythonPtr(self.attr(PropertyName.CStr()), temp);
		bool changed = primitive->EditorControlPtr(temp, label);
		if (changed) {
			pybind11::setattr(self, PropertyName.CStr(), primitive->ToPythonPtr(temp));
		}
		primitive->DestroyScratchPtr(temp);
		return changed;
	}

	void PythonPropertyInfo::Copy(const void *srcInstance, void *dstInstance) const
	{
		PrimitiveTypeInfo* primitive = TypeRegistry::GetInstance()->FindPrimitiveByName(PropertyTypeName);
		pybind11::object src = pybind11::cast(static_cast<const EngineObject*>(srcInstance), pybind11::return_value_policy::reference);
		pybind11::object dst = pybind11::cast(static_cast<EngineObject*>(dstInstance), pybind11::return_value_policy::reference);

		void* temp = primitive->ConstructScratchPtr();
		primitive->FromPythonPtr(src.attr(PropertyName.CStr()), temp);
		pybind11::setattr(dst, PropertyName.CStr(), primitive->ToPythonPtr(temp));
		primitive->DestroyScratchPtr(temp);
	}

	PropertyInfo * TypeInfo::GetTypeUuidProp() const
	{
		if (TypeUuidProp) return TypeUuidProp;
		if (BaseType) return BaseType->GetTypeUuidProp();
		return nullptr;
	}

	static UInt64 gPropertyGeneration = 1;

	void TypeInfo::AddProperty(IPropertyInfo *propertyInfo)
	{
		Properties.PushBack(propertyInfo);
		++gPropertyGeneration;
	}

	void TypeInfo::ClearProperties()
	{
		for (IPropertyInfo* property : Properties) {
			delete property;
		}
		Properties.Clear();
		// The flattened maps of every type below this one still point at the deleted properties.
		++gPropertyGeneration;
	}

	void * TypeInfo::Construct() const
	{
		return Constructor ? Constructor() : nullptr;
	}

	IPropertyInfo * TypeInfo::FindProperty(const String &propertyName)
	{
		if (PropertiesByNameGeneration != gPropertyGeneration) {
			PropertiesByName.Clear();
			DynamicArray<TypeInfo*> chain;
			for (TypeInfo* t = this; t; t = t->BaseType) {
				chain.PushBack(t);
			}
			for (Int64 i = static_cast<Int64>(chain.Size()) - 1; i >= 0; --i) {
				for (IPropertyInfo* p : chain[i]->Properties) {
					PropertiesByName[p->PropertyName] = p;
				}
			}
			PropertiesByNameGeneration = gPropertyGeneration;
		}
		IPropertyInfo** found = PropertiesByName.Find(propertyName);
		return found ? *found : nullptr;
	}

	void CopyReflectedProperties(TypeInfo *type, const void *src, void *dst)
	{
		if (!type || !src || !dst) return;
		CopyReflectedProperties(type->BaseType, src, dst);
		for (IPropertyInfo* prop : type->Properties) {
			prop->Copy(src, dst);
		}
	}

	bool TypeInfo::IsChildOf(TypeInfo *potentialParent)
	{
		return potentialParent == this->BaseType;
	}

	void GatherAllParentTypes(TypeInfo* me, DynamicArray<TypeInfo*>* typeInfos)
	{
		if (!me->BaseType) return;
		typeInfos->PushBack(me->BaseType);
		GatherAllParentTypes(me->BaseType, typeInfos);
	}

	bool TypeInfo::IsDerivedOf(TypeInfo *potentialParent)
	{
		DynamicArray<TypeInfo*> parents;
		GatherAllParentTypes(this, &parents);
		return parents.Contains(potentialParent);
	}

	bool TypeInfo::IsDerivedOfOrSame(TypeInfo *potentialParent)
	{
		return IsDerivedOf(potentialParent) || this == potentialParent;
	}

	nlohmann::json TypeInfo::SerializeToJSON(void* obj) const
	{
		return SerializeToJson ? SerializeToJson(obj) : nlohmann::json();
	}

	void * TypeInfo::DeSerializeFromJSON(DeserializationContext *dc, const nlohmann::json &j) const
	{
		return DeserializeFromJson ? DeserializeFromJson(dc, j) : nullptr;
	}

	TypeInfo::TypeInfo(UInt64 size, String typeName, TypeType type) : TypeSize(size), TypeName(typeName), Type(type)
	{
	}

	TypeInfo::~TypeInfo() = default;

	void EnumInfo::AddValue(String enumName, UInt64 value)
	{
		EnumValues.PushBack(new EnumValue{enumName, value});
	}

	TUsePointer<EngineObjectManager> TypeRegistry::GetObjectManager()
	{
		return mObjectManager;
	}

	TUsePointer<EngineAssetManager> TypeRegistry::GetAssetManager()
	{
		return mAssetManager;
	}

	TypeRegistry * TypeRegistry::GetInstance()
	{
		// Function-local static: guaranteed single, thread-safe initialisation.
		static TypeRegistry instance;
		return &instance;
	}

	void TypeRegistry::AddType(TypeInfo *typeInfo)
	{
		if (mTypeMap.Contains(typeInfo->TypeName)) {
			mTypeMap.Remove(typeInfo->TypeName);
		}
		mTypeMap.Insert(typeInfo->TypeName, typeInfo);
	}

	TypeInfo * TypeRegistry::GetTypeOfName(const String& typeName)
	{
		if (mTypeMap.Contains(typeName)) {
			return *mTypeMap.Find(typeName);
		}
		return nullptr;
	}

	HashMap<String, TypeInfo*> * TypeRegistry::GetTypeMap()
	{
		return &mTypeMap;
	}

	void TypeRegistry::Cleanup()
	{
		for (auto& entry : mTypeMap) {
			TypeInfo* typeInfo = entry.second;
			if (!typeInfo) continue;
			if (!Py_IsInitialized()) {
				typeInfo->PythonType.release();
			}
			typeInfo->ClearProperties();
			delete typeInfo;
		}
		mTypeMap.Clear();

		for (auto& entry : mEnumMap) {
			EnumInfo* enumInfo = entry.second;
			if (!enumInfo) continue;
			for (EnumValue* value : enumInfo->EnumValues) {
				delete value;
			}
			delete enumInfo;
		}
		mEnumMap.Clear();

		for (auto entry : mPrimitiveTypeMap) {
			PrimitiveTypeInfo* typeInfo = entry.second;
			if (!typeInfo) continue;
			delete typeInfo;
		}
		mPrimitiveTypeMap.Clear();
	}
}
