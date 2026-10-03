//
// Created by Plutex on 5/29/26.
//

#include "PluEngine/Gameplay/Scenes/SceneManager.h"
#include "PluEngine/AssetCore/EngineAssetManager.h"

#include "PluEngine/Core/ApplicationInfo.h"
#include "PluEngine/Timer.h"
#include "PluEngine/Gameplay/Objects/Lights/DirectionalLight.h"
#include "PluEngine/Core/DiskManager.h"
#include "PluEngine/Gameplay/Scenes/ScenesManager.h"
#include "PluEngine/Core/Objects/EngineObjectManager.h"
#include "PluEngine/Render/RenderingInterfaces.h"
#include "PluEngine/Gameplay/Scenes/SceneWorld.h"
#include "HashSet/HashSet.h"
#include "PluEngine/AssetCore/AssetDescriptor.h"

void Plu::SceneManager::UnloadScene(TUsePointer<SceneWorld> sceneWorld)
{
	if (!sceneWorld) return;
	EngineObjectHandle oldWorldHandle = sceneWorld->GetObjectHandle();
#ifdef PLU_ENGINE_EDITOR_BUILD
    if (sceneWorld == mOverlayScene || (sceneWorld->Info && sceneWorld->Info->URL == "Overlay")) {
        UnloadOverlayScene();
    }
	if (sceneWorld && sceneWorld == mActivePIEScene) {
		mActivePIEScene->UnloadGameObjects();
		mEditorCamera = nullptr;
		mObjectManager->DestroyObject(*mActivePIEScene->GetEngineObjectHandle());
		mActivePIEScene = nullptr;
		if (mActiveScene && !mIsInPIE) {
			IRendererCamera* cameraToViewInEditor = nullptr;
			DispatchEvent("EditorCameraWanted", &cameraToViewInEditor);
			mEditorCamera = cameraToViewInEditor;
		}
	}
#endif
    if (sceneWorld && sceneWorld == mActiveScene) {
        mActiveScene->UnloadGameObjects();
        mObjectManager->DestroyObject(*mActiveScene->GetEngineObjectHandle());
        mActiveScene = nullptr;
    }
	DispatchEvent("UnloadWorld", &oldWorldHandle);
}

void Plu::SceneManager::LoadScene(String url, TOwningPointer<SceneWorld>* field, bool play, TUsePointer<SceneWorld> cloneSource)
{
    PLU_PROFILE_SCOPE("LoadScene");
    if (!mRegisteredScenesByURL.Contains(url)) return;
    TUsePointer<SceneInfo> sceneInfo = mRegisteredScenesByURL[url];
    if (!sceneInfo) return;
    TUsePointer<AssetDescriptor> assetDesc = mAssetManager->GetAssetDescriptor(sceneInfo->Uuid.getUUID());
    if (!assetDesc) return;
    if (*field) UnloadScene(*field);
    EngineObjectHandle hdl = mObjectManager->CreateObject<SceneWorld>();
    TOwningPointer<SceneWorld> newWorld = mObjectManager->GetObjectAsOwner<SceneWorld>(hdl);
    newWorld->Init(mObjectManager, mClient);
    newWorld->Info = sceneInfo;
	EngineObjectHandle newWorldHandle = newWorld->GetObjectHandle();
	GetObjectEventDispatcher()->Dispatch("NewWorldBeforeLoad", &newWorldHandle);
    if (cloneSource) {
        CloneSceneInto(newWorld, cloneSource);
    } else {
        LoadSceneFromFile(newWorld);
    }
    newWorld->LoadGameObjects();
	*field = newWorld;
	if (play) {
		newWorld->Play();
	} else {
#ifdef PLU_ENGINE_EDITOR_BUILD
		IRendererCamera* cameraToViewInEditor = nullptr;
		DispatchEvent("EditorCameraWanted", &cameraToViewInEditor);
		mEditorCamera = cameraToViewInEditor;
#endif
	}
	newWorldHandle = newWorld->GetObjectHandle();
	GetObjectEventDispatcher()->Dispatch("NewWorld", &newWorldHandle);
	PLU_CORE_INFO("New Scene Loaded! URL {}", url.CStr());
}

#ifdef PLU_ENGINE_EDITOR_BUILD
void Plu::SceneManager::CreateOverlayScene()
{
	EngineObjectHandle hdl = mObjectManager->CreateObject<SceneWorld>();
	mOverlayScene = mObjectManager->GetObjectAsOwner<SceneWorld>(hdl);
	mOverlayScene->Init(mObjectManager, mClient);
	mOverlayScene->Info = nullptr;
	EngineObjectHandle newOverlayHandle = hdl;
	GetObjectEventDispatcher()->Dispatch("NewWorldBeforeLoad", &newOverlayHandle);
	mOverlayScene->LoadGameObjects();
	IRendererCamera* cameraToViewInEditor = nullptr;
	DispatchEvent("EditorCameraWanted", &cameraToViewInEditor);
	mEditorCamera = cameraToViewInEditor;
	newOverlayHandle = hdl;
	GetObjectEventDispatcher()->Dispatch("NewWorld", &newOverlayHandle);

	TUsePointer<DirectionalLight> dirLight = mOverlayScene->SpawnGameObject(DirectionalLight::GetStaticClass());
	dirLight->SetObjectRotation({250,40,0});
}

void Plu::SceneManager::UnloadOverlayScene()
{
	if (mOverlayScene) {
		EngineObjectHandle hdl = mOverlayScene->GetObjectHandle();
		mOverlayScene->UnloadGameObjects();
		mEditorCamera = nullptr;
		mObjectManager->DestroyObject(*mOverlayScene->GetEngineObjectHandle());
		mOverlayScene = nullptr;
		if (mActiveScene && !IsInPIE()) {
			IRendererCamera* cameraToViewInEditor = nullptr;
			DispatchEvent("EditorCameraWanted", &cameraToViewInEditor);
			mEditorCamera = cameraToViewInEditor;
		}
		DispatchEvent("UnloadWorld", &hdl);
	}
}
#endif

