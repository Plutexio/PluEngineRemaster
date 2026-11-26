//
// Created by Plutex on 11/26/26.
//

#include "PluEngine/Render/GraphicsAPIController.h"

#include "PluEngine/Core/ApplicationInfo.h"
#include "PluEngine/Core/Objects/EngineObjectManager.h"

Plu::TOwningPointer<Plu::GraphicsApiController> Plu::GraphicsApiController::CreateApiController(
    TClassPointer<GraphicsApiController> controllerClass, ApplicationInfo *appInfo)
{
    PLU_CORE_ASSERT(!controllerClass.GetRawType()->IsAbstract, "Provide a valid class for GraphicsApiController!")
    TUsePointer<GraphicsApiController> newController = appInfo->AppObjectManager->CreateObject(controllerClass.GetRawType());
    newController->Init();
    newController->mAppInfo = appInfo;
    return appInfo->AppObjectManager->GetObjectAsOwner<GraphicsApiController>(newController->GetObjectHandle());
}
