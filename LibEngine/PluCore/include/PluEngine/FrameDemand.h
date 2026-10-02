#ifndef PLUENGINE_FRAMEDEMAND_H
#define PLUENGINE_FRAMEDEMAND_H

#include "PluEngine/Core.h"

// Power saving: frames on demand.
//
// With power saving enabled the main loop stops free-running. It blocks on OS events and only
// builds a frame (app tick, ImGui, render snapshot) while something asks for one; the render
// thread in turn only presents when it was handed something new. ImGui is immediate mode, so
// "did anything change" cannot be answered after the fact — whoever animates without input has to
// say so through one of the Request* calls below.
//
// Every function here is thread-safe. Requests made off the main thread also wake its event wait.
namespace Plu
{
    // How long the loop keeps producing frames after RequestRedraw(). Time based rather than a
    // frame count: it has to cover ImGui's delayed tooltips, popups that need a second frame to
    // size themselves, and assets that land a moment after the frame that asked for them.
    inline constexpr float kRedrawSettleSeconds = 1.0f;

    // Refresh interval for panels that only show slowly changing numbers (profiler, stats):
    // RequestRedrawAfter(kStatsRefreshSeconds) each frame they draw. Fast enough to read, slow
    // enough that leaving such a panel open does not defeat power saving.
    inline constexpr float kStatsRefreshSeconds = 0.5f;

    struct FrameDemand
    {
        // A frame is wanted right now.
        bool RenderNow = false;
        // The frame has to reach the screen. False for a probe frame: one that is built only to
        // find out whether the UI changed (see RequestProbe).
        bool MustPresent = false;
        // Otherwise: how long the loop may sleep before the next scheduled frame.
        float WaitSeconds = 0.0f;
    };

    // Off by default — an app that never enables it runs every frame, as before.
    PLUCORE_API void SetPowerSavingEnabled(bool enabled);
    PLUCORE_API bool IsPowerSavingEnabled();

    // "Something changed": keep frames coming for kRedrawSettleSeconds from now.
    PLUCORE_API void RequestRedraw();
    // "Something happened that may or may not change the picture" — mouse motion. Keeps probe
    // frames coming for kRedrawSettleSeconds: the app builds its UI as usual, but the result is
    // only handed to the render thread if it differs from what is already on screen, and the
    // scene is not re-rendered at all. A plain pointer move over an idle UI thus costs an ImGui
    // build on the main thread and nothing on the GPU.
    PLUCORE_API void RequestProbe();
    // One frame no later than `seconds` from now (text cursor blink, slowly refreshing stats).
    // Only ever pulls the next scheduled frame closer, so re-requesting every frame is fine.
    PLUCORE_API void RequestRedrawAfter(float seconds);
    // Full frame rate for as long as this keeps being called: it covers the next frame only, so
    // whoever animates renews it each frame it draws and it lapses by itself when they stop.
    PLUCORE_API void RequestContinuousRedraw();

    // Main loop only. Reads the demand and clears the one-shot parts of it.
    PLUCORE_API FrameDemand ConsumeFrameDemand();

    // Whether the frame being built right now is a probe frame. Set by the main loop before the
    // app tick; read by whoever publishes to the render thread. Main thread only.
    PLUCORE_API void SetProbeFrame(bool probe);
    PLUCORE_API bool IsProbeFrame();

    // Called (from the requesting thread) when a request arrives off the main thread, to break the
    // main loop out of its event wait. Installed by Application, which knows the platform.
    PLUCORE_API void SetFrameDemandWakeCallback(void (*callback)());
}

#endif //PLUENGINE_FRAMEDEMAND_H
