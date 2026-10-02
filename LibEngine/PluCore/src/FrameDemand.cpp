#include "PluEngine/FrameDemand.h"

#include <atomic>
#include <chrono>

#include "PluEngine/Core/Threading/ThreadAffinity.h"

namespace
{
    using Nanoseconds = Int64;

    std::atomic<bool> gPowerSavingEnabled{false};
    // Frames are wanted until this point in time.
    std::atomic<Nanoseconds> gActiveUntil{0};
    // Probe frames are wanted until this point in time.
    std::atomic<Nanoseconds> gProbeUntil{0};
    // Main thread only.
    bool gProbeFrame = false;
    // Next scheduled single frame, 0 = none.
    std::atomic<Nanoseconds> gNextDeadline{0};
    std::atomic<bool> gContinuous{false};
    std::atomic<void (*)()> gWakeCallback{nullptr};
    // Collapses a burst of off-thread requests into one wake; re-armed by ConsumeFrameDemand.
    std::atomic<bool> gWakePending{false};

    Nanoseconds Now()
    {
        return std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
    }

    Nanoseconds ToNanoseconds(float seconds)
    {
        return static_cast<Nanoseconds>(static_cast<double>(seconds) * 1e9);
    }

    void ExtendTo(std::atomic<Nanoseconds>& target, Nanoseconds until)
    {
        Nanoseconds current = target.load(std::memory_order_relaxed);
        while (current < until && !target.compare_exchange_weak(current, until, std::memory_order_relaxed)) {}
    }

    // The main thread is never inside its event wait while it runs code, so only requests from
    // other threads need to wake it.
    void WakeMainIfNeeded()
    {
        if (Plu::IsOnMainThread()) return;
        if (gWakePending.exchange(true, std::memory_order_acq_rel)) return;
        if (void (*callback)() = gWakeCallback.load(std::memory_order_acquire)) callback();
    }
}

void Plu::SetPowerSavingEnabled(bool enabled)
{
    gPowerSavingEnabled.store(enabled, std::memory_order_relaxed);
    // Switching it on must not leave the screen on a half-settled frame.
    RequestRedraw();
}

bool Plu::IsPowerSavingEnabled()
{
    return gPowerSavingEnabled.load(std::memory_order_relaxed);
}

void Plu::RequestRedraw()
{
    ExtendTo(gActiveUntil, Now() + ToNanoseconds(kRedrawSettleSeconds));
    WakeMainIfNeeded();
}

void Plu::RequestProbe()
{
    ExtendTo(gProbeUntil, Now() + ToNanoseconds(kRedrawSettleSeconds));
    WakeMainIfNeeded();
}

void Plu::SetProbeFrame(bool probe)
{
    gProbeFrame = probe;
}

bool Plu::IsProbeFrame()
{
    return gProbeFrame;
}

void Plu::RequestRedrawAfter(float seconds)
{
    const Nanoseconds deadline = Now() + ToNanoseconds(seconds < 0.0f ? 0.0f : seconds);
    Nanoseconds current = gNextDeadline.load(std::memory_order_relaxed);
    while ((current == 0 || deadline < current)
        && !gNextDeadline.compare_exchange_weak(current, deadline, std::memory_order_relaxed)) {}
    WakeMainIfNeeded();
}

void Plu::RequestContinuousRedraw()
{
    gContinuous.store(true, std::memory_order_relaxed);
    WakeMainIfNeeded();
}

Plu::FrameDemand Plu::ConsumeFrameDemand()
{
    // Re-arm before reading: a request landing after this line wakes the loop again, one landing
    // before it is seen by the reads below.
    gWakePending.store(false, std::memory_order_release);

    FrameDemand demand;
    const Nanoseconds now = Now();

    if (gContinuous.exchange(false, std::memory_order_relaxed)) demand.MustPresent = true;
    if (now < gActiveUntil.load(std::memory_order_relaxed)) demand.MustPresent = true;

    Nanoseconds deadline = gNextDeadline.load(std::memory_order_relaxed);
    if (deadline != 0 && now >= deadline) {
        // Only clear the deadline we saw fire; a newer one set meanwhile stays.
        gNextDeadline.compare_exchange_strong(deadline, 0, std::memory_order_relaxed);
        demand.MustPresent = true;
        deadline = 0;
    }

    demand.RenderNow = demand.MustPresent || now < gProbeUntil.load(std::memory_order_relaxed);

    if (!demand.RenderNow) {
        // No deadline: sleep until an event. The caller caps this with its own heartbeat.
        demand.WaitSeconds = deadline != 0 ? static_cast<float>(static_cast<double>(deadline - now) / 1e9) : 3600.0f;
    }
    return demand;
}

void Plu::SetFrameDemandWakeCallback(void (*callback)())
{
    gWakeCallback.store(callback, std::memory_order_release);
}
