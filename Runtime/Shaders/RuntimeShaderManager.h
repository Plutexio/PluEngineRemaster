//
// Created by Plutex on 6/5/26.
//

#ifndef PLUENGINE_RUNTIMESHADERMANAGER_H
#define PLUENGINE_RUNTIMESHADERMANAGER_H

#include "PluEngine/Render/ShadersManager.h"
#include "RuntimeShaderManager.generated.h"
#include "PluEngine/Render/ShaderCacheWriter.h"

namespace Plu
{
    class RuntimeShaderCode;

    PLU_CLASS()
    class RuntimeShaderWriter : public IShaderCacheWriter
    {
        REFLECTION_BODY_RUNTIMESHADERWRITER()
    public:
        RuntimeShaderWriter() = default;
        virtual ~RuntimeShaderWriter() override = default;
        PathW GetShaderCacheDirectory() override;
    };

    PLU_CLASS()
    class RuntimeShaderManager : public IShaderManager
    {
        REFLECTION_BODY_RUNTIMESHADERMANAGER()
    private:
        HashMap<UInt64, TOwningPointer<RuntimeShaderCode>> mShaderCodes;
        HashMap<UInt64, TOwningPointer<ShaderProgram>> mShaderPrograms;
        DynamicArray<TUsePointer<ShaderProgram>> mShaderProgramsUsers;

        TUsePointer<EngineAssetManager> mAssetManager;
        TUsePointer<EngineObjectManager> mObjectManager;
    public:
        RuntimeShaderManager();
        virtual ~RuntimeShaderManager() override;

        void InitAssetEvents(TUsePointer<EngineAssetManager> assetManager, TUsePointer<EngineObjectManager> objectManager);
        void ShaderCodeScan();

        bool ShaderCodeExists(PluUUID uuid) override;
        DynamicArray<TUsePointer<ShaderProgram>> *GetRenderableShaderPrograms() override;
        TUsePointer<IShaderCode> GetShaderCode(PluUUID uuid) override;
        TUsePointer<ShaderProgram> GetShaderProgram(PluUUID uuid) override;
        void LoadShader(PluUUID uuid) override;
        void ReleaseRenderResources() override;
    };
}



#endif //PLUENGINE_RUNTIMESHADERMANAGER_H
