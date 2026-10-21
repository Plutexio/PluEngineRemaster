#pragma once
#include "EngineObject.h"
#include "PluBase/Log.h"
#include "PluEngine/Core/Objects/EngineObjectManager.h"

namespace Plu
{
    template<class T, typename ... Args>
EngineObjectHandle EngineObjectManager::CreateObject(Args &&...args)
    {
        static_assert(std::is_base_of_v<EngineObject, T>, "T must derive from EngineObject");
        // Construct OUTSIDE the lock: a constructor may itself create engine objects
        // (re-entrant CreateObject), and the slot-map mutex is non-recursive. The object
        // becomes owned by this (creating) thread via ControlBlock::owningThread.
        TOwningPointer<EngineObject> created(static_cast<EngineObject*>(new T(std::forward<Args>(args)...)));

        std::unique_lock lock(mMutex);
        UInt32 idx;
        if (mFreeList.IsEmpty()) {
            idx = mObjects.Size();
            mObjects.PushBack(nullptr);
            mObjects[idx] = std::move(created);
            mGenerations.PushBack(0);
        } else {
            idx = mFreeList.Back();
            mObjects[idx] = std::move(created);
            mFreeList.PopBack();
        }
        mObjects[idx]->mHandle = EngineObjectHandle{idx, mGenerations[idx], false};
        mObjects[idx]->mEventDispatcher = CreateOwning<EventDispatcher>();
        const String typeName = mObjects[idx]->GetClass()->TypeName;
        if (mShortTermIDs.Contains(typeName))
        {
            mShortTermIDs[typeName]++;
        } else
        {
            mShortTermIDs[typeName] = 0;
        }
        mObjects[idx]->mShortTermID = mShortTermIDs[typeName];
        return EngineObjectHandle(idx, mGenerations[idx], false);
    }

    template<class T>
    Plu::TOwningPointer<T> EngineObjectManager::GetObjectAsOwnerUnlocked(const EngineObjectHandle handle)
    {
        // Caller already holds mMutex. The owning DynamicCast asserts (via
        // ControlBlock::owningThread) that the strong-ref is taken on the object's
        // creating thread — that is the real thread-affinity guard for owning pointers.
        if (!IsValidUnlocked(handle)) return nullptr;
        return Plu::DynamicCast<T>(mObjects.At(handle.Index));
    }

    template<class T>
    Plu::TOwningPointer<T> EngineObjectManager::GetObjectAsOwner(const EngineObjectHandle handle)
    {
        std::shared_lock lock(mMutex);
        return GetObjectAsOwnerUnlocked<T>(handle);
    }

    template<class T>
    Plu::TUsePointer<T> EngineObjectManager::GetObjectAsUser(const EngineObjectHandle handle)
    {
        // Use-only reader: callable from any thread. Wrap the slot in a TUsePointer first
        // so the cast bumps only weakCount — never takes a transient strong-ref that would
        // trip the owning-thread assert on the render thread.
        std::shared_lock lock(mMutex);
        if (!IsValidUnlocked(handle)) return nullptr;
        TUsePointer<EngineObject> use(mObjects.At(handle.Index));
        return Plu::DynamicCast<T>(use);
    }
}