Plu::SceneManager::SceneManager()
{

}

Plu::SceneManager::~SceneManager()
{
#ifdef PLU_ENGINE_EDITOR_BUILD
	UnloadScene(mActivePIEScene);
#endif
	UnloadScene(mActiveScene);
}

static Plu::TUsePointer<Plu::SceneManager> gSceneManager;

void Plu::SceneManager::Initialize(ApplicationInfo *appInfo)
{
    mObjectManager = appInfo->AppObjectManager;
    mClient = appInfo->Client;
	mShaderManager = appInfo->AppShaderManager;
    mAssetManager = appInfo->AppAssetManager;
    mRegisteredScenesByURL["Overlay"] = nullptr;
    gSceneManager = mObjectManager->GetObjectAsUser<SceneManager>(*this->GetEngineObjectHandle());

	mAssetManager->SubscribeToEvent("LoadAssetDescriptor", [&](void* data) {
		UInt64* uuid = static_cast<UInt64*>(data);
		TUsePointer<AssetDescriptor> assetDescriptor = mAssetManager->GetAssetDescriptor(*uuid);
		if (!assetDescriptor) return;
		if (assetDescriptor->AssetType != SceneInfo::GetStaticClass()) return;

		TUsePointer<SceneInfo> sceneInfo = mAssetManager->GetAssetData(assetDescriptor);
		RegisterSceneInfo(sceneInfo);
	});
}

void Plu::SceneManager::OnUpdate(float deltaTime)
{
#ifdef PLU_ENGINE_EDITOR_BUILD
	if (IsInPIE())
	{
		if (GetCurrentWorld()) GetCurrentWorld()->TickScene(deltaTime);
	}
#else
	if (GetCurrentWorld()) GetCurrentWorld()->TickScene(deltaTime);
#endif
}

Plu::String Plu::SceneManager::GetCurrentWorldName()
{
    if (GetCurrentWorld()) {
        if (GetCurrentWorld()->Info) {
            return GetCurrentWorld()->Info->URL;
        }
    }
    return "";
}

Plu::TUsePointer<Plu::SceneWorld> Plu::SceneManager::GetCurrentWorld()
{
#ifdef PLU_ENGINE_EDITOR_BUILD
    return mOverlayScene ? mOverlayScene : mActivePIEScene ? mActivePIEScene : mActiveScene;
#else
    return mActiveScene;
#endif
}

void Plu::SceneManager::DisconnectFromWorld()
{
	if (GetCurrentWorld()) {
		UnloadScene(GetCurrentWorld());
	}
}

bool Plu::SceneManager::ConnectToWorld(String URL, bool startPlayOnLoad)
{
#ifdef PLU_ENGINE_EDITOR_BUILD
    UnloadOverlayScene();
    if (URL == "Overlay") {
        CreateOverlayScene();
        return true;
    }
	if (GetCurrentWorldName() == URL) return false;
    if (IsInPIE()) {
    	if (!mRegisteredScenesByURL.Contains(URL)) {
    		PLU_CORE_ERROR("No scene with URL {}", URL.CStr());
    		return false;
    	}
    	UnloadScene(mActivePIEScene);
    	LoadScene(URL, &mActivePIEScene, true);
    	return true;
    }
    UnloadScene(GetCurrentWorld());
    LoadScene(URL, &mActiveScene, startPlayOnLoad);
    return true;
#else
    if (GetCurrentWorldName() == URL) return false;
    UnloadScene(GetCurrentWorld());
    LoadScene(URL, &mActiveScene);
    return true;
#endif
}

bool Plu::SceneManager::IsAnySceneOpen()
{
    return GetCurrentWorld().IsValid();
}

void Plu::SceneManager::RegisterSceneInfo(TUsePointer<SceneInfo> sceneInfo)
{
    sceneInfo->URL.Replace(" ","");
    if (sceneInfo->URL == "Overlay" || sceneInfo->URL == "ActiveScene") {
        return;
    }
    mRegisteredScenesByURL[sceneInfo->URL] = sceneInfo;
	PLU_CORE_TRACE("New Scene info registere! {}", sceneInfo->URL.CStr());
}

#ifdef PLU_ENGINE_EDITOR_BUILD
bool Plu::SceneManager::EnterPIE()
{
	if (mIsInPIE) return false;
	PLU_PROFILE_SCOPE("PIE Enter");
	mIsInPIE = true;
	UnloadOverlayScene();
	// The PIE world is duplicated from the editor world in memory. It used to go through the scene
	// file, then through an in-memory JSON snapshot; both paid for building a JSON DOM and taking it
	// apart again, which was the bulk of the transition cost. Nothing is written to disk either way —
	// saving is only what the user explicitly asks for (SaveActiveScene).
	LoadScene(GetCurrentWorldName(), &mActivePIEScene, true, mActiveScene);
	mEditorCamera = nullptr;
	//mActivePIEScene->GetPhysicsWorld()->PhysicsDebugRenderMode = mActiveScene->GetPhysicsWorld()->PhysicsDebugRenderMode; TODO
	return true;
}

void Plu::SceneManager::ExitPIE()
{
	if (!mIsInPIE) return;
	PLU_PROFILE_SCOPE("PIE Exit");
	mIsInPIE = false;
	UnloadScene(mActivePIEScene);
	IRendererCamera* cameraToViewInEditor = nullptr;
	DispatchEvent("EditorCameraWanted", &cameraToViewInEditor);
	mEditorCamera = cameraToViewInEditor;
}

