//
// Created by Plutex on 5/29/26.
//

#include "PluEngine/Gameplay/Scenes/SceneWorld.h"
#include "PluEngine/Render/RenderParticleLiveness.h"
#include "PluEngine/Timer.h"
#include "HashSet/HashSet.h"
#include "PluEngine/Timer.h"
#include "../../include/PluEngine/Gameplay/Components/PhysicsColliderComponent.h"
#include "PluEngine/Gameplay/Components/StaticMeshComponent.h"
#include "PluEngine/Gameplay/Components/InstancedStaticMeshComponent.h"
#include "PluEngine/Gameplay/Components/SkeletalMeshComponent.h"
#include "PluEngine/Gameplay/Objects/PlayerStart.h"
#include "PluEngine/Gameplay/Objects/SpectatorPuppet.h"
#include "PluEngine/Gameplay/Objects/Lights/DirectionalLight.h"
#include "PluEngine/Gameplay/Objects/Lights/SpotLight.h"
#include "PluEngine/Gameplay/GameClient.h"
#include "PluEngine/Gameplay/Scenes/ScenesManager.h"
#include "PluEngine/Gameplay/GameObject.h"
#include "PluEngine/Gameplay/GameObjectComponent.h"
#include "PluEngine/Gameplay/WorldComponent.h"
#include "PluEngine/Gameplay/InputManager.h"
#include "PluEngine/Core/Objects/EngineObjectManager.h"
#include "PluEngine/Gameplay/Components/ParticleSpawnerComponent.h"
#include "PluEngine/Render/RenderingInterfaces.h"

namespace Plu
{
	SceneWorld::~SceneWorld()
	{
	}

	void SceneWorld::AddDebugLine(Vec3 start, Vec3 end, Vec3 color)
	{
		mDebugLineVerts.PushBack(start.x);
		mDebugLineVerts.PushBack(start.y);
		mDebugLineVerts.PushBack(start.z);

		mDebugLineVerts.PushBack(color.r);
		mDebugLineVerts.PushBack(color.g);
		mDebugLineVerts.PushBack(color.b);

		mDebugLineVerts.PushBack(end.x);
		mDebugLineVerts.PushBack(end.y);
		mDebugLineVerts.PushBack(end.z);

		mDebugLineVerts.PushBack(color.r);
		mDebugLineVerts.PushBack(color.g);
		mDebugLineVerts.PushBack(color.b);
	}

	void SceneWorld::AddDebugPoint(Vec3 point, Vec3 color)
	{
		mDebugPointVerts.PushBack(point.x);
		mDebugPointVerts.PushBack(point.y);
		mDebugPointVerts.PushBack(point.z);

		mDebugPointVerts.PushBack(color.r);
		mDebugPointVerts.PushBack(color.g);
		mDebugPointVerts.PushBack(color.b);
	}

	DynamicArray<float> * SceneWorld::GetRawDebugPointArray()
	{
		return &mDebugPointVerts;
	}

	DynamicArray<float> * SceneWorld::GetRawDebugLineArray()
	{
		return &mDebugLineVerts;
	}

	void SceneWorld::Init(const TUsePointer<EngineObjectManager> &engineObjectManager, const TUsePointer<GameClient>& client)
	{
		mEngineObjectManager = engineObjectManager;
		mClient = client;
	}

	void SceneWorld::LoadGameObjects()
	{
	}

	void SceneWorld::UnloadGameObjects()
	{
		PLU_CORE_WARN("Unloading Game Objects (Shutdown) - Scene: {}", Info ? Info->URL.CStr() : GetDisplayName().CStr());
		for (const auto& gObj : mGameObjects) {
			mGameObjects[gObj.first]->OnEndPlay();
		}
		mGameMode = nullptr;
		mControllers.Clear();
		mObjectsToDestroy.Clear();
		mObjectsToBegin.Clear();
		mPendingSpawns.Clear();
		for (const auto& gObj : mGameObjects) {
			DeleteGameObject(*gObj.second->GetEngineObjectHandle(), false);
		}
		HandleDestroy();
		mPhysicsWorld = nullptr;
		mGameObjects.Clear();
	}

