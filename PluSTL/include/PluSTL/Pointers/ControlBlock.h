#pragma once
#include <atomic>
#include <thread>
#include <cassert>

// Thread-affinity check (MT): a TOwningPointer (strong ownership) may only be copied,
// dereferenced or released on the thread that created the object. Other threads observe the
// object through TUsePointer (read-only). The owning thread is captured in ControlBlockBase
// at construction. Debug-only — compiles away in release, and can be force-disabled with
// PLU_DISABLE_PTR_THREAD_CHECKS. See TOwningPointer / Casts for the call sites.
#if !defined(NDEBUG) && !defined(PLU_DISABLE_PTR_THREAD_CHECKS)
    #define PLU_PTR_ASSERT_OWNER(controlPtr)                                                  \
        assert(((controlPtr) == nullptr ||                                                    \
                std::this_thread::get_id() == (controlPtr)->owningThread) &&                  \
            "TOwningPointer used off its owning thread (observe from other threads via TUsePointer)")
#else
    #define PLU_PTR_ASSERT_OWNER(controlPtr) ((void)0)
#endif

namespace Plu
{
    // Refcount control block shared by TOwningPointer (strong) and TUsePointer (weak).
    //
    // Thread-safety follows the canonical shared_ptr protocol so that owners/observers on
    // different threads can release concurrently without racing on the object or the block:
    //
    //   strongCount = number of owners. When it reaches 0 the managed object is destroyed.
    //   weakCount   = number of use-pointers + 1. The extra +1 is a single collective weak
    //                 reference held by all owners together; it is dropped when the last owner
    //                 goes away. When weakCount reaches 0 the control block itself is freed.
    //
    // Exactly one counter (weakCount) decides the block's lifetime, which removes the
    // check-then-act double-free of the old two-counter design. Increments are relaxed (the
    // caller already holds a live reference); the decrement that reaches 0 is acq_rel so the
    // destroying thread observes every prior modification through the released object.
    struct ControlBlockBase
    {
        void* ptr;
        std::atomic<int> strongCount{1};
        std::atomic<int> weakCount{1};
        // Set when something else owns the managed object (e.g. a script interpreter that
        // destroys it itself). The last TOwningPointer then only releases its reference and
        // never deletes the object.
        bool externallyOwned = false;

        // Thread that created (and therefore owns) this object. Captured here at control-block
        // construction, i.e. on whichever thread first wrapped the raw pointer. Read only by the
        // PLU_PTR_ASSERT_OWNER debug check; no migration support yet (owner is fixed for life).
        std::thread::id owningThread = std::this_thread::get_id();

        explicit ControlBlockBase(void* ptr) : ptr(ptr) {}
        virtual ~ControlBlockBase() = default;
    };

    template<typename T>
    struct ControlBlock : ControlBlockBase
    {
        explicit ControlBlock(T* p) : ControlBlockBase(p) {}
        T* Get() const { return static_cast<T*>(ptr); }
    };
}
