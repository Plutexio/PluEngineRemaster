//
// Created by Plutex on 9/5/26.
//

#include "PluEngine/Physics/PhysicsWorld.h"

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/BodyFilter.h>
#include <Jolt/Physics/Body/BodyManager.h>
#include <Jolt/Physics/Collision/Shape/CompoundShape.h>
#include <Jolt/Physics/Collision/CollideShape.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/ContactListener.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Collision/Shape/StaticCompoundShape.h>

#include "Jolt/Physics/Collision/Shape/ScaledShape.h"
#include "PluEngine/PluUtils.h"
#include "PluEngine/Core/ApplicationInfo.h"
#include "PluEngine/Gameplay/Components/PhysicsBodyComponent.h"
#include "PluEngine/Gameplay/Components/PhysicsColliderComponent.h"
#include "PluEngine/Gameplay/Components/StaticMeshComponent.h"
#include "PluEngine/Gameplay/RaycastInfo.h"
#include "PluEngine/Gameplay/Scenes/SceneManager.h"
#include "PluEngine/Gameplay/Scenes/SceneWorld.h"
#include "PluEngine/Physics/JoltIntializer.h"
#include "PluEngine/Physics/PhysicsCollisionRules.h"
#include "PluEngine/Physics/PhysicsUtils.h"
#include "PluEngine/Physics/PhysicsBody.h"
#include "PluEngine/Physics/PhysicsPointRenderer.h"
#include "PluEngine/Physics/PhysicsWireframeRenderer.h"
#include "PluEngine/Physics/StaticMeshCollision.h"