	void SceneWorld::Play()
	{
		mGameMode = SpawnGameObject(GameModeClass.GetRawType());
		HandleBeginPlay();
		mIsPlaying = true;
	}

	void SceneWorld::HandleBeginPlay()
	{
		// Move the batch aside first: OnBeginPlay may spawn objects, which pushes into
		// mObjectsToBegin and would reallocate the array we are iterating. Those objects begin
		// play on the next pass.
		DynamicArray<TUsePointer<GameObject>> batch = std::move(mObjectsToBegin);
		mObjectsToBegin.Clear();
		for (const auto& obj : batch) {
			if (!obj) continue;
			obj->OnBeginPlay();
			// Flattened list, so components attached under another component begin play too.
			// Copied out: OnBeginPlay may add or reparent components, which rebuilds the cache.
			DynamicArray<TUsePointer<WorldComponent>> worldComps = *obj->GetObjectWorldComponents();
			for (const auto& worldComp : worldComps) {
				if (!worldComp) continue;
				worldComp->OnBeginPlay();
			}
			for (auto comp : obj->mComponents) {
				comp->OnBeginPlay();
			}
			// Directional light is now registered at spawn time (see SpawnGameObject), so the
			// editor preview gets lighting/shadows without play. Nothing to do here.
		}
	}

	void SceneWorld::HandleDestroy()
	{
		bool destroyedSmth = false;
		// Same reason as in HandleBeginPlay: OnEndPlay may destroy further objects, which pushes
		// into mObjectsToDestroy mid-iteration. Those are handled on the next pass.
		DynamicArray<std::pair<TUsePointer<GameObject>, bool>> batch = std::move(mObjectsToDestroy);
		mObjectsToDestroy.Clear();
		for (const auto& obj : batch) {
			if (!obj.first) continue;
			if (!mGameObjects.Contains(obj.first->GetObjectUUID())) {
				PLU_CORE_ERROR("Destroying Invalid GameObject!");
				continue;
			}
			destroyedSmth = true;
			TUsePointer<GameObject> object = obj.first;
			// Guarded like OnSetupComponents/OnUpdate: a throwing python OnEndPlay used to escape the
			// whole loop, and the rest of the batch was already moved out of mObjectsToDestroy — those
			// objects would never be destroyed at all.
			if (obj.second) {
				try {
					object->OnEndPlay();
				} catch (pybind11::error_already_set& e) {
					PLU_CORE_ERROR("Error In Python OnEndPlay of object {}, what -> {}", object->GetDisplayName().CStr(), e.what());
				}
			}
			if (mStaticMeshRenderables.Contains(object->GetObjectUUID())) {
				mStaticMeshRenderables.Remove(object->GetObjectUUID());
			}
			if (mInstancedMeshRenderables.Contains(object->GetObjectUUID())) {
				mInstancedMeshRenderables.Remove(object->GetObjectUUID());
			}
			if (mSkeletalMeshRenderables.Contains(object->GetObjectUUID())) {
				mSkeletalMeshRenderables.Remove(object->GetObjectUUID());
			}
			if (object == mDirectionalLight) {
				mDirectionalLight = nullptr;
			}
			if (mSpotLights.Contains(object->GetObjectUUID())) {
				mSpotLights.Remove(object->GetObjectUUID());
			}
			// Spawners are keyed by component UUID, not object UUID. Removing them here is what makes
			// the render thread destroy their particle spawners.
			for (const auto& spawner : object->GetAllComponentsByClass(TClassPointer<GameObjectComponent>(ParticleSpawnerComponent::GetStaticClass()))) {
				if (spawner) mParticleSpawnerComponents.Remove(spawner->Uuid);
			}
			object->Cleanup();
			mGameObjects.Remove(object->mUuid);
			mEngineObjectManager->DestroyObject(*object->GetEngineObjectHandle());
		}
		// The per-class cache holds TUsePointers, which go null-like instead of dangling — but a
		// GetAllGameObjectsOfClass done between this destroy and the next spawn would still hand out
		// dead entries (only mNewGameObjectSpawned used to drop the cache).
		if (destroyedSmth) mGameObjectsPerClassCache.Clear();
#ifdef PLU_ENGINE_EDITOR_BUILD
		if (destroyedSmth) {
			PLU_CORE_WARN("Destroyed {} objects!", batch.Size());
			GetObjectEventDispatcher()->Dispatch("GameObjectsChanged", nullptr);
		}
#endif
	}

