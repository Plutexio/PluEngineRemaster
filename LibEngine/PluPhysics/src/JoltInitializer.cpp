//
// Created by Plutex on 2026-03-07.
//

#include "PluEngine/Physics/JoltIntializer.h"

#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Physics/PhysicsSettings.h>

#include "PluEngine/Core/ApplicationInfo.h"
#include "PluEngine/Core/Objects/EngineObjectManager.h"
#include "PluEngine/Gameplay/Scenes/SceneManager.h"
#include "PluEngine/Gameplay/Scenes/SceneWorld.h"
#include "PluEngine/Physics/PhysicsWorld.h"

static Plu::TOwningPointer<JPH::JobSystemThreadPool> gJobSystemThreadPool;
static Plu::ApplicationInfo* gApplicationInfo;
static Plu::HashMap<Plu::EngineObjectHandle, Plu::TOwningPointer<Plu::PhysicsWorld>> gPhysicsWorlds;

void Plu::JoltPhysics::Init(ApplicationInfo* applicationInfo) {
	JPH::RegisterDefaultAllocator();
	JPH::Factory::sInstance = new JPH::Factory();
	JPH::RegisterTypes();

	gApplicationInfo = applicationInfo;

	int hardwareThreadCount = static_cast<int>(std::thread::hardware_concurrency());
	int joltThreadCount = 0;

	if (hardwareThreadCount <= 3) {
		joltThreadCount = 2;
	} else {
		joltThreadCount = hardwareThreadCount - 2;
	}

	gJobSystemThreadPool = CreateOwning<JPH::JobSystemThreadPool>(
		JPH::cMaxPhysicsJobs,
		JPH::cMaxPhysicsBarriers,
		joltThreadCount
	);
	PLU_CORE_INFO("Jolt Physics Initialized, ThreadPool Size {}", joltThreadCount);

	//Events
	gApplicationInfo->AppScenesManager->SubscribeToEvent("NewWorldBeforeLoad", [](void* data) {
		EngineObjectHandle* worldHandle = static_cast<EngineObjectHandle*>(data);
		TUsePointer<EngineObject> object = gApplicationInfo->AppObjectManager->GetObjectAsUser<EngineObject>(*worldHandle);
		if (object->GetClass() != SceneWorld::GetStaticClass()) return;
		TUsePointer<SceneWorld> world = DynamicCast<SceneWorld>(object);
		if (!world) return;
		EngineObjectHandle physicsWorldHandle = gApplicationInfo->AppObjectManager->CreateObject<PhysicsWorld>();
		TOwningPointer<PhysicsWorld> physicsWorld = gApplicationInfo->AppObjectManager->GetObjectAsOwner<PhysicsWorld>(physicsWorldHandle);
		physicsWorld->mSceneWorldHandle = *worldHandle;
		physicsWorld->mApplicationInfo = gApplicationInfo;
		physicsWorld->Init();
		gPhysicsWorlds.Insert(*worldHandle, physicsWorld);
		PLU_CORE_INFO("New Physics World created for handle {}", worldHandle->ToString().CStr());
	});

	gApplicationInfo->AppScenesManager->SubscribeToEvent("UnloadWorld", [](void* data) {
		EngineObjectHandle* worldHandle = static_cast<EngineObjectHandle*>(data);
		if (gPhysicsWorlds.Contains(*worldHandle)) {
			TOwningPointer<PhysicsWorld> physicsWorld = *gPhysicsWorlds.Find(*worldHandle);
			gApplicationInfo->AppObjectManager->DestroyObject(physicsWorld->GetObjectHandle());
			gPhysicsWorlds.Remove(*worldHandle);
			PLU_CORE_INFO("Old Physics World annihilated");
		}
	});
}

void Plu::JoltPhysics::Shutdown() {
	JPH::UnregisterTypes();
	delete JPH::Factory::sInstance;
	JPH::Factory::sInstance = nullptr;
}

Plu::TUsePointer<Plu::PhysicsWorld> Plu::JoltPhysics::GetPhysicsWorldBySceneHandle(EngineObjectHandle sceneHandle)
{
	if (gPhysicsWorlds.Contains(sceneHandle)) {
		return *gPhysicsWorlds.Find(sceneHandle);
	}
	return nullptr;
}

Plu::TOwningPointer<JPH::JobSystem> Plu::JoltPhysics::GetJoltThreadPool()
{
	return gJobSystemThreadPool;
}