void Plu::PhysicsWorld::RebuildObjectCollision(UInt64 uuid, bool deferAdd)
{
    PLU_PROFILE_SCOPE("CreatePhysicsBody");
    TUsePointer<SceneWorld> sceneWorld = mApplicationInfo->AppObjectManager->GetObjectAsUser<SceneWorld>(mSceneWorldHandle);
    TUsePointer<GameObject> gameObject = sceneWorld->GetGameObjectByUUID(uuid);

    if (!gameObject) {
        if (mBodyPerObject.Contains(uuid)) {
            mBodyPerObject.Remove(uuid);
        }
        return;
    }

    TUsePointer<PhysicsBodyComponent> bodyComponent = gameObject->GetComponentByClass(PhysicsBodyComponent::GetStaticClass());
    DynamicArray<TUsePointer<GameObjectComponent>> colliders = gameObject->GetAllComponentsByClass(PhysicsColliderComponent::GetStaticClass());
    DynamicArray<TUsePointer<GameObjectComponent>> staticMeshColliders = gameObject->GetAllComponentsByClass(StaticMeshComponent::GetStaticClass());

    if (!bodyComponent || (colliders.IsEmpty() && staticMeshColliders.IsEmpty())) return;

    JPH::StaticCompoundShapeSettings compoundShapeSettings;

    for (auto collider : colliders) {
        TUsePointer<PhysicsColliderComponent> colliderComponent = collider;
        JPH::ShapeRefC shape = colliderComponent->GetShape();
        if (shape == nullptr) {
            PLU_CORE_ERROR("Invalid shape for collider");
            continue;
        }

        Matrix4 worldMatrix = colliderComponent->GetMatrixRelativeToGameObject();
        Vec3 loc = GetLocationFromMatrix(worldMatrix);
        Vec3 rot = GetRotationFromMatrix(worldMatrix);
        Vec3 scale = colliderComponent->GetWorldScale();

        if (scale != Vec3(1.0f)) {
            static HashMap<void*, HashMap<Vec3, JPH::ShapeRefC>> shapeCache;
            if (shapeCache.Contains(const_cast<JPH::Shape *>(shape.GetPtr()))) {
                auto shapesByScale = shapeCache.Find(const_cast<JPH::Shape *>(shape.GetPtr()));
                if (!shapesByScale->Contains(scale)) {
                    //PLU_CORE_TRACE("Cache Miss: Shape Scale");
                    shapesByScale->Insert(scale, new JPH::ScaledShape(shape.GetPtr(), ToJPH(scale)));
                }
            } else {
                shapeCache[const_cast<JPH::Shape *>(shape.GetPtr())][scale] = new JPH::ScaledShape(shape.GetPtr(), ToJPH(scale));
                //PLU_CORE_TRACE("Cache Miss: Shape Ptr {}", static_cast<void*>(const_cast<JPH::Shape *>(shape.GetPtr())));
            }

            shape = shapeCache[const_cast<JPH::Shape *>(shape.GetPtr())][scale];
        }

        compoundShapeSettings.AddShape(ToJPH(loc), ToJPHRotation(rot), shape);

        if (!mShapeChangesEventsPerObjectForComponents[gameObject->GetObjectUUID()].Contains(collider->Uuid)) {
            mShapeChangesEventsPerObjectForComponents[gameObject->GetObjectUUID()][collider->Uuid] = collider->SubscribeToEvent("ShapeChanged", [this, gameObject](void*) {
                RebuildObjectCollision(gameObject->GetObjectUUID());
            });
            collider->SubscribeToEvent("RelativeLocationChanged", [this, gameObject](void*) {
                RebuildObjectCollision(gameObject->GetObjectUUID());
            });
            collider->SubscribeToEvent("RelativeRotationChanged", [this, gameObject](void*) {
                RebuildObjectCollision(gameObject->GetObjectUUID());
            });
            collider->SubscribeToEvent("RelativeScaleChanged", [this, gameObject](void*) {
                RebuildObjectCollision(gameObject->GetObjectUUID());
            });
        }
    }

#ifdef PLU_ENGINE_EDITOR_BUILD
    for (auto mesh : mStaticMeshesUsageInObjects) {
        if (mesh.second.Contains(uuid)) {
            mesh.second.Remove(uuid);
        }
    }
#endif

    for (auto staticMeshCollider : staticMeshColliders) {
        TUsePointer<StaticMeshComponent> staticMeshComponent = staticMeshCollider;
        TUsePointer<StaticMesh> staticMesh = staticMeshComponent->GetStaticMesh();

        if (!mShapeChangesEventsPerObjectForComponents[gameObject->GetObjectUUID()].Contains(staticMeshComponent->Uuid)) {
            mShapeChangesEventsPerObjectForComponents[gameObject->GetObjectUUID()][staticMeshComponent->Uuid] = staticMeshComponent->SubscribeToEvent("StaticMeshChanged", [this, gameObject](void*) {
                RebuildObjectCollision(gameObject->GetObjectUUID());
            });
            staticMeshComponent->SubscribeToEvent("RelativeLocationChanged", [this, gameObject](void*) {
                RebuildObjectCollision(gameObject->GetObjectUUID());
            });
            staticMeshComponent->SubscribeToEvent("RelativeRotationChanged", [this, gameObject](void*) {
                RebuildObjectCollision(gameObject->GetObjectUUID());
            });
            staticMeshComponent->SubscribeToEvent("RelativeScaleChanged", [this, gameObject](void*) {
                RebuildObjectCollision(gameObject->GetObjectUUID());
            });
        }

        if (!staticMesh) continue;

        Matrix4 worldMatrix = staticMeshComponent->GetMatrixRelativeToGameObject();
        Vec3 loc = GetLocationFromMatrix(worldMatrix);
        Vec3 rot = GetRotationFromMatrix(worldMatrix);
        Vec3 scale = staticMeshComponent->GetWorldScale();

        if (staticMesh->CollisionName != "") {
            if (!staticMesh->CollisionData || (staticMesh->CollisionName != staticMesh->CollisionData->GetClass()->TypeName)) {
                TypeInfo* newData = TypeRegistry::GetInstance()->GetTypeOfName(staticMesh->CollisionName);
                if (newData->IsDerivedOfOrSame(IStaticMeshCollisionData::GetStaticClass())) {
                    staticMesh->CollisionData = TOwningPointer(static_cast<IStaticMeshCollisionData*>(newData->Construct()));
                }
            }
            JPH::ShapeRefC shape = staticMesh->CollisionData->GetShape(staticMesh.GetRaw());

            if (scale != Vec3(1.0f)) {
                shape = new JPH::ScaledShape(shape.GetPtr(), ToJPH(scale));
            }
            compoundShapeSettings.AddShape(ToJPH(loc + staticMesh->CollisionData->GetOffset(staticMesh.GetRaw(), scale)), ToJPHRotation(rot), shape);
        }

#ifdef PLU_ENGINE_EDITOR_BUILD
        mStaticMeshesUsageInObjects[staticMesh->Uuid].Insert(staticMeshCollider->GetParentGameObject()->GetObjectUUID());
#endif
    }

    if (compoundShapeSettings.mSubShapes.empty()) {
        if (mBodyPerObject.Contains(gameObject->GetObjectUUID())) {
            mBodyPerObject.Remove(gameObject->GetObjectUUID());
        }
        return;
    }

    JPH::Shape::ShapeResult result = compoundShapeSettings.Create();
    if (result.HasError()) {
        PLU_CORE_ERROR("Failed to create shape for collider, error {}", result.GetError());
        return;
    }
    JPH::ShapeRefC finalShape = result.Get();

    if (mBodyPerObject.Contains(gameObject->GetObjectUUID())) {
        mBodyPerObject.Remove(gameObject->GetObjectUUID());
    }

    TOwningPointer<PhysicsBody> body = CreateOwning<PhysicsBody>(
        this->mPhysicsSystem->GetBodyInterface(),
        finalShape,
        ToJPH(gameObject->GetObjectLocation()),
        ToJPHRotation(gameObject->GetObjectRotation()),
        bodyComponent->Type,
        bodyComponent->Friction,
        bodyComponent->Restitution,
        bodyComponent->Mass,
        deferAdd
    );

    mBodyToObjectMap[body->GetID().GetIndexAndSequenceNumber()] = gameObject->GetObjectUUID();

    mBodyPerObject.Insert(gameObject->GetObjectUUID(), body);
    if (deferAdd) mPendingBodyObjects.Insert(gameObject->GetObjectUUID());

    if (!mRotLocChangesEventsPerObject.Contains(gameObject->GetObjectUUID())) {
        Int32 locEvent = gameObject->SubscribeToEvent("LocationChange", [this, gameObject](void*) {
            if (mBodyPerObject.Contains(gameObject->GetObjectUUID()) && !mIsUpdatingObjectsFromPhysics) {
                if (mBodyPerObject[gameObject->GetObjectUUID()]->GetBodyType() == PhysicsBodyType::Static) {
                    RebuildObjectCollision(gameObject->GetObjectUUID());
                } else {
                    mBodyPerObject[gameObject->GetObjectUUID()]->SetPosition(ToJPH(gameObject->GetObjectLocation()));
                }
            }
        });
        Int32 rotEvent = gameObject->SubscribeToEvent("RotationChange", [this, gameObject](void*) {
            if (mBodyPerObject.Contains(gameObject->GetObjectUUID()) && !mIsUpdatingObjectsFromPhysics) {
                if (mBodyPerObject[gameObject->GetObjectUUID()]->GetBodyType() == PhysicsBodyType::Static) {
                    RebuildObjectCollision(gameObject->GetObjectUUID());
                } else {
                    mBodyPerObject[gameObject->GetObjectUUID()]->SetRotation(ToJPHRotation(gameObject->GetObjectRotation()));
                }
            }
        });
        Int32 scaleEvent = gameObject->SubscribeToEvent("ScaleChange", [this, gameObject](void*) {
            if (mBodyPerObject.Contains(gameObject->GetObjectUUID()) && !mIsUpdatingObjectsFromPhysics) {
                RebuildObjectCollision(gameObject->GetObjectUUID());
            }
        });
        mRotLocChangesEventsPerObject.Insert(gameObject->GetObjectUUID(), {locEvent, rotEvent});
    }
}