	void SceneWorld::FlushPendingDestroys()
	{
		// Inside the tick loop the queue is drained by TickScene itself, and destroying an object
		// there would mutate mGameObjects while it is being iterated.
		PLU_CORE_ASSERT(!mTickingGameObjects, "FlushPendingDestroys called from inside the tick loop")
		HandleDestroy();
	}

	void SceneWorld::FlushPendingSpawns()
	{
		if (mPendingSpawns.IsEmpty()) return;
		for (const auto& object : mPendingSpawns) {
			mGameObjects.Insert(object->mUuid, object);
		}
		mPendingSpawns.Clear();
	}

	TUsePointer<Controller> SceneWorld::GetControllerByID(UInt16 playerID)
	{
		return mControllers.Contains(playerID) ? mControllers[playerID] : nullptr;
	}

	void SceneWorld::TickScene(float deltaTime)
	{
		DispatchEvent("PhysicsTick", &deltaTime);
		PLU_TIMER_START("GameObjects Ticks");
		// A tick may spawn objects; while this flag is up SpawnGameObject parks them in
		// mPendingSpawns instead of inserting into the map we are iterating. They join the map
		// right after the loop and tick from the next frame on.
		mTickingGameObjects = true;
		for (const auto& gameObject : mGameObjects) {
			gameObject.second->TickObject(deltaTime);
		}
		mTickingGameObjects = false;
		FlushPendingSpawns();
		PLU_TIMER_END("GameObjects Ticks");
		HandleDestroy();
		HandleBeginPlay();
	}

	RaycastHitInfo SceneWorld::ShootRaycast(const Vec3& Start, const Vec3& End, const DynamicArray<GameObject*>& IgnoredObjects)
	{
		PLU_PROFILE_SCOPE("SceneWorld ShootRaycast");
		RaycastRequest request{Start, End, {}, {}};
		request.IgnoredObjectUuids.Reserve(IgnoredObjects.Size());
		for (GameObject* ignored : IgnoredObjects) {
			if (ignored) request.IgnoredObjectUuids.PushBack(ignored->GetObjectUUID());
		}
		DispatchEvent("Raycast", &request);
		return request.Result;
	}

	RaycastHitInfo SceneWorld::ShootRaycastInDirection(const Vec3& Start, const Vec3& Direction, float Length, const DynamicArray<GameObject*>& IgnoredObjects)
	{
		if (glm::length(Direction) <= 0.0f) return {};
		return ShootRaycast(Start, Start + glm::normalize(Direction) * Length, IgnoredObjects);
	}