bool Plu::SceneManager::IsInPIE() const
{
	return mActivePIEScene && mIsInPIE;
}

Plu::TUsePointer<Plu::SceneWorld> Plu::SceneManager::GetBaseSceneWorld()
{
	return mActiveScene;
}

void Plu::SceneManager::SetEditorRenderCamera(IRendererCamera* camera)
{
	mEditorCamera = camera;
}

Plu::IRendererCamera* Plu::SceneManager::GetEditorRenderCamera() const
{
	return mEditorCamera;
}
#endif

Plu::TUsePointer<Plu::SceneWorld> Plu::GetCurrentWorld()
{
    return gSceneManager ? gSceneManager->GetCurrentWorld() : nullptr;
}

void Plu::ConnectToWorld(const String &URL) {
	if (!gSceneManager) return;
	gSceneManager->ConnectToWorld(URL);
}

// Reflected properties land in the flat "fields" array written by TypeSerializer<TypeInfo*>, so a
// single string field can be read straight out of the JSON — no need to construct an object and run
// a full deserialization just to inspect one value. outFound (optional) distinguishes "field absent"
// from "field present but empty".
static Plu::String ReadStringFieldFromJson(const JSON& j, const char* fieldName, bool* outFound = nullptr)
{
	if (outFound) *outFound = false;
	if (!j.contains("fields")) return {};
	for (const auto& field : j["fields"]) {
		if (!field.contains("name") || !field.contains("value")) continue;
		if (field["name"].get<std::string>() != fieldName) continue;
		Plu::String value;
		Plu::TypeSerializer<Plu::String>::Deserialize(nullptr, field["value"], &value);
		if (outFound) *outFound = true;
		return value;
	}
	return {};
}

void Plu::SceneManager::DeserializeWorldComponent(DeserializationContext* dc, const JSON& j, TUsePointer<WorldComponent> parentComponent, TUsePointer<GameObject> parentObject)
{
	PLU_PROFILE_SCOPE("DeserializeWorldComponent");
	if (!j.contains("typeName")) return;
	TypeInfo* componentClass = TypeRegistry::GetInstance()->GetTypeOfName(j["typeName"].get<std::string>().c_str());
	if (!componentClass) return;
	const String componentName = ReadStringFieldFromJson(j, "mComponentName");
	DynamicArray<TUsePointer<WorldComponent>> componentsToSearchIn = parentComponent ? parentComponent->GetChildren() : parentObject->GetDirectlyAttachedWorldComponents();
	TUsePointer<WorldComponent>* result = componentsToSearchIn.FindIf([&componentName](TUsePointer<WorldComponent> comp) -> bool {
		return componentName == comp->GetComponentName();
	});
	if (result != componentsToSearchIn.End()) {
		// TypeSerializer<TypeInfo*>::Deserialize(dc, j, componentClass, result->GetRaw());
		//
		// if (j.contains("relativeLocation")) {
		// 	Vec3 newRelativeLocation;
		// 	TypeSerializer<Vec3>::Deserialize(dc, j["relativeLocation"], &newRelativeLocation);
		// 	result->GetRaw()->SetRelativeLocation(newRelativeLocation);
		// }
		//
		// if (j.contains("relativeRotation")) {
		// 	Vec3 newRelativeRotation;
		// 	TypeSerializer<Vec3>::Deserialize(dc, j["relativeRotation"], &newRelativeRotation);
		// 	result->GetRaw()->SetRelativeRotation(newRelativeRotation);
		// }
		//
		// if (j.contains("relativeScale")) {
		// 	Vec3 newRelativeScale;
		// 	TypeSerializer<Vec3>::Deserialize(dc, j["relativeScale"], &newRelativeScale);
		// 	result->GetRaw()->SetRelativeScale(newRelativeScale);
		// }

		if (!j.contains("children")) return;
		for (const auto& child : j["children"]) {
			DeserializeWorldComponent(dc, child, *result, parentObject);
		}
		return;
	}
	TUsePointer<WorldComponent> newComponent = mObjectManager->CreateObject(componentClass);
	newComponent->SetComponentName(componentName);

	TypeSerializer<TypeInfo*>::Deserialize(dc, j, componentClass, newComponent.GetRaw());

	parentObject->RegisterComponent(newComponent->GetObjectHandle());

	// AddComponent hangs the component off the object; the JSON nesting is what says otherwise.
	// KeepRelative — the relative transform read below is authored against the attach point.
	if (parentComponent) {
		newComponent->AttachTo(parentComponent.GetRaw(), EAttachmentRule::KeepRelative);
	}

	if (j.contains("relativeLocation")) {
		Vec3 newRelativeLocation;
		TypeSerializer<Vec3>::Deserialize(dc, j["relativeLocation"], &newRelativeLocation);
		newComponent->SetRelativeLocation(newRelativeLocation);
	}

	if (j.contains("relativeRotation")) {
		Vec3 newRelativeRotation;
		TypeSerializer<Vec3>::Deserialize(dc, j["relativeRotation"], &newRelativeRotation);
		newComponent->SetRelativeRotation(newRelativeRotation);
	}

	if (j.contains("relativeScale")) {
		Vec3 newRelativeScale;
		TypeSerializer<Vec3>::Deserialize(dc, j["relativeScale"], &newRelativeScale);
		newComponent->SetRelativeScale(newRelativeScale);
	}

	if (!j.contains("children")) return;
	for (const auto& child : j["children"]) {
		DeserializeWorldComponent(dc, child, newComponent, parentObject);
	}
}

