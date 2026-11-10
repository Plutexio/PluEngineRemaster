//
// Created by Plutex on 11/7/26.
//

#ifndef PLUENGINE_THREADPOOL_H
#define PLUENGINE_THREADPOOL_H

#include "PluEngine/Core.h"
#include "PluEngine/Core/Objects/EngineObject.h"
#include "ThreadPool.generated.h"
#include "PluSTL/Concurrent/ConcurrentQueue.h"
#include "PluSTL/Concurrent/ConcurrentArray.h"

namespace Plu
{
    enum class ETaskPrority
    {
        FRAME,
        BACKGROUND
    };

    PLU_CLASS()
    class PLUCORE_API ThreadPool final : public EngineObject
    {
        REFLECTION_BODY_THREADPOOL()
    public:
        using TaskPtr = void (*)(/*Here args, for future trust me*/);
    private:
        DynamicArray<std::thread*> mThreads;
        std::atomic<bool> mIsRunning;

        ConcurrentQueue<TaskPtr> mBackgroundTasks;
        ConcurrentQueue<TaskPtr> mFrameTasks;

        DynamicArray<std::condition_variable*> mVariables;
        DynamicArray<std::pair<std::mutex, TaskPtr>*> mTasksMutexes;
        ConcurrentQueue<int> mFreeThreads;

        void RunThread(int workerIndex);
    public:
        ThreadPool();
        ~ThreadPool() override;

        [[nodiscard]] int GetThreadCount() const;

        void Run(TaskPtr task, ETaskPrority priority = ETaskPrority::BACKGROUND);
    };
}

#endif //PLUENGINE_THREADPOOL_H