	void SceneWorld::NewGameObjectComponent(const TOwningPointer<GameObjectComponent>& component)
	{
		component->OnSetupComponent();

		TUsePointer<GameObjectComponent> newComponent = component;
		DispatchEvent("NewComponent", &newComponent);

		if (component->GetClass()->IsDerivedOfOrSame(StaticMeshComponent::GetStaticClass())) {
			if (mStaticMeshRenderables.Contains(component->GetParentGameObject()->GetObjectUUID())) {
				mStaticMeshRenderables[component->GetParentGameObject()->GetObjectUUID()].PushBack(component);
			} else {
				mStaticMeshRenderables[component->GetParentGameObject()->GetObjectUUID()] = {component};
			}
		}
		if (component->GetClass()->IsDerivedOfOrSame(InstancedStaticMeshComponent::GetStaticClass())) {
			if (mInstancedMeshRenderables.Contains(component->GetParentGameObject()->GetObjectUUID())) {
				mInstancedMeshRenderables[component->GetParentGameObject()->GetObjectUUID()].PushBack(component);
			} else {
				mInstancedMeshRenderables[component->GetParentGameObject()->GetObjectUUID()] = {component};
			}
		}
		if (component->GetClass()->IsDerivedOfOrSame(SkeletalMeshComponent::GetStaticClass())) {
			if (mSkeletalMeshRenderables.Contains(component->GetParentGameObject()->GetObjectUUID())) {
				mSkeletalMeshRenderables[component->GetParentGameObject()->GetObjectUUID()].PushBack(component);
			} else {
				mSkeletalMeshRenderables[component->GetParentGameObject()->GetObjectUUID()] = {component};
			}
		}

		if (component->GetClass()->IsDerivedOfOrSame(ParticleSpawnerComponent::GetStaticClass())) {
			mParticleSpawnerComponents[component->Uuid] = component;
		}
	}

	void SceneWorld::UpdateParticleLiveness()
	{
		PLU_PROFILE_SCOPE("Particle Liveness");
		if (mParticleSpawnerComponents.IsEmpty()) return;

		ParticleLivenessFrame frame;
		ReadParticleLiveness(frame);
		// The channel carries the world the render thread ticked last. Anything else says nothing about
		// this world, and "no entry" must never be read as "finished".
		if (frame.SceneHandle != GetObjectHandle()) return;

		HashMap<UInt64, const ParticleSpawnerLiveness*> byUuid;
		for (const ParticleSpawnerLiveness& entry : frame.Spawners) byUuid.InsertOrAssign(entry.SpawnerUuid, &entry);

		for (auto& entry : mParticleSpawnerComponents) {
			ParticleSpawnerComponent* component = entry.second.GetRaw();
			if (!component) continue;
			const ParticleSpawnerLiveness* const* found = byUuid.Find(component->Uuid.getUUID());
			if (!found) continue;

			component->ApplyLiveness((*found)->AliveParticles, (*found)->CompletedActivationVersion);
			if (component->AutoDestroyWhenFinished && component->IsFinished()) {
				TUsePointer<GameObject> owner = component->GetParentGameObject();
				// Deferred: DeleteGameObject queues the destroy for the next scene update.
				if (owner) DeleteGameObject(owner->GetObjectHandle());
			}
		}
	}

	void SceneWorld::DeleteGameObjectComponent(const TOwningPointer<GameObjectComponent> &component)
	{
		TUsePointer<GameObjectComponent> oldComponent = component;
		DispatchEvent("DestroyComponent", &oldComponent);

		if (mStaticMeshRenderables.Contains(component->GetParentGameObject()->GetObjectUUID()) &&
			component->GetClass()->IsDerivedOfOrSame(StaticMeshComponent::GetStaticClass())) {
			mStaticMeshRenderables[component->GetParentGameObject()->GetObjectUUID()].Remove(component);
			return;
		}
		if (mSkeletalMeshRenderables.Contains(component->GetParentGameObject()->GetObjectUUID()) &&
			component->GetClass()->IsDerivedOfOrSame(SkeletalMeshComponent::GetStaticClass())) {
			mSkeletalMeshRenderables[component->GetParentGameObject()->GetObjectUUID()].Remove(component);
			return;
		}
		if (mInstancedMeshRenderables.Contains(component->GetParentGameObject()->GetObjectUUID()) &&
			component->GetClass()->IsDerivedOfOrSame(InstancedStaticMeshComponent::GetStaticClass())) {
			mInstancedMeshRenderables[component->GetParentGameObject()->GetObjectUUID()].Remove(component);
			return;
		}

		if (component->GetClass()->IsDerivedOfOrSame(ParticleSpawnerComponent::GetStaticClass())) {
			// Dropping it from the map is the whole destroy: the next snapshot no longer lists it,
			// and the render thread destroys the spawner.
			mParticleSpawnerComponents.Remove(component->Uuid);
		}
	}

