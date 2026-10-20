//
// Created by Plutex on 2026-08-07.
//

#ifndef PLUSTL_CONCURRENT_H
#define PLUSTL_CONCURRENT_H

// ============================================================================
// PluSTL/Concurrent — containers that carry their own synchronization
// ============================================================================
//
// Umbrella header. Include it when a translation unit uses several of these;
// otherwise include the one container you need. Nothing here is in PluSTL_FWD.h
// on purpose — that header is the precompiled header for the whole project and
// these pull in <atomic>/<mutex>/<shared_mutex>/<thread>.
//
// ---------------------------------------------------------------------------
// The API is the single-threaded one
// ---------------------------------------------------------------------------
// Each container mirrors its PluSTL counterpart, name for name, so switching a
// member over is a type change and not a rewrite:
//
//   ConcurrentHashMap  <- HashMap        ConcurrentArray   <- DynamicArray
//   ConcurrentHashSet  <- HashSet        ConcurrentString  <- String
//   ConcurrentQueue    <- Queue (it is one, behind a mutex), plus Drain()
//   ConcurrentRingQueue<- Queue, minus what a bounded lock-free ring cannot
//                         promise: TryPushBack can fail.
//
// Insert / Emplace / Contains / Remove / Clear / Size / IsEmpty / Capacity /
// Reserve / Rehash / IndexOf / LoadFactor mean exactly what they mean on the
// single-threaded type. Where a member is missing, the two rules below say why,
// and the container's header names the replacement.
//
// ---------------------------------------------------------------------------
// Rule 1 — no raw handle into the storage ever escapes
// ---------------------------------------------------------------------------
// No method returns a pointer, reference or iterator into the container. This is
// not an oversight; it is why a LockPolicy bolted onto the existing containers
// was rejected. DynamicArray::Iterator *is* T*, HashMap::Find returns
// TValue*, HashSet::Find returns an iterator — under a lock every one of those
// dangles as soon as another thread rehashes or reallocates. So:
//
//   read       -> copy out:  Find(key, out) / Get(index, out) / Snapshot()
//   mutate     -> visitor:   Visit / VisitOrInsert / Write
//   iterate    -> visitor:   ForEach / Drain / Snapshot
//   push       -> returns an index (ConcurrentArray), never a T&
//
// ---------------------------------------------------------------------------
// Rule 2 — a callback runs under the container's lock
// ---------------------------------------------------------------------------
// Everything handed to Visit / VisitOrInsert / ForEach / Drain / Read / Write
// runs while the lock is held. It must not block, must not allocate heavily or
// do I/O, must not take another PluSTL lock, and must not re-enter the same
// container. For the striped containers that lock is a spinlock — a waiter burns
// a core the whole time. Copy what you need out and do the real work after the
// call returns. (ConcurrentString uses a shared_mutex instead, but it is not
// recursive either.)
//
// ---------------------------------------------------------------------------
// Rule 3 — Size() and friends are true when read, not when used
// ---------------------------------------------------------------------------
// Counts are relaxed atomic loads: telemetry and "is there anything to do at
// all" fast-outs, never control flow that assumes the answer stays true.
//
// ---------------------------------------------------------------------------
// Ownership
// ---------------------------------------------------------------------------
// All of them are non-copyable and non-movable: each is meant to be owned by one
// subsystem for its whole lifetime. Copy a Snapshot() if you need a value.
//
// Shared machinery lives in Concurrent/Detail/ (the striped table behind the map
// and the set, the shared_mutex holder behind ConcurrentString) and in
// LockPrimitives.h (SpinBackoff, SpinLock, StripeArray).
//
// Tests: Tests/PluSTLTests — unit + stress, clean under ThreadSanitizer.

#include "PluSTL/Concurrent/LockPrimitives.h"
#include "PluSTL/Concurrent/ConcurrentArray.h"
#include "PluSTL/Concurrent/ConcurrentHashMap.h"
#include "PluSTL/Concurrent/ConcurrentHashSet.h"
#include "PluSTL/Concurrent/ConcurrentQueue.h"
#include "PluSTL/Concurrent/ConcurrentString.h"

#endif //PLUSTL_CONCURRENT_H
