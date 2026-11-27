//
// Created by Plutex on 11/26/26.
//

#ifndef PLUENGINE_SDLPLATFORM_H
#define PLUENGINE_SDLPLATFORM_H

#include "PluEngine/Core.h"

#ifdef PLU_PLATFORM_LINUX

#include "PluEngine/Platform/Platform.h"
#include "PluSTL/HashMap/HashMap.h"
#include "SdlPlatform.generated.h"

namespace Plu
{
	class SDLWindow;

	PLU_CLASS()
	class SDLPlatform final : public IPlatform
	{
		REFLECTION_BODY_SDLPLATFORM()
	public:
		explicit SDLPlatform() = default;
		~SDLPlatform() override = default;

		void Init() override;
		void Shutdown() override;

		void PollEvents() override;
		void WaitForEvents(float timeoutSeconds) override;
		void WakeEventLoop() override;

		void SpawnConsoleWindow() override;

		DynamicArray<const char*> GetRequiredVulkanInstanceExtensions() override;
		bool GetVulkanPresentationSupport(VkInstance instance, VkPhysicalDevice physicalDevice,
		                                  UInt32 queueFamilyIndex) override;
		bool CreateVulkanSurface(const TUsePointer<IWindow>& window, VkInstance instance,
		                         VkSurfaceKHR* outSurface) override;

		// Event routing: SDL events carry SDL's window id, so live windows are looked up here by it.
		// A window registers once its SDL_Window exists and must unregister before destroying it.
		void RegisterWindow(int sdlWindowID, SDLWindow* window);
		void UnregisterWindow(int sdlWindowID);
	private:
		HashMap<int, SDLWindow*> mWindows;
	};
}

#endif

#endif //PLUENGINE_SDLPLATFORM_H