	void SceneWorld::OnGameObjectScaleChanged(GameObject* gameObject)
	{
		if (!mIsPlaying || !gameObject) return;
		// Only objects that already have a physics body need their colliders rebuilt.
		if (!mEngineObjectManager->IsValid(gameObject->mPhysicsBodyHandle)) return;
	}

	void SceneWorld::OnComponentTransformChanged(GameObject* gameObject)
	{
		if (!mIsPlaying || !gameObject) return;
		// Deferred, unlike the scale path above: transform setters are called per frame while a
		// value is dragged in the editor, and a rebuild per call would be wasteful.
	}

	TUsePointer<GameObject> SceneWorld::SpawnGameObject(TClassPointer<GameObject> objectClass)
	{
		return SpawnGameObjectInternal(objectClass, true);
	}

	TUsePointer<GameObject> SceneWorld::SpawnGameObjectUnnamed(TClassPointer<GameObject> objectClass)
	{
		return SpawnGameObjectInternal(objectClass, false);
	}

	TUsePointer<GameObject> SceneWorld::SpawnGameObjectWithUuid(TClassPointer<GameObject> objectClass, PluUUID uuid)
	{
		return SpawnGameObjectInternal(objectClass, false, &uuid);
	}

	TUsePointer<GameObject> SceneWorld::SpawnGameObjectInternal(TClassPointer<GameObject> objectClass, bool generateDefaultName, const PluUUID* explicitUuid)
	{
		if (!objectClass) {
			PLU_CORE_ERROR("Invalid Class for spawning GameObject!");
			return nullptr;
		}
		TUsePointer<GameObject> newObjectUser = mEngineObjectManager->CreateObject(objectClass);
		TOwningPointer<GameObject> newObject = mEngineObjectManager->GetObjectAsOwner<GameObject>(newObjectUser->GetObjectHandle());
		PluUUID uuid;
		// A caller that wants the object to keep its identity (scene load, python hot reload) passes
		// the UUID in. It has to be assigned here, before OnSetupComponents runs: the renderable side
		// tables and the physics bookkeeping are keyed by it from inside NewGameObjectComponent.
		if (explicitUuid) {
			if (mGameObjects.Contains(*explicitUuid) || mPendingSpawns.FindIf([explicitUuid](const TOwningPointer<GameObject>& pending) -> bool {
					return pending && pending->mUuid == *explicitUuid;
				}) != mPendingSpawns.End()) {
				PLU_CORE_WARN("Spawn with UUID {} — already taken in this scene, falling back to a fresh one.", explicitUuid->getUUID());
			} else {
				uuid = *explicitUuid;
			}
		}
		newObject->mUuid = uuid;
		// Nazwa musi powstać przed wstawieniem do mGameObjects/mPendingSpawns, żeby obiekt
		// nie zobaczył samego siebie (pustej nazwy) przy zbieraniu zajętych indeksów.
		// Wołający, który zaraz nada nazwę sam (wczytywanie sceny z JSON-a), pomija ten krok —
		// patrz SpawnGameObjectUnnamed.
		if (generateDefaultName) {
			newObject->mObjectName = MakeDefaultObjectName(objectClass);
		}
		if (mTickingGameObjects) {
			mPendingSpawns.PushBack(newObject);
		} else {
			mGameObjects.Insert(uuid, newObject);
		}
		newObject->InitGameObject(mEngineObjectManager->GetObjectAsUser<SceneWorld>(*GetEngineObjectHandle()), mEngineObjectManager);
		try {
			newObject->OnSetupComponents();
		} catch (pybind11::error_already_set& e) {
			PLU_CORE_ERROR("Error has happened on SetupComponents phase on object {}, what -> {}", newObject->GetDisplayName().CStr(), e.what());
		}
		// Register the directional light at spawn time (independent of play state) so the editor
		// preview renders lighting/shadows without entering PIE. Mirrors how StaticMeshComponents
		// register in NewGameObjectComponent during OnSetupComponents. HandleBeginPlay no longer
		// owns this responsibility.
		if (newObject->GetClass()->IsDerivedOfOrSame(DirectionalLight::GetStaticClass())) {
			PLU_CORE_ASSERT(!mDirectionalLight, "There can be only one Directional Light in a scene")
			mDirectionalLight = mEngineObjectManager->GetObjectAsOwner<DirectionalLight>(newObject->GetObjectHandle());
		}
		// Same spawn-time registration, but no uniqueness assert — spot lights are a set, and how
		// many of them get a shadow map is decided per frame by the render thread's slot budget,
		// not by how many may exist.
		if (newObject->GetClass()->IsDerivedOfOrSame(SpotLight::GetStaticClass())) {
			mSpotLights.Insert(newObject->GetObjectUUID(), mEngineObjectManager->GetObjectAsOwner<SpotLight>(newObject->GetObjectHandle()));
		}
		mObjectsToBegin.PushBack(newObject);
		mNewGameObjectSpawned = true;
		GetObjectEventDispatcher()->Dispatch("GameObjectsChanged", nullptr);
		return newObject;
	}