#ifdef PLU_ENGINE_EDITOR_BUILD
JSON Plu::SceneManager::SerializeActiveScene(JSON base)
{
	PLU_PROFILE_SCOPE("SerializeActiveScene");
	base["gameModeClass"] = mActiveScene->GameModeClass.GetRawType()->TypeName.CStr();
	base["gameObjects"].clear();
	Vec3 cameraLoc;
	DispatchEvent("EditorCameraLocationToSave", &cameraLoc);
	Vec3 cameraRot;
	DispatchEvent("EditorCameraRotationToSave", &cameraRot);
	base["editorCameraLocation"] = TypeSerializer<Vec3>::Serialize(&cameraLoc);
	base["editorCameraRotation"] = TypeSerializer<Vec3>::Serialize(&cameraRot);
	auto gameObjects = mActiveScene->GetAllGameObjects();
	for (const auto& gameObject : gameObjects) {
		base["gameObjects"].push_back(TypeSerializer<TUsePointer<GameObject>>::Serialize(const_cast<TUsePointer<GameObject>*>(&gameObject)));
	}
	return base;
}

void Plu::SceneManager::SaveActiveScene()
{
	PLU_PROFILE_SCOPE("SaveActiveScene");
	PathW scenePath = mAssetManager->GetAssetPath(mActiveScene->Info->Uuid).ToString().ToWide();
	// Start from the file so keys of the scene asset that we do not own survive the round-trip.
	nlohmann::json base;
	base = DiskManager::LoadJson(scenePath);
	JSON json = SerializeActiveScene(std::move(base));
	PLU_PROFILE_SCOPE("SaveActiveScene Write");
	DiskManager::SaveJson(scenePath.ToString(), json);
}
#endif

void Plu::SceneManager::LoadSceneFromFile(TUsePointer<SceneWorld> sceneWorld)
{
	PLU_PROFILE_SCOPE("LoadSceneFromFile");
	JSON j;
	{
		PLU_PROFILE_SCOPE("LoadSceneFromFile Parse");
		j = DiskManager::LoadJson(mAssetManager->GetAssetPath(sceneWorld->Info->Uuid).ToString().ToWide());
	}
	LoadSceneFromJson(sceneWorld, j);
}

void Plu::SceneManager::LoadSceneFromJson(TUsePointer<SceneWorld> sceneWorld, const JSON& j)
{
	PLU_PROFILE_SCOPE("LoadSceneFromJson");
	if (j.contains("gameModeClass")) {
		sceneWorld->GameModeClass = TypeRegistry::GetInstance()->GetTypeOfName(j["gameModeClass"].get<std::string>().c_str());
	}
	if (j.contains("editorCameraLocation") && j.contains("editorCameraRotation")) {
		Vec3 location;
		Vec3 rotation;
		TypeSerializer<Vec3>::Deserialize(nullptr, j["editorCameraLocation"], &location);
		TypeSerializer<Vec3>::Deserialize(nullptr, j["editorCameraRotation"], &rotation);
		DispatchEvent("EditorCameraLocationLoaded", &location);
		DispatchEvent("EditorCameraRotationLoaded", &rotation);
	}
	if (!j.contains("gameObjects")) return;
	DeserializationContext dc = MakeDeserializationContext();
	for (const auto& obj : j["gameObjects"]) {
		LoadGameObjectFromJSON(&dc, sceneWorld, obj);
	}
	// After the whole batch: an attachment may point forward, at an object later in the array.
	ResolvePendingAttachments(sceneWorld);
}

void Plu::SceneManager::CloneSceneInto(TUsePointer<SceneWorld> target, TUsePointer<SceneWorld> source)
{
	PLU_PROFILE_SCOPE("CloneSceneInto");
	if (!target || !source) return;
	target->GameModeClass = source->GameModeClass;
	// No editor-camera events here, unlike LoadSceneFromJson: that path replays the camera stored in
	// the scene, but a clone's "stored" camera would be the live editor camera itself, so replaying
	// it would only move the camera to where it already is.
	DynamicArray<TUsePointer<GameObject>> sourceObjects = source->GetAllGameObjects();
	// Clones get fresh UUIDs, so attachments cannot be rebuilt by looking the parent up by UUID in
	// the target world — the source object's UUID is the only stable key between the two worlds.
	HashMap<UInt64, TUsePointer<GameObject>> clonesBySourceUuid;
	for (const auto& sourceObject : sourceObjects) {
		TUsePointer<GameObject> clone = CloneGameObjectInto(target, sourceObject);
		if (clone) clonesBySourceUuid.Insert(sourceObject->GetObjectUUID().getUUID(), clone);
	}

	// Second pass, once every clone exists: a parent may have been cloned after its child.
	for (const auto& sourceObject : sourceObjects) {
		if (!sourceObject || !sourceObject->IsAttached()) continue;
		TUsePointer<GameObject>* clone = clonesBySourceUuid.Find(sourceObject->GetObjectUUID().getUUID());
		TUsePointer<GameObject> sourceParent = sourceObject->GetAttachParentObject();
		if (!clone || !sourceParent) continue;
		TUsePointer<GameObject>* parentClone = clonesBySourceUuid.Find(sourceParent->GetObjectUUID().getUUID());
		if (!parentClone) continue;

		TUsePointer<WorldComponent> sourceParentComponent = sourceObject->GetAttachParentComponent();
		if (!sourceParentComponent) {
			(*clone)->AttachToObject(parentClone->GetRaw(), EAttachmentRule::KeepRelative);
			continue;
		}
		// Components are matched by name, exactly like CloneWorldComponent matches them.
		const String componentName = sourceParentComponent->GetComponentName();
		const DynamicArray<TUsePointer<WorldComponent>>* components = (*parentClone)->GetObjectWorldComponents();
		const TUsePointer<WorldComponent>* parentComponentClone = components->FindIf([&componentName](TUsePointer<WorldComponent> comp) -> bool {
			return comp && comp->GetComponentName() == componentName;
		});
		if (parentComponentClone == components->End()) continue;
		(*clone)->AttachToComponent(parentComponentClone->GetRaw(), sourceObject->GetAttachSocketName(), EAttachmentRule::KeepRelative);
	}
}