#ifdef PLU_ENGINE_EDITOR_BUILD
void Plu::PhysicsWorld::RebuildObjectsThatUseMesh(StaticMesh *staticMesh)
{
    for (auto object : mStaticMeshesUsageInObjects[staticMesh->Uuid]) {
        RebuildObjectCollision(object);
    }
}
#endif

Plu::RaycastHitInfo Plu::PhysicsWorld::ShootRaycast(Vec3 Start, Vec3 End, const DynamicArray<UInt64>& ignoredObjectUuids)
{
    RaycastHitInfo result;
    JPH::RRayCast ray;
    ray.mOrigin = ToJPH(Start);
    // Jolt's mDirection is the whole ray (direction * length), not the end point.
    ray.mDirection = ToJPH(End - Start);

    JPH::RayCastResult rayResult;

    JPH::IgnoreMultipleBodiesFilter bodyFilter;
    bodyFilter.Reserve(static_cast<JPH::uint>(ignoredObjectUuids.Size()));
    for (UInt64 ignoredUuid : ignoredObjectUuids) {
        if (mBodyPerObject.Contains(ignoredUuid)) {
            bodyFilter.IgnoreBody(mBodyPerObject[ignoredUuid]->GetID());
        }
    }

    result.Hit = mPhysicsSystem->GetNarrowPhaseQuery().CastRay(ray, rayResult, {}, {}, bodyFilter);

    if (result.Hit) {
        result.HitLocation = ToGLM(ray.GetPointOnRay(rayResult.mFraction));
        const UInt32 bodyKey = rayResult.mBodyID.GetIndexAndSequenceNumber();
        if (mBodyToObjectMap.Contains(bodyKey)) {
            TUsePointer<SceneWorld> sceneWorld = mApplicationInfo->AppObjectManager->GetObjectAsUser<SceneWorld>(mSceneWorldHandle);
            result.HitObject = sceneWorld->GetGameObjectByUUID(mBodyToObjectMap[bodyKey]);
        }
    }

    return result;
}

