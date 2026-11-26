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
	class SDLPlatform;

	PLU_CLASS()
	class SDLWindow final : public IWindow
	{
		REFLECTION_BODY_SDLWINDOW()
	protected:
		void Close() override;
	public:
		void SetCursorVisibility(bool visible) override;

		explicit SDLWindow();
		~SDLWindow() override;

		void Init() override;
		void OnUpdate(float deltaTime) override;
		void OnEventSDL(SDL_Event* e);
		// SDL_EVENT_QUIT carries no window; the platform forwards it to every window as a close request.
		void OnQuitRequested();
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

		void* GetWindowHandle() override;

		void SetWindowTitle(String title) override;
		void SetCursorPosition(IVec2 pos) override;
		IVec2 GetCursorPosition() override;

		bool HasWindowFocus() override;
	private:
		SDL_Window* mWindow = nullptr;
		// Routes SDL events to this window; set in Init(), when the window registers with it.
		TUsePointer<SDLPlatform> mPlatform;

		int mSDLWindowID = -1;

		bool mRunning = false;
		bool mVSyncEnabled = true;

		FullscreenType mFullscreenType = FullscreenType::Windowed;
	};

}

#endif

#endif //PLUENGINE_SDLWINDOW_H