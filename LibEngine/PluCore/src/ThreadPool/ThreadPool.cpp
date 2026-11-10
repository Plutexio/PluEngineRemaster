//
// Created by Plutex on 11/7/26.
//

#include "PluEngine/ThreadPool/ThreadPool.h"

#include "PluBase/ThreadAffinity.h"
#include "PluEngine/Timer.h"

#include <atomic>
#include <exception>
#include <memory>

namespace
{
    // The pool whose worker the current thread is; null on every other thread.
    thread_local const Plu::ThreadPool* tWorkerOfPool = nullptr;
}

void Plu::ThreadPool::RunThread(int workerIndex)
{
    RegisterThreadName(String::Format("Worker {0}", workerIndex));
    tWorkerOfPool = this;

    while (true) {
        TaskPtr task;
        {
            std::unique_lock<std::mutex> lock(mTaskMutex);
            mWakeUp.wait(lock, [this]() {
                return !mIsRunning || !mFrameTasks.IsEmpty() || !mBackgroundTasks.IsEmpty();
            });

            if (!mFrameTasks.TryPopFront(task) && !mBackgroundTasks.TryPopFront(task)) {
                return;
            }
        }
        task();
    }
}

Plu::ThreadPool::ThreadPool()
{
    int hardwareThreadCount = static_cast<int>(std::thread::hardware_concurrency());
    int threadCount = 0;
    if (hardwareThreadCount < 4) {
        threadCount = 2;
    } else {
        threadCount = hardwareThreadCount - 2;
    }
    PLU_CORE_INFO("Initializing thread pool with {} threads", threadCount);
    mThreads.Reserve(threadCount);
    mIsRunning = true;

    mBackgroundTasks.Reserve(1000);
    mFrameTasks.Reserve(5000);

    for (int i = 0; i < threadCount; i++) {
        mThreads.PushBack(new std::thread(&ThreadPool::RunThread, this, i));
    }
    PLU_CORE_INFO("Initialized Thread Pool!");
}

Plu::ThreadPool::~ThreadPool()
{
    PLU_CORE_INFO("Waiting for threads to finish and destroying thread pool");

    {
        std::lock_guard<std::mutex> lock(mTaskMutex);
        mIsRunning = false;
    }
    mWakeUp.notify_all();

    for (std::thread *thread: mThreads) {
        if (thread->joinable()) {
            thread->join();
        }
        delete thread;
    }
    mThreads.Clear();
    PLU_CORE_INFO("Thread pool exited cleanly");
}

int Plu::ThreadPool::GetThreadCount() const
{
    return static_cast<int>(mThreads.Size());
}

bool Plu::ThreadPool::IsCurrentThreadWorker() const
{
    return tWorkerOfPool == this;
}

namespace
{
    // Shared by the calling thread and the helper tasks of one ParallelFor. Helpers hold it by
    // refcount because one may only get a worker after the call has returned; it then claims an
    // index past the end and leaves without touching callback/context.
    struct ParallelForState
    {
        size_t Count = 0;
        void (*Callback)(void*, size_t) = nullptr;
        void* Context = nullptr;

        std::atomic<size_t> NextIndex{0};
        // Every index in [0, Count) is claimed and completed exactly once, also after a failure
        // (it is skipped then), so the caller only ever waits on this reaching Count.
        std::atomic<size_t> Completed{0};
        std::atomic<bool> Failed{false};
        std::mutex ErrorMutex;
        std::exception_ptr FirstError;

        void Drain()
        {
            for (size_t index = NextIndex.fetch_add(1, std::memory_order_relaxed); index < Count;
                 index = NextIndex.fetch_add(1, std::memory_order_relaxed)) {
                if (!Failed.load(std::memory_order_relaxed)) {
                    try {
                        Callback(Context, index);
                    } catch (...) {
                        std::lock_guard<std::mutex> lock(ErrorMutex);
                        if (!FirstError) FirstError = std::current_exception();
                        Failed.store(true, std::memory_order_relaxed);
                    }
                }
                // acq_rel: publishes this item's writes to the caller, which acquires below.
                if (Completed.fetch_add(1, std::memory_order_acq_rel) + 1 == Count) {
                    Completed.notify_all();
                }
            }
        }
    };
}

void Plu::ThreadPool::ParallelForImpl(size_t count, ParallelForCallback callback, void* context, ETaskPrority priority)
{
    PLU_PROFILE_SCOPE("ThreadPool::ParallelFor");
    if (count == 0) return;

    auto state = std::make_shared<ParallelForState>();
    state->Count = count;
    state->Callback = callback;
    state->Context = context;

    // A worker blocked waiting on its own pool is one worker fewer to do the work it waits for,
    // so nested calls stay serial. The caller takes items as well, so count - 1 helpers suffice.
    const size_t helperCount = IsCurrentThreadWorker() ? 0 : std::min(mThreads.Size(), count - 1);
    for (size_t i = 0; i < helperCount; ++i) {
        Run([state]() { state->Drain(); }, priority);
    }

    // The caller drains too, so the call finishes even if every worker is busy with other tasks.
    state->Drain();

    size_t completed = state->Completed.load(std::memory_order_acquire);
    while (completed != count) {
        state->Completed.wait(completed, std::memory_order_acquire);
        completed = state->Completed.load(std::memory_order_acquire);
    }

    if (state->FirstError) std::rethrow_exception(state->FirstError);
}

void Plu::ThreadPool::Run(TaskPtr task, ETaskPrority priority)
{
    {
        std::lock_guard<std::mutex> lock(mTaskMutex);
        (priority == ETaskPrority::FRAME ? mFrameTasks : mBackgroundTasks).PushBack(std::move(task));
    }
    mWakeUp.notify_all();
}
