#pragma once
#include "PluEngine/Core.h"
#ifdef PLU_PLATFORM_WINDOWS


#include "PlatformInputBackend.h"
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <Xinput.h>
#pragma comment(lib, "Xinput.lib")

namespace Plu
{
    class PLUPLATFORM_API WinAPIInputBackend final : public PlatformInputBackend
    {
    public:
        // =========================================================
        //  Init / Shutdown
        // =========================================================

        bool Init() override;

        void Shutdown() override { /* nothing to clean up */ }

        // =========================================================
        //  Frame lifecycle
        // =========================================================

        void BeginFrame() override
        {
            // Reset per-frame scroll accumulator before processing messages.
            // Actual message pumping should happen in your WndProc / message loop.
            // Call FeedMessage() from WndProc for WM_KEYDOWN, WM_KEYUP,
            // WM_MOUSEMOVE, WM_MOUSEWHEEL, WM_LBUTTONDOWN, etc.
        }

        void Update() override;

        void EndFrame() override;

        // =========================================================
        //  WndProc hook – call this from your window procedure
        //  for: WM_MOUSEWHEEL, WM_MOUSEHWHEEL
        // =========================================================

        void FeedMessage(UINT msg, WPARAM wParam, LPARAM /*lParam*/);

        // =========================================================
        //  Accessors
        // =========================================================

        [[nodiscard]] const KeyboardState&    GetKeyboard()           const override { return m_keyboard; }
        [[nodiscard]] const MouseState&       GetMouse()              const override { return m_mouse;    }
        [[nodiscard]] const GenericController& GetController(int idx) const override
        {
            return m_controllers[idx < kMaxControllers ? idx : 0];
        }

        void SetRumble(int index, float low, float high) override;

        void SetMouseCaptured(bool captured) override;
        [[nodiscard]] bool IsMouseCaptured() const override;

        bool IsMouseCentered() const override;
        void SetMouseCentered(bool centered) override;

        void ResyncMousePosition() override;

    private:
        KeyboardState     m_keyboard{};
        MouseState        m_mouse{};
        GenericController m_controllers[kMaxControllers]{};
        bool              m_mouseCaptured = false;
        bool mMouseCentered = false;

        // =========================================================
        //  XInput controller update
        // =========================================================

        void UpdateXInputController(DWORD index);

        // =========================================================
        //  Key → Virtual Key mapping
        // =========================================================

        static int KeyToVirtualKey(Key key);
    };
}

#endif // _WIN32
