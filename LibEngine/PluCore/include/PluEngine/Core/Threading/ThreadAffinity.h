//
// Created by Plutex.
//
// Thread-affinity helpers for the multithreading rollout.
// The engine confines mutation of core subsystems (EngineObjectManager,
// AssetManager registry, SceneWorld, Input, Python) to the main thread; the
// render thread reads snapshots only. These helpers identify the main thread so
// confinement can be asserted in debug builds (see PLU_CORE_ASSERT).
//

#ifndef PLUENGINE_THREADAFFINITY_H
#define PLUENGINE_THREADAFFINITY_H

#include "PluEngine/Core.h"
#include "PluSTL/PluSTL_FWD.h"

#include <thread>

namespace Plu
{
    // Records the calling thread as the engine's main thread. Call once, on the
    // main thread, during engine initialization. Also names it "Main".
    PLUCORE_API void RegisterMainThread();

    // The id captured by RegisterMainThread (default-constructed id if never called).
    PLUCORE_API std::thread::id GetMainThreadId();

    // True if the calling thread is the registered main thread. Returns true when
    // no main thread has been registered yet, so pre-init code and standalone
    // tooling never trip confinement asserts.
    PLUCORE_API bool IsOnMainThread();

    // Names the calling thread for diagnostics (profiler entries, logs). Call once
    // at the top of a thread's entry point. Names are thread-local, so every thread
    // that should be distinguishable in the Profiler panel needs its own call.
    PLUCORE_API void RegisterThreadName(const String& name);

    // Name of the calling thread. Falls back to "Main" for the registered main
    // thread and "Thread <id>" for anything unnamed, so it is never empty.
    PLUCORE_API const String& GetCurrentThreadName();
}

#endif //PLUENGINE_THREADAFFINITY_H