Plu::TUsePointer<Plu::GameObject> Plu::SceneManager::CloneGameObjectInto(TUsePointer<SceneWorld> targetWorld, TUsePointer<GameObject> source)
{
	PLU_PROFILE_SCOPE("CloneGameObject");
	if (!source) return nullptr;
	// Unnamed: the name is a reflected property, so it arrives with the property copy below — same
	// reasoning as the JSON path, where generating a default name would be wasted work.
	TUsePointer<GameObject> clone = targetWorld->SpawnGameObjectUnnamed(source->GetClass());
	if (!clone) return nullptr;

	CopyReflectedProperties(source->GetClass(), source.GetRaw(), clone.GetRaw());
	// Relative, like the JSON path: the attachment itself is rebuilt by CloneSceneInto's second
	// pass, and the world transform follows from it.
	clone->SetRelativeLocation(source->GetRelativeLocation());
	clone->SetRelativeRotation(source->GetRelativeRotation());
	clone->SetRelativeScale(source->GetRelativeScale());

	for (const auto& worldComp : source->GetDirectlyAttachedWorldComponents()) {
		CloneWorldComponent(worldComp, nullptr, clone);
	}

	DynamicArray<TOwningPointer<GameObjectComponent>>* cloneComponents = clone->GetObjectComponents();
	for (const auto& sourceComp : *source->GetObjectComponents()) {
		const String componentName = sourceComp->GetComponentName();
		TOwningPointer<GameObjectComponent>* existing = cloneComponents->FindIf([&componentName](TOwningPointer<GameObjectComponent> find)->bool {
			return find->GetComponentName() == componentName;
		});
		if (existing != cloneComponents->End()) {
			CopyReflectedProperties(sourceComp->GetClass(), sourceComp.GetRaw(), existing->GetRaw());
			continue;
		}
		TUsePointer<GameObjectComponent> newComponent = clone->AddComponent(sourceComp->GetClass(), "comp");
		if (!newComponent) continue;
		CopyReflectedProperties(sourceComp->GetClass(), sourceComp.GetRaw(), newComponent.GetRaw());
	}
	return clone;
}

void Plu::SceneManager::CloneWorldComponent(TUsePointer<WorldComponent> source, TUsePointer<WorldComponent> parentComponent, TUsePointer<GameObject> targetObject)
{
	PLU_PROFILE_SCOPE("CloneWorldComponent");
	if (!source) return;
	const String componentName = source->GetComponentName();
	DynamicArray<TUsePointer<WorldComponent>> componentsToSearchIn = parentComponent ? parentComponent->GetChildren() : targetObject->GetDirectlyAttachedWorldComponents();
	TUsePointer<WorldComponent>* existing = componentsToSearchIn.FindIf([&componentName](TUsePointer<WorldComponent> comp) -> bool {
		return componentName == comp->GetComponentName();
	});

	TUsePointer<WorldComponent> target;
	if (existing != componentsToSearchIn.End()) {
		// A component the object built for itself in OnSetupComponents. DeserializeWorldComponent
		// deliberately leaves those untouched (its property/transform block is commented out) and
		// only walks into their children — mirrored here, so switching PIE to cloning changes speed
		// and nothing else.
		target = *existing;
	} else {

		TUsePointer<WorldComponent> newComponent = mObjectManager->CreateObject(source->GetClass());
		newComponent->SetComponentName(componentName);

		CopyReflectedProperties(source->GetClass(), source.GetRaw(), newComponent.GetRaw());

		targetObject->RegisterComponent(newComponent->GetObjectHandle());

		target = newComponent;
		if (!target) return;
		// Mirror the source's attachment — AddComponent puts every component directly on the object.
		if (parentComponent) {
			target->AttachTo(parentComponent.GetRaw(), EAttachmentRule::KeepRelative);
		}
		target->SetRelativeLocation(source->GetRelativeLocation());
		target->SetRelativeRotation(source->GetRelativeRotation());
		target->SetRelativeScale(source->GetRelativeScale());
	}

	for (const auto& child : source->GetChildren()) {
		CloneWorldComponent(child, target, targetObject);
	}
}

Plu::DeserializationContext Plu::SceneManager::MakeDeserializationContext()
{
	DeserializationContext dc;
	dc.assetManager = mAssetManager;
	dc.scenesManager = mObjectManager->GetObjectAsUser<SceneManager>(*this->GetEngineObjectHandle());
	dc.shaderManager = mShaderManager;
	return dc;
}

void Plu::SceneManager::LoadGameObjectFromJSON(TUsePointer<SceneWorld> sceneWorld, JSON j)
{
	DeserializationContext dc = MakeDeserializationContext();
	LoadGameObjectFromJSON(&dc, sceneWorld, j);
	// Single-object path (editor duplicate / spawn from JSON): the parent, if any, is an object that
	// already lives in the scene, so the pending entry can be resolved right away.
	ResolvePendingAttachments(sceneWorld);
}

