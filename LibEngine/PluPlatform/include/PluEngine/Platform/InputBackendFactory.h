#pragma once
#include "PlatformInputBackend.h"
#include <memory>

#include "PluSTL/PluSTL_FWD.h"
#include "PluEngine/Core.h"

// ============================================================
//  InputBackendFactory
//
//  Automatically selects the correct backend for the current
//  platform. Include this header and call CreateInputBackend()
//  — you never have to ifdef in your own code.
//
//  Example usage:
//
//      auto input = InputBackendFactory::Create();
//      input->Init();
//
//      // game loop:
//      while (running)
//      {
//          input->BeginFrame();
//          input->Update();
//
//          if (input->GetKeyboard().JustPressed(Key::Escape))
//              running = false;
//
//          if (input->GetMouse().JustPressed(MouseButton::Left))
//              Fire();
//
//          auto* pad = input->GetFirstConnected();
//          if (pad && pad->IsDown(GenericButton::South))
//              Jump();
//
//          input->EndFrame();
//      }
//
//      input->Shutdown();
// ============================================================

#if defined(PLU_PLATFORM_LINUX) || defined(SDL_INPUT_BACKEND_FORCE)
#   include "SDLInputBackend.h"
    using PlatformBackendImpl = SDLInputBackend;
#elif defined(PLU_PLATFORM_WINDOWS)
#   include "WinAPIInputBackend.h"
    using PlatformBackendImpl = Plu::WinAPIInputBackend;
#else
#   error "Unsupported platform – implement a PlatformInputBackend subclass."
#endif

class InputBackendFactory
{
public:
    static Plu::TOwningPointer<PlatformInputBackend> Create()
    {
        Plu::TOwningPointer<PlatformBackendImpl> backend = Plu::CreateOwning<PlatformBackendImpl>();
        return backend;
    }
};
