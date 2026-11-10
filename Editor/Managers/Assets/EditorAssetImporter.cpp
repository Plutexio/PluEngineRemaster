//
// Created by Plutex on 5/25/26.
//

#include "EditorAssetImporter.h"

#include <filesystem>

#include "EditorAppContext.h"
#include "Managers/Project/EditorProjectManager.h"
#include "PluEngine/Core/Reflection/TypeTraits.h"
#include "PluEngine/Application.h"
#include "PluEngine/AssetCore/AssetLoader.h"
#include "PluEngine/AssetCore/EngineAssetManager.h"
#include "PluEngine/ThreadPool/ThreadPool.h"

void Plu::EditorAssetImporter::Initialize(DynamicArray<Path> assetPaths, ApplicationInfo *appInfo, const Path& targetDirectory)
{
    mApplicationInfo = appInfo;
    mTargetDirectory = targetDirectory;

    String extension = assetPaths[0].GetExtension();
    for (auto path : assetPaths) {
        if (path.GetExtension() != extension) {
            GetObjectEventDispatcher()->Dispatch("Finito");
            return;
        }
    }

    DynamicArray<TUsePointer<IAssetLoader>> assetLoaders = mApplicationInfo->AppAssetManager->GetAssetLoadersForExtension(extension);;
    if (assetLoaders.Size() == 1) {
        String assetType = assetLoaders[0]->GetSupportedAssetType();
        TUsePointer<IAssetLoader> assetLoader = assetLoaders[0];
        mAssetPathsPerType[assetType] = assetPaths;
        mAssetImportSettingsPerType[assetType] = assetLoader->GetImportSettingsClass();
        mAssetLoaderPerType[assetType] = assetLoader;
    } else if (assetLoaders.Size() > 1) {
        for (auto path : assetPaths) {
            for (auto assetLoader : assetLoaders) {
                if (assetLoader->CanImportAsset(path, mApplicationInfo->AppAssetManager, mApplicationInfo->AppObjectManager)) {
                    String assetType = assetLoader->GetSupportedAssetType();
                    mAssetPathsPerType[assetType].PushBack(path);
                    mAssetImportSettingsPerType[assetType] = assetLoader->GetImportSettingsClass();
                    mAssetLoaderPerType[assetType] = assetLoader;
                    break;
                }
            }
        }
    } else {
        PLU_CORE_ERROR("No asset loader for {}", extension.CStr());
    }
    if (mAssetLoaderPerType.IsEmpty() || mAssetImportSettingsPerType.IsEmpty() || mAssetPathsPerType.IsEmpty()) {
        GetObjectEventDispatcher()->Dispatch("Finito");
    }
}

extern Plu::EditorAppContext* gEditorAppContext;

Plu::Path Plu::EditorAssetImporter::GetImportDirectory() const
{
    if (!mTargetDirectory.IsEmpty()) {
        std::filesystem::create_directories(mTargetDirectory.CStr());
        return mTargetDirectory;
    }
    return gEditorAppContext->EditorProjectManager->GetProjectAssetsDirectory().ToString().ToNarrow();
}

void Plu::EditorAssetImporter::RenderUI()
{
    auto cleanup = [this]() {
        for (auto data : mAssetImportSettingsPerTypeData) {
            if (data.second) {
                delete data.second;
            }
        }
        mAssetImportSettingsPerTypeData.Clear();
        mAssetPathsPerType.Clear();
        mApplicationInfo = nullptr;
        mAssetLoaderPerType.Clear();
    };

    // Grupy bez klasy import-settingsów nie potrzebują dialogu — importuj je od razu
    // i usuń z map, żeby popup (i jego „Import") zajął się tylko resztą.
    DynamicArray<String> noSettingsTypes;
    for (auto idk : mAssetImportSettingsPerType) {
        if (!idk.second) noSettingsTypes.PushBack(idk.first);
    }
    for (auto assetType : noSettingsTypes) {
        mApplicationInfo->AppThreadPool->ParallelFor(mAssetPathsPerType[assetType].Size(), [&](int i) {
                mAssetLoaderPerType[assetType]->HandleAssetImporting(
                    mAssetPathsPerType[assetType][i],
                GetImportDirectory(),
                nullptr,
                mApplicationInfo->AppAssetManager,
                mApplicationInfo->AppObjectManager
            );
        });
        mAssetPathsPerType.Remove(assetType);
        mAssetImportSettingsPerType.Remove(assetType);
        mAssetLoaderPerType.Remove(assetType);
    }

    // Nic z settingsami do skonfigurowania — kończymy.
    if (mAssetImportSettingsPerType.IsEmpty()) {
        cleanup();
        GetObjectEventDispatcher()->Dispatch("Finito");
        return;
    }
    if (mFirstTime) {
        ImGui::OpenPopup("Asset Import Settings");
        for (auto idk2 : mAssetImportSettingsPerType) {
            mAssetImportSettingsPerTypeData[idk2.first] = idk2.second->Construct();
        }
        mFirstTime = false;
    }
    if (ImGui::BeginPopupModal("Asset Import Settings")) {
        if (ImGui::BeginTabBar("##assetimportsettings")) {
            for (auto settingsData : mAssetImportSettingsPerTypeData) {
                if (ImGui::BeginTabItem(mAssetLoaderPerType[settingsData.first]->GetSupportedAssetType().CStr())) {
                    TypeSerializer<TypeInfo*>::EditorControl(mAssetImportSettingsPerType[settingsData.first], settingsData.second);
                    ImGui::EndTabItem();
                }
            }
            ImGui::EndTabBar();
        }
        ImGui::Separator();
        if (ImGui::Button("Cancel")) {
            ImGui::CloseCurrentPopup();
            cleanup();
            GetObjectEventDispatcher()->Dispatch("Finito");
        }
        if (ImGui::Button("Import")) {
            for (auto loader : mAssetLoaderPerType) {
                mApplicationInfo->AppThreadPool->ParallelFor(mAssetPathsPerType[loader.first].Size(), [&](int i) {
                    loader.second->HandleAssetImporting(
                        mAssetPathsPerType[loader.first][i],
                     GetImportDirectory(),
                     mAssetImportSettingsPerTypeData[loader.first],
                     mApplicationInfo->AppAssetManager,
                     mApplicationInfo->AppObjectManager
                     );
                });
            }
            ImGui::CloseCurrentPopup();
            cleanup();
            GetObjectEventDispatcher()->Dispatch("Finito");
        }
        ImGui::EndPopup();
    }
}
