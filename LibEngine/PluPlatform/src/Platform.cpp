//
// Created by Plutex on 11/26/26.
//

#include "PluEngine/Platform/Platform.h"

#ifdef PLU_PLATFORM_WINDOWS
#include "PluEngine/Platforms/Windows/WindowsPlatform.h"
#elif defined(PLU_PLATFORM_LINUX)
#include "PluEngine/Platforms/Linux/SdlPlatform.h"
#endif
#include "PluEngine/Core/Objects/EngineObjectManager.h"

namespace Plu
{
    TOwningPointer<IPlatform> IPlatform::PlutexCreatePlatform(const TUsePointer<EngineObjectManager>& objectManager,
                                                              ApplicationInfo* applicationInfo)
    {
#ifdef PLU_PLATFORM_LINUX
        TUsePointer<SDLPlatform> platformUser = objectManager->CreateObject(SDLPlatform::GetStaticClass());
        TOwningPointer<SDLPlatform> platform = objectManager->GetObjectAsOwner<SDLPlatform>(platformUser->GetObjectHandle());
#elif defined(PLU_PLATFORM_WINDOWS)
        TUsePointer<WindowsPlatform> platformUser = objectManager->CreateObject(WindowsPlatform::GetStaticClass());
        TOwningPointer<WindowsPlatform> platform = objectManager->GetObjectAsOwner<WindowsPlatform>(platformUser->GetObjectHandle());
#else
        return nullptr;
#endif
        platform->mApplicationInfo = applicationInfo;
        return platform;
    }
}