Plu::RaycastHitInfo Plu::PhysicsWorld::ShootRaycast(Vec3 Start, Vec3 Direction, float Length, const DynamicArray<UInt64>& ignoredObjectUuids)
{
    return ShootRaycast(Start, Start + Direction * Length, ignoredObjectUuids);
}

Plu::PhysicsWorld::PhysicsWorld()
{
    // Falls back to malloc instead of aborting when a step outgrows the preallocated block.
    mAllocator = CreateOwning<JPH::TempAllocatorImplWithMallocFallback>(kTempAllocatorSize);
    mBPLayerInterface = CreateOwning<BPLayerInterfaceImpl>();
    mObjVsBPFilter = CreateOwning<ObjectVsBroadPhaseLayerFilterImpl>();
    mObjVsObjFilter = CreateOwning<ObjectLayerPairFilterImpl>();

    mPhysicsSystem = CreateOwning<JPH::PhysicsSystem>();
    mPhysicsSystem->Init(
        kMaxBodies, 0, kMaxBodyPairs, kMaxContactConstraints,
        *mBPLayerInterface,
        *mObjVsBPFilter,
        *mObjVsObjFilter
    );

    mPointRenderer = CreateOwning<JoltPointRenderer>();
    mWireframeRenderer = CreateOwning<JoltWireframeRenderer>();

    PLU_CORE_TRACE("Physics World Intialized");
}

Plu::PhysicsWorld::~PhysicsWorld()
{
}

