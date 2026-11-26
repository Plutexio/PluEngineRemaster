//
// Created by Plutex on 11/26/26.
//

#include "PluEngine/Platforms/Linux/SdlPlatform.h"

#ifdef PLU_PLATFORM_LINUX

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>

#include "PluEngine/Platforms/Linux/SdlWindow.h"
#include "PluEngine/FrameDemand.h"
#include "PluEngine/Timer.h"

namespace Plu
{
	void SDLPlatform::Init()
	{
		PLU_PROFILE_SCOPE("SDLPlatform::Init");
		if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_GAMEPAD)) {
			PLU_CORE_ERROR("SDL_Init failed: {}", SDL_GetError());
		}
	}

	void SDLPlatform::Shutdown()
	{
		SDL_Quit();
	}

	void SDLPlatform::PollEvents()
	{
		PLU_PROFILE_SCOPE("SDLPlatform::PollEvents");
		SDL_Event e;
		while (SDL_PollEvent(&e)) {
			// Any OS event may change what is on screen. A bare pointer move usually does not, so
			// it only asks for probe frames (the UI decides whether anything changed). The wake
			// event is excluded: whoever pushed it has already stated what it needs.
			if (e.type == SDL_EVENT_MOUSE_MOTION) RequestProbe();
			else if (e.type != SDL_EVENT_USER) RequestRedraw();

			// Not every event carries a windowID (clipboard, quit, ...) — reading e.window.windowID
			// blindly is uninitialised stack memory there. SDL_GetWindowFromEvent knows which event
			// types have one and returns null for the rest (0 is never a valid SDL window ID).
			SDL_Window* eventWindow = SDL_GetWindowFromEvent(&e);
			const int windowID = eventWindow ? static_cast<int>(SDL_GetWindowID(eventWindow)) : 0;

			if (SDLWindow** window = mWindows.Find(windowID)) {
				(*window)->OnEventSDL(&e);
			} else if (e.type == SDL_EVENT_QUIT) {
				for (auto window : mWindows) {
					window.second->OnQuitRequested();
				}
			}
		}
	}

	void SDLPlatform::WaitForEvents(float timeoutSeconds)
	{
		SDL_WaitEventTimeout(nullptr, static_cast<Sint32>(timeoutSeconds * 1000.0f));
	}

	void SDLPlatform::WakeEventLoop()
	{
		SDL_Event wake = {};
		wake.type = SDL_EVENT_USER;
		SDL_PushEvent(&wake);
	}

	void SDLPlatform::SpawnConsoleWindow()
	{
		// The log already goes to the terminal the process was started from.
	}

	DynamicArray<const char*> SDLPlatform::GetRequiredVulkanInstanceExtensions()
	{
		DynamicArray<const char*> extensions;
		Uint32 count = 0;
		// The array is owned by SDL and lives until SDL_Quit, so the pointers can be handed out as is.
		const char* const* sdlExtensions = SDL_Vulkan_GetInstanceExtensions(&count);
		if (!sdlExtensions) {
			PLU_CORE_ERROR("SDL_Vulkan_GetInstanceExtensions failed: {}", SDL_GetError());
			return extensions;
		}
		extensions.Reserve(count);
		for (Uint32 i = 0; i < count; ++i) {
			extensions.PushBack(sdlExtensions[i]);
		}
		return extensions;
	}

	void SDLPlatform::RegisterWindow(int sdlWindowID, SDLWindow* window)
	{
		mWindows.Insert(sdlWindowID, window);
	}

	void SDLPlatform::UnregisterWindow(int sdlWindowID)
	{
		mWindows.Remove(sdlWindowID);
	}
}

#endif
