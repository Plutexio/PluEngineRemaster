//
// Created by Plutex on 11/7/26.
//

#include "PluEngine/ThreadPool/ThreadPool.h"

#include "PluBase/ThreadAffinity.h"


void Plu::ThreadPool::RunThread(int workerIndex)
{
    RegisterThreadName(String::Format("Worker {0}", workerIndex));

    while (mIsRunning) {
        //TODO
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
    for (int i = 0; i < threadCount; i++) {
        mThreads.PushBack(new std::thread(&ThreadPool::RunThread, this, i));
    }
    PLU_CORE_INFO("Initialized Thread Pool!");

    mBackgroundTasks.Reserve(1000);
    mFrameTasks.Reserve(5000);
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


}