void Plu::PhysicsWorld::Init()
{
    TUsePointer<SceneWorld> sceneWorld = mApplicationInfo->AppObjectManager->GetObjectAsUser<SceneWorld>(mSceneWorldHandle);
    sceneWorld->SubscribeToEvent("PhysicsTick", [this](void* data) {
        float deltaTime = *static_cast<float *>(data);
        this->OnUpdate(deltaTime, true);
    });

    sceneWorld->SubscribeToEvent("Raycast", [this](void* data) {
        RaycastRequest* request = static_cast<RaycastRequest*>(data);
        request->Result = ShootRaycast(request->Start, request->End, request->IgnoredObjectUuids);
    });

    sceneWorld->SubscribeToEvent("NewComponent", [this](void* data) {
        TUsePointer<GameObjectComponent> newComponent = *static_cast<TUsePointer<GameObjectComponent>*>(data);
        TUsePointer<GameObject> parentObject = newComponent->GetParentGameObject();
        if (newComponent->GetClass()->IsDerivedOfOrSame(PhysicsColliderComponent::GetStaticClass()) ||
        newComponent->GetClass()->IsDerivedOfOrSame(StaticMeshComponent::GetStaticClass())
        ) {
            mObjectsToCheck.Insert(parentObject->GetObjectUUID());
        }

        if (newComponent->GetClass() == PhysicsBodyComponent::GetStaticClass()) {
            mObjectsToCheck.Insert(parentObject->GetObjectUUID());

            newComponent->SubscribeToEvent("GetLinearVelocity", [newComponent, this](void* data) {
                TUsePointer<GameObject> bodyOwnerObject = newComponent->GetParentGameObject();
                if (!mBodyPerObject.Contains(bodyOwnerObject->GetObjectUUID())) return;
                Vec3 linearVelocity = ToGLM(mBodyPerObject[bodyOwnerObject->GetObjectUUID()]->GetLinearVelocity());
                *static_cast<Vec3*>(data) = linearVelocity;
            });
            newComponent->SubscribeToEvent("SetLinearVelocity", [newComponent, this](void* data) {
                TUsePointer<GameObject> bodyOwnerObject = newComponent->GetParentGameObject();
                if (!mBodyPerObject.Contains(bodyOwnerObject->GetObjectUUID())) return;
                mBodyPerObject[bodyOwnerObject->GetObjectUUID()]->SetLinearVelocity(ToJPH(*static_cast<Vec3*>(data)));
            });
            newComponent->SubscribeToEvent("AddLinearVelocity", [newComponent, this](void* data) {
                TUsePointer<GameObject> bodyOwnerObject = newComponent->GetParentGameObject();
                if (!mBodyPerObject.Contains(bodyOwnerObject->GetObjectUUID())) return;
                mBodyPerObject[bodyOwnerObject->GetObjectUUID()]->AddLinearVelocity(ToJPH(*static_cast<Vec3*>(data)));
            });

            newComponent->SubscribeToEvent("GetAngularVelocity", [newComponent, this](void* data) {
                TUsePointer<GameObject> bodyOwnerObject = newComponent->GetParentGameObject();
                if (!mBodyPerObject.Contains(bodyOwnerObject->GetObjectUUID())) return;
                Vec3 angularVelocity = ToGLM(mBodyPerObject[bodyOwnerObject->GetObjectUUID()]->GetAngularVelocity());
                *static_cast<Vec3*>(data) = angularVelocity;
            });
            newComponent->SubscribeToEvent("SetAngularVelocity", [newComponent, this](void* data) {
                TUsePointer<GameObject> bodyOwnerObject = newComponent->GetParentGameObject();
                if (!mBodyPerObject.Contains(bodyOwnerObject->GetObjectUUID())) return;
                mBodyPerObject[bodyOwnerObject->GetObjectUUID()]->SetAngularVelocity(ToJPH(*static_cast<Vec3*>(data)));
            });

            newComponent->SubscribeToEvent("GetFriction", [newComponent, this](void* data) {
                TUsePointer<GameObject> bodyOwnerObject = newComponent->GetParentGameObject();
                if (!mBodyPerObject.Contains(bodyOwnerObject->GetObjectUUID())) return;
                float friction = mBodyPerObject[bodyOwnerObject->GetObjectUUID()]->GetFriction();
                *static_cast<float*>(data) = friction;
            });
            newComponent->SubscribeToEvent("SetFriction", [newComponent, this](void* data) {
                TUsePointer<GameObject> bodyOwnerObject = newComponent->GetParentGameObject();
                if (!mBodyPerObject.Contains(bodyOwnerObject->GetObjectUUID())) return;
                mBodyPerObject[bodyOwnerObject->GetObjectUUID()]->SetFriction(*static_cast<float*>(data));
            });

            newComponent->SubscribeToEvent("GetRestitution", [newComponent, this](void* data) {
                TUsePointer<GameObject> bodyOwnerObject = newComponent->GetParentGameObject();
                if (!mBodyPerObject.Contains(bodyOwnerObject->GetObjectUUID())) return;
                float restitution = mBodyPerObject[bodyOwnerObject->GetObjectUUID()]->GetRestitution();
                *static_cast<float*>(data) = restitution;
            });
            newComponent->SubscribeToEvent("SetRestitution", [newComponent, this](void* data) {
                TUsePointer<GameObject> bodyOwnerObject = newComponent->GetParentGameObject();
                if (!mBodyPerObject.Contains(bodyOwnerObject->GetObjectUUID())) return;
                mBodyPerObject[bodyOwnerObject->GetObjectUUID()]->SetRestitution(*static_cast<float *>(data));
            });

            newComponent->SubscribeToEvent("AddForce", [newComponent, this](void* data) {
                TUsePointer<GameObject> bodyOwnerObject = newComponent->GetParentGameObject();
                if (!mBodyPerObject.Contains(bodyOwnerObject->GetObjectUUID())) return;
                mBodyPerObject[bodyOwnerObject->GetObjectUUID()]->AddForce(ToJPH(*static_cast<Vec3*>(data)));
            });
            newComponent->SubscribeToEvent("AddTorque", [newComponent, this](void* data) {
                TUsePointer<GameObject> bodyOwnerObject = newComponent->GetParentGameObject();
                if (!mBodyPerObject.Contains(bodyOwnerObject->GetObjectUUID())) return;
                mBodyPerObject[bodyOwnerObject->GetObjectUUID()]->AddTorque(ToJPH(*static_cast<Vec3*>(data)));
            });
            newComponent->SubscribeToEvent("AddImpulse", [newComponent, this](void* data) {
                TUsePointer<GameObject> bodyOwnerObject = newComponent->GetParentGameObject();
                if (!mBodyPerObject.Contains(bodyOwnerObject->GetObjectUUID())) return;
                mBodyPerObject[bodyOwnerObject->GetObjectUUID()]->AddImpulse(ToJPH(*static_cast<Vec3*>(data)));
            });
            newComponent->SubscribeToEvent("AddAngularImpulse", [newComponent, this](void* data) {
                TUsePointer<GameObject> bodyOwnerObject = newComponent->GetParentGameObject();
                if (!mBodyPerObject.Contains(bodyOwnerObject->GetObjectUUID())) return;
                mBodyPerObject[bodyOwnerObject->GetObjectUUID()]->AddAngularImpulse(ToJPH(*static_cast<Vec3*>(data)));
            });
        }

    });

    sceneWorld->SubscribeToEvent("DestroyComponent", [this](void* data) {
        TUsePointer<GameObjectComponent> oldComponent = *static_cast<TUsePointer<GameObjectComponent>*>(data);
        TUsePointer<GameObject> parentObject = oldComponent->GetParentGameObject();
        if (oldComponent->GetClass()->IsDerivedOfOrSame(PhysicsColliderComponent::GetStaticClass()) ||
        oldComponent->GetClass()->IsDerivedOfOrSame(StaticMeshComponent::GetStaticClass())
        )
        {

            mShapeChangesEventsPerObjectForComponents[parentObject->GetObjectUUID()].Remove(oldComponent->Uuid);

            mObjectsToCheck.Insert(parentObject->GetObjectUUID());
        }

        if (oldComponent->GetClass() == PhysicsBodyComponent::GetStaticClass()) {
            mObjectsToCheck.Insert(parentObject->GetObjectUUID());
        }

    });
}

