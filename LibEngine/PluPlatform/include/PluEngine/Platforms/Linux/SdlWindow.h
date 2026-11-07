//
// Created by Plutex on 1/5/26.
//

#ifndef PLUENGINE_SDLWINDOW_H
#define PLUENGINE_SDLWINDOW_H

#include "PluEngine/Core.h"

#ifdef PLU_PLATFORM_LINUX

#include "PluEngine/Platform/Window.h"
#include <SDL3/SDL.h>
#include "SdlWindow.generated.h"

namespace Plu
{
	PLU_CLASS()
	class SDLWindow final : public IWindow
	{
		REFLECTION_BODY_SDLWINDOW()
	protected:
		void Close() override;
	public:
		static void InitSDL();
		static void HandleSDLEvents();
		// Power saving (FrameDemand.h). Blocks the calling (main) thread until an event is queued
		// or the timeout runs out; the event stays queued for HandleSDLEvents().
		static void WaitForEvents(float timeoutSeconds);
		// Any thread. Breaks WaitForEvents() out of its wait.
		static void WakeEventLoop();
		void SetCursorVisibility(bool visible) override;

		explicit SDLWindow();
		~SDLWindow() override;

		void Init() override;
		void OnUpdate(float deltaTime) override;
		void OnEventSDL(SDL_Event* e);
		void Shutdown() override;

		bool IsRunning() override;
		// SDL's own window id (the one carried by SDL_Event), not IWindow::GetWindowID().
		int GetSDLWindowID() const;

		int GetWidth() override;
		int GetHeight() override;

		IVec2 GetWindowPosition() override;
		void SetWindowPosition(IVec2 position) override;

		void Minimize() override;
		void Maximize() override;
		bool IsWindowMinimized() override;
		bool IsWindowMaximized() override;

		void MakeFullscreen(FullscreenType type, IVec2 resolution = IVec2(0, 0)) override;
		FullscreenType GetFullscreenType() const override;
		DynamicArray<DisplayMode> GetSupportedDisplayModes() override;
		IVec2 GetDesktopResolution() override;

		bool IsVSyncEnabled() override;
		void SetVSyncEnabled(bool enabled) override;
		void ApplySwapInterval(bool vsync) override;

		void* GetWindowHandle() override;
		void *GetGLContext() override;
		void MakeGLContextCurrent() override;
		void ReleaseGLContext() override;
		void SwapBuffer() override;

		void SetWindowTitle(String title) override;
		void SetCursorPosition(IVec2 pos) override;
		IVec2 GetCursorPosition() override;

		bool HasWindowFocus() override;
	private:
		SDL_Window* mWindow = nullptr;
		SDL_GLContext mGLContext = nullptr;

		int mSDLWindowID = -1;

		bool mRunning = false;
		bool mVSyncEnabled = false;
		bool mRequestedVSync = true;

		FullscreenType mFullscreenType = FullscreenType::Windowed;
	};

}

#endif

#endif //PLUENGINE_SDLWINDOW_H