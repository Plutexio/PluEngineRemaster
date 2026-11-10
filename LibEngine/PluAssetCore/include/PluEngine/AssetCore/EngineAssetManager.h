//
// Created by Plutex on 5/20/26.
//

#ifndef PLUENGINE_ENGINEASSETMANAGER_H
#define PLUENGINE_ENGINEASSETMANAGER_H
#include "PluEngine/Core.h"
#include "PluEngine/Core/Objects/EngineObject.h"
#include "PluEngine/AssetCore/AssetDescriptor.h"
#include "EngineAssetManager.generated.h"
#include "PluBase/PluUUID.h"
#include "PluBase/ThreadAffinity.h"
#include "PluSTL/HashSet/HashSet.h"
#include "PluSTL/Concurrent/ConcurrentHashSet.h"
#include "PluSTL/Concurrent/ConcurrentQueue.h"
#include <shared_mutex>
#include <mutex>

namespace Plu
{
    class IAssetLoader;
    struct IAssetData;


    PLU_CLASS()
    class PLUASSETCORE_API EngineAssetManager : public EngineObject
    {
        REFLECTION_BODY_ENGINEASSETMANAGER()
    private:
        HashMap<UInt64, TOwningPointer<AssetDescriptor>> mAssetMap;
        HashMap<UInt64, Path> mAssetPathMap;

        HashMap<UInt64, TOwningPointer<IAssetData>> mAssetDataMap;
        HashMap<Path, UInt64> mAssetPathByUUIDMap;
#ifdef PLU_ENGINE_EDITOR_BUILD
        void DispatchAssetSaveBinary(PluUUID uuid);
        void DispatchAssetSaveJSON(PluUUID uuid);

        HashSet<UInt64> mDirtyAssets;
#endif

        void LoadJSONAssetData(TUsePointer<AssetDescriptor> assetDesc);
        void LoadBinaryAssetData(TUsePointer<AssetDescriptor> assetDesc);

        PluUUID RegisterAssetDescriptor(const AssetDescriptorHeader& header);

        ApplicationInfo* mApplicationInfo = nullptr;

        HashMap<String,TOwningPointer<IAssetLoader>> mAssetLoaders;
        void RegisterAssetDataFromLoader(TOwningPointer<IAssetData> assetData, TUsePointer<AssetDescriptor> assetDesc);
        friend class IAssetLoader;

        mutable std::shared_mutex mMutex;
        ConcurrentHashSet<UInt64> mPendingLoadRequests;
        // Headers read by LoadAssetDescriptor off the main thread, registered in ProcessPendingLoads().
        ConcurrentQueue<AssetDescriptorHeader> mPendingDescriptors;

        // Mutations remain main-thread-only. Asserts in debug, no-op in release.
        void CheckOwnerThread() const
        {
            PLU_CORE_ASSERT(IsOnMainThread(), "EngineAssetManager mutation off the main thread");
        }
    public:
        EngineAssetManager();
        virtual ~EngineAssetManager() override;

        //Setup
        void Initialize(ApplicationInfo* appInfo);
        void PrepareLoaders();

        //Loading
        // Any thread. Off the main thread the descriptor is registered (and announced) by the next
        // ProcessPendingLoads(), so it is not visible to getters yet when this returns.
        void LoadAssetDescriptor(Path assetPath);
        void ScanDirectory(const Path &assetPath, bool engineAssets = false);
        void LoadAssetData(TUsePointer<AssetDescriptor> assetDesc);

        TUsePointer<AssetDescriptor> GetAssetDescriptor(PluUUID uuid);
        TUsePointer<IAssetData> GetAssetData(PluUUID uuid);
        TUsePointer<IAssetData> GetAssetData(TUsePointer<AssetDescriptor> assetDesc);
        [[nodiscard]] TUsePointer<IAssetData> GetAssetDataNoLoad(PluUUID uuid) const;
        TUsePointer<IAssetLoader> GetAssetLoader(TypeInfo* type);

        void RequestAssetDataLoad(PluUUID uuid);
        void ProcessPendingLoads();

        //Getters for Paths
        Path GetAssetPath(PluUUID uuid);
        Path GetAssetPath(TUsePointer<AssetDescriptor> assetDesc);

        //Validation
        [[nodiscard]] bool AssetExists(PluUUID uuid) const;
        [[nodiscard]] bool AssetExistsInPath(Path assetPath) const;
        [[nodiscard]] bool IsAssetLoaded(PluUUID uuid) const;

#ifdef PLU_ENGINE_EDITOR_BUILD
        //Getters
        TUsePointer<AssetDescriptor> GetAssetDescriptor(Path assetPath);
        TUsePointer<IAssetData> GetAssetData(Path assetPath);
        DynamicArray<TUsePointer<IAssetLoader>> GetAssetLoadersForExtension(String extension);

        //Validation
        bool AssetExistsWithName(String assetName);
        [[nodiscard]] bool AnyAssetsUnderDirectory(const Path& directory) const;

        void RelocateAssets(const Path& oldPath, const Path& newPath);

        //Slow Section
        DynamicArray<TUsePointer<AssetDescriptor>> GetAllAssetDescriptorsOfType(TypeInfo* type);

        //Utils
        void PrepareAssetsForDistribution(Path dir);
        void ConstructPythonAssetDictionary(Path file);

        //Dirtieness, btw only way do clean the asset is to save it
        void MarkAssetDirty(TUsePointer<AssetDescriptor> assetDesc);
        bool IsAssetDirty(TUsePointer<AssetDescriptor> assetDesc) const;
        bool AreAnyAssetsDirty() const;

        //Saving
        void SaveAsset(TUsePointer<AssetDescriptor> assetDesc);
        void SaveAsset(PluUUID uuid);
        void SaveAsset(TUsePointer<IAssetData> assetDesc);
#endif
    };

    PLU_FUNCTION(PyName=GetAssetByUUID)
    PLUASSETCORE_API TUsePointer<IAssetData> GetAssetByUUID(UInt64 uuid);

    PLUASSETCORE_API TUsePointer<IAssetData> GetAssetUserAsRaw(IAssetData* assetInfo);
}

#endif //PLUENGINE_ENGINEASSETMANAGER_H