void Plu::PhysicsWorld::OnUpdate(float deltaTime, bool updateBodies)
{
    if (!mObjectsToCheck.IsEmpty()) {
        PLU_PROFILE_SCOPE("Physics RebuildPendingObjects");
        for (auto uuid : mObjectsToCheck) {
            RebuildObjectCollision(uuid, true);
        }
        mObjectsToCheck.Clear();
        FlushPendingBodies();
    }
    PLU_PROFILE_SCOPE("Physics Tick");
    if (updateBodies) mPhysicsSystem->Update(deltaTime, 1, mAllocator.GetRaw(), JoltPhysics::GetJoltThreadPool().GetRaw());

    mIsUpdatingObjectsFromPhysics = true;
    TUsePointer<SceneWorld> sceneWorld = mApplicationInfo->AppObjectManager->GetObjectAsUser<SceneWorld>(mSceneWorldHandle);
    DynamicArray<UInt64> toDestroy;
    // Writing a transform back is never free: SetObjectLocation/SetObjectRotation mark the object
    // and its whole component subtree for world-matrix regeneration, which is what the render
    // snapshot builder reads per frame. Doing it for bodies that cannot have moved made every
    // static prop in the scene recompute its world/normal matrix every frame — 10 ms of
    // RenderSnapshotBuilder::BatchStaticMeshes on a 2000-component scene. Two guards below:
    // nothing was simulated at all, or this particular body is not simulating.
    JPH::BodyInterface& bodyInterface = mPhysicsSystem->GetBodyInterface();
    for (const auto& body : mBodyPerObject) {
        TUsePointer<PhysicsBody> actualBody = body.second;
        TUsePointer<GameObject> gameObject = sceneWorld->GetGameObjectByUUID(body.first);

        if (!gameObject) {
            toDestroy.PushBack(body.first);
            continue;
        }

        // The editor outside PIE steps this world only to drain pending collision rebuilds and
        // refresh debug geometry (SceneViewportPanel passes updateBodies = false) — no simulation
        // ran, so no body moved.
        if (!updateBodies) continue;

        // Static bodies are never active, and a body that fell asleep is not moving any more —
        // both keep the GameObject transform the simulation last left them at. The extra
        // WasActiveOnLastSync pass catches the frame a body deactivates in: Jolt takes it off the
        // active list at the end of the step that brought it to rest, so without it the last few
        // millimetres of that step would never reach the object.
        const bool bodyIsActive = bodyInterface.IsActive(actualBody->GetID());
        const bool needsSync = bodyIsActive || actualBody->WasActiveOnLastSync;
        actualBody->WasActiveOnLastSync = bodyIsActive;
        if (!needsSync) continue;

        gameObject->SetObjectLocation(ToGLM(actualBody->GetPosition()));

        JPH::Quat jphRot = actualBody->GetRotation();
        glm::quat glmRot(jphRot.GetW(), jphRot.GetX(), jphRot.GetY(), jphRot.GetZ());
        Vec3 eulerDeg = glm::degrees(glm::eulerAngles(glmRot));
        gameObject->SetObjectRotation(eulerDeg);
    }
    mIsUpdatingObjectsFromPhysics = false;
    for (const auto& destroy : toDestroy) {
        RebuildObjectCollision(destroy);
    }

    if (DebugRenderMode == PhysicsDebugRenderMode::NONE) return;

    mPointRenderer->BeginFrame();
    mWireframeRenderer->BeginFrame();

    JPH::BodyIDVector bodies;
    mPhysicsSystem->GetBodies(bodies);
    for (JPH::BodyID body : bodies)
    {
        JPH::BodyLockRead lock(mPhysicsSystem->GetBodyLockInterface(), body);
        if (!lock.Succeeded()) continue;
        if (DebugRenderMode == PhysicsDebugRenderMode::WIREFRAME) mWireframeRenderer->AddBody(lock.GetBody(), DebugLineColor);
        if (DebugRenderMode == PhysicsDebugRenderMode::POINTS) mPointRenderer->AddBody(lock.GetBody(), DebugPointColor);
    }

    mWireframeRenderer->PackInto(sceneWorld->GetRawDebugLineArray());
    mPointRenderer->PackInto(sceneWorld->GetRawDebugPointArray());
}