void Plu::SceneManager::ResolvePendingAttachments(const TUsePointer<SceneWorld>& sceneWorld)
{
	if (mPendingAttachments.IsEmpty()) return;
	PLU_PROFILE_SCOPE("ResolvePendingAttachments");
	DynamicArray<PendingAttachment> pending = std::move(mPendingAttachments);
	mPendingAttachments.Clear();
	if (!sceneWorld) return;

	for (const auto& entry : pending) {
		if (!entry.Object) continue;
		TUsePointer<GameObject> parentObject = sceneWorld->GetGameObjectByUUID(PluUUID(entry.ParentUuid));
		if (!parentObject) {
			PLU_CORE_ERROR("Attachment of '{}' dropped — parent object {} is not in the scene.",
				entry.Object->GetObjectName().CStr(), entry.ParentUuid);
			continue;
		}
		if (entry.ParentComponentName.IsEmpty()) {
			entry.Object->AttachToObject(parentObject.GetRaw(), entry.Rule);
			continue;
		}
		// Flattened list: the attach parent may be a component nested under another component.
		const DynamicArray<TUsePointer<WorldComponent>>* components = parentObject->GetObjectWorldComponents();
		const TUsePointer<WorldComponent>* parentComponent = components->FindIf([&entry](TUsePointer<WorldComponent> comp) -> bool {
			return comp && comp->GetComponentName() == entry.ParentComponentName;
		});
		if (parentComponent == components->End()) {
			PLU_CORE_ERROR("Attachment of '{}' dropped — '{}' has no component named '{}'.",
				entry.Object->GetObjectName().CStr(), parentObject->GetObjectName().CStr(), entry.ParentComponentName.CStr());
			continue;
		}
		entry.Object->AttachToComponent(parentComponent->GetRaw(), entry.SocketName, entry.Rule);
	}
}

Plu::SceneManager::PendingAttachment::PendingAttachment() : Rule(EAttachmentRule::KeepRelative)
{
}

void Plu::SceneManager::LoadGameObjectFromJSON(DeserializationContext* dc, TUsePointer<SceneWorld> sceneWorld, const JSON& j)
{
	PLU_PROFILE_SCOPE("LoadGameObjectFromJSON");
	if (!j.contains("typeName")) {
		PLU_ERROR("GameObject JSON without a typeName — skipping.");
		return;
	}
	// When the JSON carries a name, skip generating a default one: MakeDefaultObjectName scans the
	// whole scene (O(n^2) over a load) and the deserialization below overwrites the result anyway.
	// Scenes saved before mObjectName existed have no such field and still get the default name.
	bool hasSavedName = false;
	ReadStringFieldFromJson(j, "mObjectName", &hasSavedName);
	TypeInfo* objectClass = TypeRegistry::GetInstance()->GetTypeOfName(j["typeName"].get<std::string>().c_str());
	// The stored UUID is the object's identity: attachments are saved as parentUuid, and the world's
	// renderable/physics side tables are keyed by it. Ignoring it (which this path used to do) meant
	// every load handed out fresh UUIDs and every saved attachment was dropped by
	// ResolvePendingAttachments. Paths that deliberately want a new object — the editor's Duplicate —
	// overwrite j["uuid"] with a fresh one before calling in here.
	PluUUID savedUuid;
	const bool hasSavedUuid = j.contains("uuid");
	if (hasSavedUuid) {
		TypeSerializer<PluUUID>::Deserialize(dc, j["uuid"], &savedUuid);
	}
	TUsePointer<GameObject> gameObject = hasSavedUuid
		? sceneWorld->SpawnGameObjectWithUuid(objectClass, savedUuid)
		: (hasSavedName
			? sceneWorld->SpawnGameObjectUnnamed(objectClass)
			: sceneWorld->SpawnGameObject(objectClass));
	// SpawnGameObjectWithUuid does not generate a name either, so an old scene that stores a UUID but
	// no name still gets the default one.
	if (hasSavedUuid && !hasSavedName && gameObject) {
		gameObject->SetObjectName(sceneWorld->MakeDefaultObjectName(objectClass));
	}
	if (!gameObject) {
		PLU_ERROR("No GameObject class of name {}! Maybe some python scripts were not run!", j["typeName"].get<std::string>().c_str());
		return;
	}
	TypeSerializer<TypeInfo*>::Deserialize(dc, j, gameObject->GetClass(), gameObject.GetRaw());
	// j is a const reference now, so a missing key can no longer be silently materialised as null
	// the way it was when this took the JSON by value — every optional key is checked.
	// Relative setters, matching what the serializer wrote. For an unattached object this is the
	// world transform, so scenes without attachments load exactly as before.
	if (j.contains("location")) {
		Vec3 loc;
		TypeSerializer<Vec3>::Deserialize(dc, j["location"], &loc);
		gameObject->SetRelativeLocation(loc);
	}
	if (j.contains("rotation")) {
		Vec3 rot;
		TypeSerializer<Vec3>::Deserialize(dc, j["rotation"], &rot);
		gameObject->SetRelativeRotation(rot);
	}
	if (j.contains("scale")) {
		Vec3 scl;
		TypeSerializer<Vec3>::Deserialize(dc, j["scale"], &scl);
		gameObject->SetRelativeScale(scl);
	}
	if (j.contains("attachment") && j["attachment"].contains("parentUuid")) {
		PendingAttachment pending;
		pending.Object = gameObject;
		PluUUID parentUuid;
		TypeSerializer<PluUUID>::Deserialize(dc, j["attachment"]["parentUuid"], &parentUuid);
		pending.ParentUuid = parentUuid.getUUID();
		if (j["attachment"].contains("componentName")) {
			pending.ParentComponentName = j["attachment"]["componentName"].get<std::string>().c_str();
		}
		if (j["attachment"].contains("socket")) {
			pending.SocketName = j["attachment"]["socket"].get<std::string>().c_str();
		}
		mPendingAttachments.PushBack(pending);
	}
	if (j.contains("worldComponents")) {
		for (const auto& worldComp : j["worldComponents"]) {
			DeserializeWorldComponent(dc, worldComp, nullptr, gameObject);
		}
	}

	if (!j.contains("components")) return;
	for (const auto& comp : j["components"]) {
		// The name used to be obtained by constructing a scratch component, deserializing the whole
		// JSON into it and destroying it again — for every component in the scene. It is a plain
		// reflected field, so read it out of the JSON directly.
		const String componentName = ReadStringFieldFromJson(comp, "mComponentName");
		TOwningPointer<GameObjectComponent>* findComp = gameObject->GetObjectComponents()->FindIf([&componentName](TOwningPointer<GameObjectComponent> find)->bool {
			return find->GetComponentName() == componentName;
		});
		if (findComp != gameObject->GetObjectComponents()->End()) {
			TOwningPointer<WorldComponent> compToPopulate = *findComp;
			TypeSerializer<TypeInfo*>::Deserialize(dc, comp, compToPopulate->GetClass(), compToPopulate.GetRaw());
			continue;
		}
		if (!comp.contains("typeName")) continue;
		TUsePointer<GameObjectComponent> component = gameObject->AddComponent(TypeRegistry::GetInstance()->GetTypeOfName(comp["typeName"].get<std::string>().c_str()), "comp");
		if (!component) continue;
		TypeSerializer<TypeInfo*>::Deserialize(dc, comp, component->GetClass(), component.GetRaw());
	}
}
#ifdef PLU_ENGINE_EDITOR_BUILD
void Plu::SceneManager::CapturePythonReloadRiders(const TUsePointer<GameObject>& parent, const HashSet<UInt64>& victimUuids)
{
	if (!parent) return;
	const UInt64 parentUuid = parent->GetObjectUUID();
	auto queueRider = [this, parentUuid, &victimUuids](const TUsePointer<GameObject>& rider, const String& parentComponentName) {
		if (!rider) return;
		// A rider that is a victim itself already carries its attachment in its own JSON.
		if (victimUuids.Contains(rider->GetObjectUUID())) return;
		PendingAttachment pending;
		pending.Object = rider;
		pending.ParentUuid = parentUuid;
		pending.ParentComponentName = parentComponentName;
		pending.SocketName = rider->GetAttachSocketName();
		// KeepWorld, unlike a scene load: by the time this is applied the rider has been detached with
		// KeepWorld (GameObject::Cleanup / WorldComponent::Cleanup), so its stored transform is a world
		// one. The recreated parent lands on the same transform, so recomputing gives back exactly the
		// offset the rider had before the reload.
		pending.Rule = EAttachmentRule::KeepWorld;
		mPendingAttachments.PushBack(pending);
	};

	for (const auto& rider : parent->GetAttachedObjects()) {
		queueRider(rider, "");
	}
	// Flattened list: a rider may hang off a component nested under another component.
	for (const auto& component : *parent->GetObjectWorldComponents()) {
		if (!component) continue;
		for (const auto& rider : component->GetAttachedObjects()) {
			queueRider(rider, component->GetComponentName());
		}
	}
}

