//
// Created by Plutex on 08.01.2026.
//

#include "PluEngine/Engine.h"

#include "PluBase/Log.h"

namespace Plu
{
    static Engine* engine;
    void Engine::CreateEngine()
    {
        if (engine)
        {
            PLU_CORE_ERROR("Trying to create engine that already exist!");
            return;
        }
        PLU_CORE_INFO("Engine Singleton Created!");
        engine = new Engine;
    }

    Engine* Engine::GetEngine()
    {
        return engine;
    }

    void Engine::DestroyEngine()
    {
        PLU_CORE_INFO("Destroy Engine Singleton");
        delete engine;
    }

    void Engine::InitImGui(ImGuiContext* ctx)
    {
        mImGuiContext = ctx;
    }

    ImGuiContext* Engine::GetImGuiContext() const
    {
        return mImGuiContext;
    }
}