void Plu::PhysicsWorld::FlushPendingBodies()
{
    if (mPendingBodyObjects.IsEmpty()) return;
    PLU_PROFILE_SCOPE("Physics FlushPendingBodies");

    JPH::BodyInterface& bodyInterface = mPhysicsSystem->GetBodyInterface();
    DynamicArray<JPH::BodyID> activeBodies;
    DynamicArray<JPH::BodyID> inactiveBodies;

    // Looked up by object rather than stored as IDs: a deferred body may have been replaced or
    // destroyed since it was created, and handing a destroyed ID to AddBodiesPrepare is fatal.
    for (UInt64 uuid : mPendingBodyObjects) {
        if (!mBodyPerObject.Contains(uuid)) continue;
        TUsePointer<PhysicsBody> body = mBodyPerObject[uuid];
        if (!body->IsValid() || bodyInterface.IsAdded(body->GetID())) continue;

        (body->NeedsActivation() ? activeBodies : inactiveBodies).PushBack(body->GetID());
    }
    mPendingBodyObjects.Clear();

    auto addBatch = [&bodyInterface](DynamicArray<JPH::BodyID>& bodyIds, JPH::EActivation activation) {
        if (bodyIds.IsEmpty()) return;
        const int count = static_cast<int>(bodyIds.Size());
        JPH::BodyInterface::AddState addState = bodyInterface.AddBodiesPrepare(bodyIds.Data(), count);
        bodyInterface.AddBodiesFinalize(bodyIds.Data(), count, addState, activation);
    };
    addBatch(activeBodies, JPH::EActivation::Activate);
    addBatch(inactiveBodies, JPH::EActivation::DontActivate);

    if (activeBodies.Size() + inactiveBodies.Size() >= kOptimizeBroadPhaseMinBatch) {
        PLU_PROFILE_SCOPE("Physics OptimizeBroadPhase");
        mPhysicsSystem->OptimizeBroadPhase();
    }
}

unsigned int Plu::PhysicsWorld::GetNumOfBodies() const
{
    return mPhysicsSystem->GetNumBodies();
}
