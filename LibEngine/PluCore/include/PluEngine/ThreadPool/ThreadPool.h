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
        using TaskPtr = MoveOnlyFunction<void()>;
    private:
        DynamicArray<std::thread*> mThreads;
        std::atomic<bool> mIsRunning;

        Queue<TaskPtr> mBackgroundTasks;
        Queue<TaskPtr> mFrameTasks;

        std::condition_variable mWakeUp;
        std::mutex mTaskMutex;

        void RunThread(int workerIndex);

        using ParallelForCallback = void (*)(void* context, size_t index);
        void ParallelForImpl(size_t count, ParallelForCallback callback, void* context, ETaskPrority priority);
    public:
        ThreadPool();
        ~ThreadPool() override;

        [[nodiscard]] int GetThreadCount() const;
        // True on one of this pool's worker threads.
        [[nodiscard]] bool IsCurrentThreadWorker() const;

        void Run(TaskPtr task, ETaskPrority priority = ETaskPrority::BACKGROUND);

        // Calls body(index) once for every index in [0, count) on the pool's workers and the
        // calling thread, and returns when every call has finished. Indices are handed out one
        // at a time, so items of uneven cost balance themselves.
        // body runs concurrently: it may only write state owned by its index (or synchronize
        // itself). Workers have no GL context and must not create TOwningPointers (the worker
        // would own them) or dispatch events — collect results and act on them after the call.
        // If body throws, the remaining indices are skipped and the first exception is rethrown
        // here. Called from one of this pool's workers it runs serially on that worker.
        template<typename Body>
        void ParallelFor(size_t count, Body&& body, ETaskPrority priority = ETaskPrority::FRAME)
        {
            using BodyType = std::remove_reference_t<Body>;
            ParallelForImpl(count, [](void* context, size_t index) {
                (*static_cast<BodyType*>(context))(index);
            }, const_cast<void*>(static_cast<const void*>(std::addressof(body))), priority);
        }
    };
}

#endif //PLUENGINE_THREADPOOL_H