	void SceneWorld::DeleteGameObject(EngineObjectHandle gameObject, bool callEndPlay)
	{
		TOwningPointer<GameObject> object = mEngineObjectManager->GetObjectAsOwner<GameObject>(gameObject);
		if (!object) return;
		mObjectsToDestroy.PushBack({object, callEndPlay});
	}

	void SceneWorld::DestroyGameObject(GameObject *gameObject)
	{
		DeleteGameObject(gameObject->GetObjectHandle());
	}

	DynamicArray<TUsePointer<GameObject>> SceneWorld::GetAllGameObjects()
	{
		DynamicArray<TUsePointer<GameObject>> result;
		result.Reserve(mGameObjects.Size());
		for (const std::pair<UInt64, TOwningPointer<GameObject>>& obj : mGameObjects) {
			result.PushBack(obj.second);
		}
		return result;
	}

	void SceneWorld::GetFormattedGameObjectNames(DynamicArray<String>* result)
	{
		result->Clear();
		result->Reserve(mGameObjects.Size());
		for (auto obj : mGameObjects) {
			// GetDisplayName() to fallback dla obiektów, które powstały z pominięciem
			// SpawnGameObject (nie dostały wtedy domyślnej nazwy).
			const String& name = obj.second->GetObjectName();
			result->PushBack(name.IsEmpty() ? obj.second->GetDisplayName() : name);
		}
	}

	bool SceneWorld::IsObjectNameTaken(const String& name) const
	{
		for (const auto& obj : mGameObjects) {
			if (obj.second->GetObjectName() == name) {
				return true;
			}
		}
		for (const auto& obj : mPendingSpawns) {
			if (obj->GetObjectName() == name) {
				return true;
			}
		}
		return false;
	}

	String SceneWorld::MakeDefaultObjectName(TClassPointer<GameObject> objectClass)
	{
		if (!objectClass) {
			return String();
		}
		return MakeDefaultObjectNameFromBase(objectClass.GetRawType()->TypeName);
	}

