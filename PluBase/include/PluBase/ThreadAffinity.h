//
// Created by Plutex.
//
// Thread-affinity helpers. An application that confines some state to its main
// thread registers that thread once at startup, and code touching the state can
// then assert it is on it: PLU_CORE_ASSERT(IsOnMainThread(), ...). Threads can
// also be given names for diagnostics (log lines, profiler entries).
//

#ifndef PLUBASE_THREADAFFINITY_H
#define PLUBASE_THREADAFFINITY_H

#include "PluBase/Base.h"
#include "PluSTL/String/String.h"

#include <thread>

namespace Plu
{
    // Records the calling thread as the application's main thread. Call once, on the
    // main thread, during initialization. Also names it "Main".
    PLUBASE_API void RegisterMainThread();

    // The id captured by RegisterMainThread (default-constructed id if never called).
    PLUBASE_API std::thread::id GetMainThreadId();

    // True if the calling thread is the registered main thread. Returns true when
    // no main thread has been registered yet, so pre-init code and standalone
    // tooling never trip confinement asserts.
    PLUBASE_API bool IsOnMainThread();

    // Names the calling thread for diagnostics (profiler entries, logs). Call once
    // at the top of a thread's entry point. Names are thread-local, so every thread
    // that should be distinguishable in the Profiler panel needs its own call.
    PLUBASE_API void RegisterThreadName(const String& name);

    // Name of the calling thread. Falls back to "Main" for the registered main
    // thread and "Thread <id>" for anything unnamed, so it is never empty.
    PLUBASE_API const String& GetCurrentThreadName();
}

#endif //PLUBASE_THREADAFFINITY_H
