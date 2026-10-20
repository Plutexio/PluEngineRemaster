//
// Created by Plutex on 2026-07-03.
//

#ifndef PLUENGINE_RENDERUSAGESTATS_H
#define PLUENGINE_RENDERUSAGESTATS_H

#include "PluEngine/Core.h"
#include "PluSTL/PluSTL_FWD.h"

namespace Plu {

    // Pojedynczy wpis licznika użycia assetu (mesh / tekstura). Kluczem w rejestrze jest
    // surowy UInt64 UUID assetu; nazwę do wyświetlenia panel rozwiązuje przez AssetManager.
    struct AssetUsageEntry {
        UInt64 TotalUses = 0;        // suma użyć od ostatniego resetu (ranking "hottest")
        UInt32 LastFrameUses = 0;    // ile razy asset był użyty w POPRZEDNIej klatce (bieżąca liczba instancji)
        UInt32 CurrentFrameUses = 0; // akumulator dla trwającej klatki (rolowany w BeginFrame)
    };

    // Globalny rejestr "gorących" assetów renderowania: które meshe i tekstury są najczęściej
    // używane w głównym passie sceny. Plain singleton (nie EngineObject), tak jak Profiler.
    //
    // Bez synchronizacji: i zapis (RenderSnapshotBuilder), i odczyt (panel edytora) dzieją się na
    // wątku MAIN, więc dostęp jest z natury sekwencyjny. Rejestr celowo NIE liczy tekstur cieni
    // (mapy kaskad CSM) — do liczników trafiają wyłącznie tekstury materiałów, a mapy cieni są
    // ustawiane silnikowo w Rendererze, poza materiałem.
    class PLURENDER_API RenderUsageStats {
    public:
        static RenderUsageStats* GetInstance();

        // Zamyka poprzednią klatkę (CurrentFrameUses -> LastFrameUses) i zeruje akumulator.
        // Woływane raz na klatkę, na początku budowania snapshotu.
        void BeginFrame();

        // count > 1 pozwala zapisać N użyć jednym wywołaniem (agregacja per unikalny asset
        // w RenderSnapshotBuilderze zamiast wywołania per komponent).
        void RecordMesh(UInt64 meshUuid, UInt32 count = 1);
        void RecordSkeletalMesh(UInt64 meshUuid, UInt32 count = 1);
        void RecordTexture(UInt64 textureUuid, UInt32 count = 1);

        // Bezpośredni dostęp do rejestrów (odczyt na tym samym wątku co zapis — MAIN).
        const HashMap<UInt64, AssetUsageEntry>& GetMeshUsage() const { return mMeshes; }
        const HashMap<UInt64, AssetUsageEntry>& GetSkeletalMeshUsage() const { return mSkeletalMeshes; }
        const HashMap<UInt64, AssetUsageEntry>& GetTextureUsage() const { return mTextures; }

        // Zeruje wszystkie zebrane liczniki.
        void Clear();

    private:
        RenderUsageStats() = default;

        HashMap<UInt64, AssetUsageEntry> mMeshes;
        HashMap<UInt64, AssetUsageEntry> mSkeletalMeshes;
        HashMap<UInt64, AssetUsageEntry> mTextures;
    };


    // Shadow and spot-light frame counters, published by the render thread at the end of a
    // frame and read from editor panels on the main thread. They live here rather than in
    // PluUtils because their arrays are sized by kMaxShadowCascades / kMaxSpotShadowSlots,
    // which are renderer limits — core has no business knowing them.
    // --- Directional shadow stats ----------------------------------------------------------
    // Same mirror pattern, per cascade: how many casters (static instances + skeletal meshes)
    // actually survived culling into each cascade's depth map, and how many cascades are live
    // this frame (0 = no directional shadows). Published by Renderer::RenderSnapshot.
    PLURENDER_API void SetShadowCascadeStats(const UInt32* casterCounts, UInt32 cascadeCount);

    PLU_FUNCTION()
    PLURENDER_API UInt32 GetStatShadowCascadeCount();
    PLU_FUNCTION()
    PLURENDER_API UInt32 GetStatShadowCascadeCasters(UInt32 cascadeIndex);

    // --- Spot light stats -------------------------------------------------------------------
    // Same mirror pattern again. visibleLights is how many spot lights survived the camera
    // frustum cull on MAIN this frame; casterCounts/slotCount describe the shadow atlas, i.e.
    // how many of those lights actually won a slot and how many casters each slot drew.
    // A light being visible but slotless is normal — it lights the scene without occluding.
    PLURENDER_API void SetSpotLightStats(const UInt32* casterCounts, UInt32 slotCount, UInt32 visibleLights);

    PLU_FUNCTION()
    PLURENDER_API UInt32 GetStatVisibleSpotLights();
    PLU_FUNCTION()
    PLURENDER_API UInt32 GetStatSpotShadowSlots();
    PLU_FUNCTION()
    PLURENDER_API UInt32 GetStatSpotShadowCasters(UInt32 slotIndex);
}

#endif //PLUENGINE_RENDERUSAGESTATS_H
