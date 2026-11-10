//
// Created by Plutex on 11/7/26.
//

#include "PluEngine/ThreadPool/ThreadPool.h"

#include "PluBase/ThreadAffinity.h"


void Plu::ThreadPool::RunThread(int workerIndex)
{
    RegisterThreadName(String::Format("Worker {0}", workerIndex));
    mFreeThreads.PushBack(workerIndex);

    while (mIsRunning) {
        std::unique_lock<std::mutex> lock(mTasksMutexes[workerIndex]->first);
        mVariables[workerIndex]->wait(lock, [this, workerIndex]() -> bool {
            return mTasksMutexes[workerIndex]->second != nullptr;
        });
        mTasksMutexes[workerIndex]->second();
        mTasksMutexes[workerIndex]->second = nullptr;
        mFreeThreads.PushBack(workerIndex);
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

    mVariables.Resize(threadCount);
    for (int i =0; i < threadCount; i++) {
        mVariables[i] = new std::condition_variable;
    }
    mTasksMutexes.Resize(threadCount);
    for (int i =0; i < threadCount; i++) {
        mTasksMutexes[i] = new std::pair<std::mutex, TaskPtr>();
    }

    for (int i = 0; i < threadCount; i++) {
        mThreads.PushBack(new std::thread(&ThreadPool::RunThread, this, i));
    }
    PLU_CORE_INFO("Initialized Thread Pool!");
}

Plu::ThreadPool::~ThreadPool()
{
    mIsRunning = false;
    PLU_CORE_INFO("Waiting for threads to finish and destroying thread pool");
    for (std::thread *thread: mThreads) {
        if (thread->joinable()) {
            thread->join();
        }
        delete thread;
    }
    mThreads.Clear();

    for (auto variable : mVariables) {
        delete variable;
    }
    mVariables.Clear();
    PLU_CORE_INFO("Thread pool exited cleanly");
}

int Plu::ThreadPool::GetThreadCount() const
{
    return static_cast<int>(mThreads.Size());
}

void Plu::ThreadPool::Run(TaskPtr task, ETaskPrority priority)
{
    switch (priority) {
        case ETaskPrority::FRAME:
            mFrameTasks.PushBack(task);
            break;
        case ETaskPrority::BACKGROUND:
            mBackgroundTasks.PushBack(task);
            break;
    }

    int idx;
    bool success = mFreeThreads.TryPopFront(idx);
    if (!success) {
        return;
    }
    std::unique_lock<std::mutex> lock(mTasksMutexes[idx]->first);
    mTasksMutexes[idx]->second = task;
    lock.unlock();
    mVariables[idx]->notify_one();
}
