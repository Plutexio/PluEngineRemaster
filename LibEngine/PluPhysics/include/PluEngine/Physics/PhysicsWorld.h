//
// Created by Plutex on 9/5/26.
//

#ifndef PLUENGINE_PHYSICSWORLD_H
#define PLUENGINE_PHYSICSWORLD_H

#include "PluEngine/Core.h"
#include "JoltIntializer.h"
#include "PluEngine/Core/Objects/EngineObject.h"
#include "PhysicsWorld.generated.h"
#include "PluEngine/AssetTypes/StaticMesh/StaticMesh.h"

namespace JPH
{
    class PhysicsSystem;
    class TempAllocatorImplWithMallocFallback;
}

namespace Plu
{
    class JoltPointRenderer;
    class JoltWireframeRenderer;
    class PhysicsBody;
    class PhysicsBodyComponent;
    class PhysicsColliderComponent;
    class ObjectLayerPairFilterImpl;
    class ObjectVsBroadPhaseLayerFilterImpl;
    class BPLayerInterfaceImpl;

    PLU_ENUM(PyNamespace=Plu)
    enum class PhysicsDebugRenderMode
    {
        NONE,
        POINTS,
        WIREFRAME
    };


    PLU_CLASS()
    class PLUPHYSICS_API PhysicsWorld : public EngineObject
    {
        REFLECTION_BODY_PHYSICSWORLD()
    private:
        friend void JoltPhysics::Init(ApplicationInfo* applicationInfo);

        EngineObjectHandle mSceneWorldHandle;
        ApplicationInfo* mApplicationInfo;

        //My own associations
        HashSet<UInt64> mObjectsToCheck;

        HashMap<UInt64, TOwningPointer<PhysicsBody>> mBodyPerObject;
        // Objects whose body was created with deferAdd and still waits for FlushPendingBodies.
        HashSet<UInt64> mPendingBodyObjects;
        HashMap<UInt64, std::pair<Int32, Int32>> mRotLocChangesEventsPerObject;
        HashMap<UInt64, HashMap<UInt64, Int32>> mShapeChangesEventsPerObjectForComponents;

        bool mIsUpdatingObjectsFromPhysics = false;

#ifdef PLU_ENGINE_EDITOR_BUILD
        HashMap<UInt64, HashSet<UInt64>> mStaticMeshesUsageInObjects;
#endif

        //Jolt stuff
        TOwningPointer<JPH::TempAllocatorImplWithMallocFallback> mAllocator;
        TOwningPointer<JPH::PhysicsSystem>                     mPhysicsSystem;
        TOwningPointer<BPLayerInterfaceImpl>                   mBPLayerInterface;
        TOwningPointer<ObjectVsBroadPhaseLayerFilterImpl>      mObjVsBPFilter;
        TOwningPointer<ObjectLayerPairFilterImpl>              mObjVsObjFilter;

        TOwningPointer<JoltWireframeRenderer> mWireframeRenderer;
        TOwningPointer<JoltPointRenderer> mPointRenderer;

        // Jolt preallocates per-body bookkeeping for kMaxBodies up front (the broadphase node pool
        // grows lazily), so a high cap is cheap — and statics (a forest, scattered props) count
        // toward it just like dynamic bodies. Contact constraints are allocated from mAllocator
        // every step (~480 B each), which is what kTempAllocatorSize has to cover.
        static constexpr UInt32 kMaxBodies = 65536;
        static constexpr UInt32 kMaxBodyPairs = 65536;
        static constexpr UInt32 kMaxContactConstraints = 10240;
        static constexpr UInt32 kTempAllocatorSize = 16 * 1024 * 1024;

        // A flush adding at least this many bodies also rebuilds the broadphase from scratch.
        // Below it, Jolt's incremental rebuild during Update is enough.
        static constexpr UInt32 kOptimizeBroadPhaseMinBatch = 256;

        // Inserts every body created with deferAdd through Jolt's batch interface
        // (AddBodiesPrepare/AddBodiesFinalize), one batch per activation mode. Adding thousands
        // of bodies one AddBody at a time leaves the broadphase tree degenerate until it is rebuilt.
        void FlushPendingBodies();
    public:
        PhysicsWorld();
        virtual ~PhysicsWorld() override;

        void Init();
        void OnUpdate(float deltaTime, bool updateBodies);

        // deferAdd leaves the new body out of the physics system until FlushPendingBodies — bulk
        // paths (draining mObjectsToCheck after a scene load or PIE start) batch a whole scene.
        void RebuildObjectCollision(UInt64 uuid, bool deferAdd = false);

#ifdef PLU_ENGINE_EDITOR_BUILD
        void RebuildObjectsThatUseMesh(StaticMesh* staticMesh);
#endif

        [[nodiscard]] unsigned int GetNumOfBodies() const;

        PhysicsDebugRenderMode DebugRenderMode = PhysicsDebugRenderMode::NONE;
        Vec3 DebugLineColor = Vec3(1.0f, 0.0f, 0.0f);
        Vec3 DebugPointColor = Vec3(1.0f, 0.0f, 0.0f);
    };
}

#endif //PLUENGINE_PHYSICSWORLD_H