UInt32 Plu::SceneManager::ReloadPythonComponents(DeserializationContext* dc, const TUsePointer<GameObject>& object, const DynamicArray<String>& componentClasses)
{
	if (!object) return 0;

	// Collected up front: deleting or adding a component invalidates the object's flattened
	// component cache, so the lists cannot be walked while they are being mutated.
	DynamicArray<TUsePointer<WorldComponent>> worldHits;
	for (const auto& component : *object->GetObjectWorldComponents()) {
		if (!component) continue;
		if (!componentClasses.Contains(component->GetClass()->TypeName)) continue;
		worldHits.PushBack(component);
	}
	// A hit nested under another hit is recreated together with its ancestor (the whole subtree
	// travels in that component's JSON), so it must not be processed on its own.
	DynamicArray<TUsePointer<WorldComponent>> worldRoots;
	for (const auto& hit : worldHits) {
		const bool nested = worldHits.FindIf([&hit](const TUsePointer<WorldComponent>& other) -> bool {
			return other && other != hit && hit->IsAttachedTo(other.GetRaw());
		}) != worldHits.End();
		if (!nested) worldRoots.PushBack(hit);
	}

	DynamicArray<TUsePointer<GameObjectComponent>> plainHits;
	for (const auto& component : *object->GetObjectComponents()) {
		if (!component) continue;
		if (!componentClasses.Contains(component->GetClass()->TypeName)) continue;
		plainHits.PushBack(component);
	}

	UInt32 recreated = 0;
	for (const auto& component : worldRoots) {
		if (!component) continue;
		const JSON componentJson = component->Serialize();
		TUsePointer<WorldComponent> parentComponent = component->GetParentComponent();
		// Objects riding the component — or anything under it, since the whole subtree goes down with
		// it — survive it, same as with a whole object. Component names are preserved by the rebuild, so
		// each rider can be re-hung on the component of the same name.
		for (const auto& subComponent : *object->GetObjectWorldComponents()) {
			if (!subComponent) continue;
			if (subComponent != component && !subComponent->IsAttachedTo(component.GetRaw())) continue;
			for (const auto& rider : subComponent->GetAttachedObjects()) {
				if (!rider) continue;
				PendingAttachment pending;
				pending.Object = rider;
				pending.ParentUuid = object->GetObjectUUID();
				pending.ParentComponentName = subComponent->GetComponentName();
				pending.SocketName = rider->GetAttachSocketName();
				pending.Rule = EAttachmentRule::KeepWorld;
				mPendingAttachments.PushBack(pending);
			}
		}
		if (!object->DeleteComponent(component.GetRaw())) continue;
		try {
			// The load path, unchanged: creates by typeName, re-attaches to the same attach point,
			// replays reflected properties and the relative transform, and recurses into children.
			DeserializeWorldComponent(dc, componentJson, parentComponent, object);
			++recreated;
		} catch (pybind11::error_already_set& e) {
			PLU_CORE_ERROR("Python hot reload: recreating world component of '{}' failed -> {}", object->GetObjectName().CStr(), e.what());
		}
	}

	for (const auto& component : plainHits) {
		if (!component) continue;
		const JSON componentJson = TypeSerializer<TypeInfo*>::Serialize(component->GetClass(), component.GetRaw());
		const String componentName = component->GetComponentName();
		// The TypeInfo itself is reused across a reload (RegisterPluClass updates it in place), so this
		// already points at the freshly imported python class.
		TypeInfo* componentClass = component->GetClass();
		if (!object->DeleteComponent(component.GetRaw())) continue;
		try {
			TUsePointer<GameObjectComponent> newComponent = object->AddComponent(componentClass, componentName);
			if (!newComponent) continue;
			TypeSerializer<TypeInfo*>::Deserialize(dc, componentJson, newComponent->GetClass(), newComponent.GetRaw());
			++recreated;
		} catch (pybind11::error_already_set& e) {
			PLU_CORE_ERROR("Python hot reload: recreating component '{}' of '{}' failed -> {}",
				componentName.CStr(), object->GetObjectName().CStr(), e.what());
		}
	}
	return recreated;
}

