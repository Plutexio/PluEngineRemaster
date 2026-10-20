//
// Created by Plutex on 12/30/25.
//

#ifndef PLUENGINE_ENGINEOBJECTMANAGER_H
#define PLUENGINE_ENGINEOBJECTMANAGER_H

#include "PluSTL/PluSTL_FWD.h"

#include "EngineObjectHandle.h"
#include "PluEngine/Core.h"
#include "PluEngine/Core/Threading/ThreadAffinity.h"

#include <mutex>
#include <shared_mutex>

namespace Plu
{
    struct TypeInfo;
    class EngineObject;

    class PLUCORE_API EngineObjectManager
    {
        DynamicArray<TOwningPointer<EngineObject>> mObjects;
        DynamicArray<UInt32> mGenerations;
        DynamicArray<UInt32> mFreeList;
        HashMap<String, UInt32> mShortTermIDs;

        // Protects the slot-map (mObjects/mGenerations/mFreeList/mShortTermIDs) so it can
        // be mutated and read from any thread. Writers (CreateObject/DestroyObject) take
        // unique_lock; readers (GetObjectAs*, GetObjectOnIndex, GetObjectNames,
        // GetNumberOfObjects, IsValid) take shared_lock. Thread-affinity is enforced
        // per-object by ControlBlock::owningThread (an owning pointer may only be operated
        // on its creating thread), not by confining the manager to the main thread — GL
        // resources (FrameBuffer/Texture) are legitimately created and owned on the render
        // thread where the GL context is current.
        mutable std::shared_mutex mMutex;

        // Main-thread confinement for the few slow editor-introspection paths that still
        // assume it. Asserts in debug, compiles away in release. See ThreadAffinity.h.
        void CheckOwnerThread() const
        {
            PLU_CORE_ASSERT(IsOnMainThread(), "EngineObjectManager accessed off the main thread");
        }

        // Lock-free, thread-check-free validity probe. The caller must already hold the
        // appropriate lock (shared for readers, unique for writers). Avoids re-entering
        // mMutex from methods that already hold it.
        bool IsValidUnlocked(const EngineObjectHandle &handle) const;

        // Lock-free strong-ref getter. The caller must already hold mMutex (e.g. a writer
        // holding unique_lock that needs to hand back an owning pointer). Public
        // GetObjectAsOwner wraps this in a shared_lock.
        template<class T>
        Plu::TOwningPointer<T> GetObjectAsOwnerUnlocked(EngineObjectHandle handle);
    public:
        // Constructs an engine object from a Python class. Installed by the scripting layer at
        // startup (InstallPythonObjectFactory) — PluCore sits below it, so it holds the callback
        // rather than calling OwnerFromPython directly. Null in a build without scripting, where
        // TypeInfo::IsPythonType is never set either.
        static std::function<TOwningPointer<EngineObject>(pybind11::type)> PythonObjectFactory;

        EngineObjectManager();
        ~EngineObjectManager();

        DynamicArray<String> GetObjectNames(UInt32 numElements);
        UInt32 GetNumberOfObjects();
        TUsePointer<EngineObject> GetObjectOnIndex(UInt32 idx);

        //Refactor definition
        //SLOW!!!!!! DO NOT USE EVERY FRAME!!!! ITERATES THROUGH ALL OBJECTS AND CHECKS PARENTS!!! THOUSANDS OF OPERATIONS!!!
        DynamicArray<EngineObjectHandle> GetAllObjectsOfClass(TypeInfo* parent);

        template<class T, typename... Args>
        EngineObjectHandle CreateObject(Args&&... args);

        TUsePointer<EngineObject> CreateObject(const TypeInfo *Class);

        template<class T>
        Plu::TOwningPointer<T> GetObjectAsOwner(EngineObjectHandle handle);

        template<class T>
        Plu::TUsePointer<T> GetObjectAsUser(EngineObjectHandle handle);

        void DestroyObject(const EngineObjectHandle &handle);
        bool IsValid(const EngineObjectHandle &handle);
    };
}

#include "EngineObjectManager.inl"

#endif //PLUENGINE_ENGINEOBJECTMANAGER_H