	String SceneWorld::MakeDefaultObjectNameFromBase(const String& base)
	{
		PLU_PROFILE_SCOPE("SceneWorld::MakeDefaultObjectNameFromBase");

		if (base.IsEmpty()) {
			return String();
		}

		// Jeden przebieg po scenie zbierający zajęte indeksy tej klasy, zamiast sprawdzania
		// kandydat-po-kandydacie (to dawałoby O(n^2) na sam spawn i O(n^3) na wczytanie sceny).
		HashSet<UInt32> usedIndices;
		auto collect = [&](const String& name) {
			if (name.Length() <= base.Length() || !name.StartsWith(base.CStr())) {
				return;
			}
			const String suffix = name.Substring(base.Length());
			for (UInt64 i = 0; i < suffix.Length(); ++i) {
				if (suffix[i] < '0' || suffix[i] > '9') {
					return; // np. "Cube_kopia" — nie zajmuje numerka
				}
			}
			bool parsed = false;
			const UInt32 index = suffix.ToInt<UInt32>(&parsed);
			if (parsed) {
				usedIndices.Insert(index);
			}
		};
		for (const auto& obj : mGameObjects) {
			collect(obj.second->GetObjectName());
		}
		for (const auto& obj : mPendingSpawns) {
			collect(obj->GetObjectName());
		}

		UInt32 index = 0;
		while (usedIndices.Contains(index)) {
			++index;
		}
		return base + String::FromInt(index);
	}

	TUsePointer<GameObject> SceneWorld::GetGameObjectByUUID(PluUUID uuid)
	{
		auto* found = mGameObjects.Find(uuid);
		if (found) {
			return *found;
		}
		if (mPendingSpawns.IsEmpty()) {
			return nullptr;
		}
		mPendingSpawns.FindIf([&](TOwningPointer<GameObject> gameObject) -> bool {
			return gameObject->GetObjectUUID() == uuid;
		});
		return nullptr;
	}

	TUsePointer<GameObject> SceneWorld::GetGameObjectOfClass(TClassPointer<GameObject> gameObjectClass)
	{
		for (const auto& gameObject : mGameObjects) {
			if (gameObject.second->GetClass()->IsDerivedOfOrSame(gameObjectClass)) {
				return gameObject.second;
			}
		}
		return nullptr;
	}

	DynamicArray<TUsePointer<GameObject>> SceneWorld::GetAllGameObjectsOfClass(
		TClassPointer<GameObject> gameObjectClass)
	{
		if (!mGameObjectsPerClassCache.Contains(gameObjectClass.GetRawType()->TypeName) || mNewGameObjectSpawned) {
			DynamicArray<TUsePointer<GameObject>> gameObjects;
			for (const auto& gameObject : mGameObjects) {
				if (gameObject.second->GetClass()->IsDerivedOfOrSame(gameObjectClass)) {
					gameObjects.PushBack(gameObject.second);
				}
			}
			if (mGameObjectsPerClassCache.Contains(gameObjectClass.GetRawType()->TypeName)) {
				mGameObjectsPerClassCache.Remove(gameObjectClass.GetRawType()->TypeName);
			}
			mGameObjectsPerClassCache.Insert(gameObjectClass.GetRawType()->TypeName, gameObjects);
			mNewGameObjectSpawned = false;
		}
		return mGameObjectsPerClassCache[gameObjectClass.GetRawType()->TypeName];
	}

	void SceneWorld::JoinPlayerLocally(UInt16 playerID)
	{
		PLU_CORE_ASSERT(mGameMode, "No game mode!");
		TUsePointer<Controller> controller = SpawnGameObject(mGameMode->ControllerClass ? mGameMode->ControllerClass : TClassPointer<Controller>(Controller::GetStaticClass()));
		TUsePointer<Puppet> puppet = SpawnGameObject(mGameMode->PuppetClass ? mGameMode->PuppetClass : TClassPointer<Puppet>(SpectatorPuppet::GetStaticClass()));
		mControllers.Insert(playerID, controller);
		controller->mPlayerID = playerID;

		DynamicArray<TUsePointer<GameObject>> playerStarts = GetAllGameObjectsOfClass(PlayerStart::GetStaticClass());
		if (!playerStarts.IsEmpty()) {
			const TUsePointer<GameObject>& playerStart = playerStarts.GetRandomItem();

			const Vec3 startLocation = playerStart->GetObjectLocation();
			const Vec3 startRotation = playerStart->GetObjectRotation();

			puppet->SetObjectLocation(startLocation + puppet->GetSpawnOffset());
			puppet->SetObjectRotation(startRotation);
		}

		controller->Possess(puppet);
	}

}
