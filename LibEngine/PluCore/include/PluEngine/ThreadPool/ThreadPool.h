//
// Created by Plutex on 11/7/26.
//

#ifndef PLUENGINE_THREADPOOL_H
#define PLUENGINE_THREADPOOL_H

#include "PluEngine/Core.h"
#include "PluEngine/Core/Objects/EngineObject.h"
#include "ThreadPool.generated.h"

namespace Plu
{
    PLU_CLASS()
    class PLUCORE_API ThreadPool : public EngineObject
    {
        REFLECTION_BODY_THREADPOOL()
    private:
        DynamicArray<std::thread*> mThreads;
        std::atomic<bool> mIsRunning;

        void RunThread(int workerIndex);
    public:
        ThreadPool();
        ~ThreadPool();

        int GetThreadCount() const;
    };
}

#endif //PLUENGINE_THREADPOOL_H
