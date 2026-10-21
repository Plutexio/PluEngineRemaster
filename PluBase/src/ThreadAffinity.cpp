//
// Created by Plutex.
//

#include "PluBase/ThreadAffinity.h"

#include <sstream>

namespace Plu
{
    namespace
    {
        std::thread::id gMainThreadId{};

        // Per-thread diagnostic name. Empty until RegisterThreadName or the first
        // GetCurrentThreadName call fills in a fallback.
        thread_local String gThreadName{};
    }

    void RegisterMainThread()
    {
        gMainThreadId = std::this_thread::get_id();
        RegisterThreadName("Main");
    }

    std::thread::id GetMainThreadId()
    {
        return gMainThreadId;
    }

    bool IsOnMainThread()
    {
        // Not registered yet -> don't trip confinement asserts (pre-init / tooling).
        return gMainThreadId == std::thread::id{} || std::this_thread::get_id() == gMainThreadId;
    }

    void RegisterThreadName(const String& name)
    {
        gThreadName = name;
    }

    const String& GetCurrentThreadName()
    {
        if (gThreadName.IsEmpty()) {
            // Unnamed thread: "Main" if it is the registered main thread (RegisterMainThread
            // names it, but a thread-local can still be fresh in another DLL), otherwise the
            // OS id so at least the entries stay separable in the Profiler panel.
            if (gMainThreadId != std::thread::id{} && std::this_thread::get_id() == gMainThreadId) {
                gThreadName = "Main";
            } else {
                std::ostringstream stream;
                stream << std::this_thread::get_id();
                gThreadName = String("Thread ") + stream.str().c_str();
            }
        }
        return gThreadName;
    }
}