void Plu::SceneManager::ReloadPythonInstances(const DynamicArray<String>& typeNames)
{
	PLU_PROFILE_SCOPE("ReloadPythonInstances");
	if (typeNames.IsEmpty()) return;
	if (mIsInPIE) {
		PLU_CORE_WARN("Python hot reload skipped — stop PIE first (runtime state cannot be rebuilt from reflected properties).");
		return;
	}
	TUsePointer<SceneWorld> world = mActiveScene;
	if (!world) return;

	DynamicArray<String> objectClasses;
	DynamicArray<String> componentClasses;
	for (const auto& typeName : typeNames) {
		TypeInfo* type = TypeRegistry::GetInstance()->GetTypeOfName(typeName);
		if (!type) continue;
		if (type->IsDerivedOfOrSame(GameObject::GetStaticClass())) {
			objectClasses.PushBack(typeName);
		} else if (type->IsDerivedOfOrSame(GameObjectComponent::GetStaticClass())) {
			componentClasses.PushBack(typeName);
		} else {
			PLU_CORE_WARN("Python hot reload: '{}' is neither a GameObject nor a GameObjectComponent — live instances left untouched.", typeName.CStr());
		}
	}
	if (objectClasses.IsEmpty() && componentClasses.IsEmpty()) return;

	DeserializationContext dc = MakeDeserializationContext();

	// --- Objects: serialize, destroy, spawn again under the same UUID -------------------------
	DynamicArray<TUsePointer<GameObject>> allObjects = world->GetAllGameObjects();
	HashSet<UInt64> victimUuids;
	for (const auto& object : allObjects) {
		if (!object || !object->GetClass()) continue;
		if (!objectClasses.Contains(object->GetClass()->TypeName)) continue;
		victimUuids.Insert(object->GetObjectUUID());
	}

	DynamicArray<JSON> capturedObjects;
	DynamicArray<EngineObjectHandle> toDestroy;
	for (const auto& object : allObjects) {
		if (!object || !victimUuids.Contains(object->GetObjectUUID())) continue;
		// Carries reflected properties, the object name, the relative transform, the UUID, the
		// attachment to its own parent and the whole component tree.
		TUsePointer<GameObject> objectToSerialize = object;
		capturedObjects.PushBack(TypeSerializer<TUsePointer<GameObject>>::Serialize(&objectToSerialize));
		CapturePythonReloadRiders(object, victimUuids);
		toDestroy.PushBack(object->GetObjectHandle());
	}

	if (!toDestroy.IsEmpty()) {
		for (const auto& handle : toDestroy) {
			world->DeleteGameObject(handle);
		}
		// Synchronous on purpose: the respawn below reuses these UUIDs, and they are the key into
		// mGameObjects and the renderable side tables.
		world->FlushPendingDestroys();
	}

	UInt32 recreatedObjects = 0;
	for (const auto& objectJson : capturedObjects) {
		try {
			// Private overload: pending attachments accumulate and are resolved once below, because a
			// victim's parent may itself be one of the objects being recreated here.
			LoadGameObjectFromJSON(&dc, world, objectJson);
			++recreatedObjects;
		} catch (pybind11::error_already_set& e) {
			PLU_CORE_ERROR("Python hot reload: respawning object of class '{}' failed -> {}",
				objectJson.contains("typeName") ? objectJson["typeName"].get<std::string>().c_str() : "?", e.what());
		}
	}

	// --- Components on objects that survived --------------------------------------------------
	UInt32 recreatedComponents = 0;
	if (!componentClasses.IsEmpty()) {
		for (const auto& object : world->GetAllGameObjects()) {
			if (!object) continue;
			// Objects recreated above already built their components from the new classes.
			if (victimUuids.Contains(object->GetObjectUUID())) continue;
			recreatedComponents += ReloadPythonComponents(&dc, object, componentClasses);
		}
	}

	ResolvePendingAttachments(world);

	if (recreatedObjects == 0 && recreatedComponents == 0) return;
	PLU_CORE_INFO("Python hot reload: {} object(s) and {} component(s) recreated.", recreatedObjects, recreatedComponents);
	world->GetObjectEventDispatcher()->Dispatch("GameObjectsChanged", nullptr);
}
#endif
