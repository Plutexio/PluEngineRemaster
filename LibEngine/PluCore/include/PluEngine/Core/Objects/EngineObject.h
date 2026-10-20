//
// Created by Plutex on 12/30/25.
//

#ifndef PLUENGINE_ENGINEOBJECT_H
#define PLUENGINE_ENGINEOBJECT_H
#include "PluEngine/Core.h"
#include "EngineObject.generated.h"
#include "EngineObjectHandle.h"
#include "PluEngine/Core/Events/EventDispatcher.h"

namespace Plu
{
    class EngineObject;
    template<typename T>
    concept EngineObjectConc = std::is_same_v<EngineObject, T> || std::is_base_of_v<EngineObject, T>;

    PLU_CLASS(Abstract, PyExport, PyDerive)
    class PLUCORE_API EngineObject
    {
    private:
        friend class EngineObjectManager;
        EngineObjectHandle mHandle = {};
        UInt32 mShortTermID = 0;
        TOwningPointer<EventDispatcher> mEventDispatcher;
        TypeInfo* mPythonType = nullptr;
        template<typename B>
        requires EngineObjectConc<B>
        friend TOwningPointer<B> OwnerFromPython(pybind11::type type);
    protected:
        [[nodiscard]] TypeInfo* GetPythonType() const
        {return mPythonType;}
        void DispatchEvent(const String& name, void* payload) const {mEventDispatcher->Dispatch(name, payload);}
    public:
        TUsePointer<EventDispatcher> GetObjectEventDispatcher() {return mEventDispatcher;}
        EngineObjectHandle* GetEngineObjectHandle() {return &mHandle;}
        [[nodiscard]] EngineObjectHandle GetObjectHandle() const {return mHandle;}
        static TypeInfo* GetStaticClass();
        virtual TypeInfo* GetClass() = 0;
        virtual ~EngineObject();
        String GetDisplayName() //ClassName + Short-Term ID
        {
            TypeInfo* type = GetClass();
            return type->TypeName + String::FromInt(mShortTermID);
        }

        PLU_FUNCTION()
        Int32 SubscribeToEvent(String eventName, std::function<void(void*)> eventCallback)
        {
            return GetObjectEventDispatcher()->Subscribe(eventName, eventCallback);
        }

        PLU_FUNCTION()
        void UnsubscribeFromEvent(String eventName, Int32 eventId)
        {
            GetObjectEventDispatcher()->Unsubscribe(eventName, eventId);
        }
    };
}

#endif //PLUENGINE_ENGINEOBJECT_H