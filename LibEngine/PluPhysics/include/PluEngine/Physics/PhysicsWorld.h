//
// Created by Plutex on 9/5/26.
//

#ifndef PLUENGINE_PHYSICSWORLD_H
#define PLUENGINE_PHYSICSWORLD_H

#include "PluEngine/Core.h"
#include "JoltIntializer.h"
#include "PluEngine/Core/Objects/EngineObject.h"
#include "PhysicsWorld.generated.h"
#include "Concurrent/ConcurrentHashMap.h"
#include "Concurrent/ConcurrentQueue.h"
#include "PluEngine/AssetTypes/StaticMesh/StaticMesh.h"

namespace JPH
{
    class PhysicsSystem;
    class TempAllocatorImplWithMallocFallback;
}

namespace Plu
{
    class GameObject;
    class PluContactListener;
    struct RaycastHitInfo;
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

    struct PhysicsOverlapEventInfo
    {
        bool End = false;

        // Jolt's contact identity: both body IDs and both sub-shape IDs (see MakeContactKey).
        std::pair<UInt64, UInt64> ContactKey = {0, 0};

        // Filled by the contact listener: object UUIDs (body user data) and collider indices
        // into PhysicsWorld::mCollidersPerObject (compound sub-shape user data).
        UInt64 ObjectA = 0;
        UInt64 ObjectB = 0;

        UInt64 ColliderA = 0;
        UInt64 ColliderB = 0;

        // Filled on the main thread when the begin is resolved. The end reads these back, so it
        // does not depend on collider indices a rebuild may have shuffled in the meantime.
        UInt64 ComponentA = 0;
        UInt64 ComponentB = 0;
    };

    // (object UUID, component UUID) of both sides, smaller side first.
    using PhysicsOverlapPairKey = std::pair<std::pair<UInt64, UInt64>, std::pair<UInt64, UInt64>>;


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
        HashMap<UInt32, UInt64> mBodyToObjectMap;
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

        TOwningPointer<PluContactListener> mContactListener;

        // Written by the contact listener on Jolt's worker threads, drained on the main thread
        // after each step. mCurrentOverlaps holds every live sensor contact by its ContactKey, so
        // OnContactRemoved can skip blocking contacts and the end can find what its begin resolved.
        ConcurrentQueue<PhysicsOverlapEventInfo> mOverlapQueue;
        ConcurrentHashMap<std::pair<UInt64,UInt64>, PhysicsOverlapEventInfo> mCurrentOverlaps;
        // Component UUID of every compound sub-shape, indexed by the sub-shape's user data.
        HashMap<UInt64, DynamicArray<UInt64>> mCollidersPerObject;
        // Main thread only. Live sensor contacts per (object, component) pair: Jolt reports one
        // contact per sub-shape pair (per triangle for a mesh collider), and a rebuilt body adds
        // its new contacts before the old body's are removed. Begin fires on 0 -> 1, end on 1 -> 0.
        HashMap<PhysicsOverlapPairKey, UInt32> mOverlapCounts;
        // Main thread only. Pairs whose begin actually reached gameplay. mOverlapCounts follows
        // Jolt, but a begin can still be dropped at dispatch (an earlier callback in the same batch
        // removed one of the components), and its end must then be dropped too.
        HashSet<PhysicsOverlapPairKey> mAnnouncedOverlaps;

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

        friend class PluContactListener;

        // Updates the overlap bookkeeping for one queued contact event. Returns true when it is a
        // begin/end gameplay has to hear about; overlapInfo is then complete (ends get their
        // begin's objects and components). Never calls into gameplay.
        bool ResolveCollisionOverlap(PhysicsOverlapEventInfo& overlapInfo);
        // Calls OnOverlapBegin/OnOverlapEnd on both objects. An end still reaches the side that is
        // alive when the other object or its component is already gone.
        void DispatchOverlapEvent(const PhysicsOverlapEventInfo& overlapInfo);
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

        // Closest hit along the segment, skipping the bodies of the objects in ignoredObjectUuids.
        RaycastHitInfo ShootRaycast(Vec3 Start, Vec3 End, const DynamicArray<UInt64>& ignoredObjectUuids = {});
        RaycastHitInfo ShootRaycast(Vec3 Start, Vec3 Direction, float Length, const DynamicArray<UInt64>& ignoredObjectUuids = {});

        [[nodiscard]] unsigned int GetNumOfBodies() const;

        PhysicsDebugRenderMode DebugRenderMode = PhysicsDebugRenderMode::NONE;
        Vec3 DebugLineColor = Vec3(1.0f, 0.0f, 0.0f);
        Vec3 DebugPointColor = Vec3(1.0f, 0.0f, 0.0f);
    };
}

#endif //PLUENGINE_PHYSICSWORLD_H
