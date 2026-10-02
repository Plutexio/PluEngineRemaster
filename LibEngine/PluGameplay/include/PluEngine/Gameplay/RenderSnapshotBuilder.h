//
// Created by Plutex on 6/21/26.
//

#ifndef PLUENGINE_RENDERSNAPSHOTBUILDER_H
#define PLUENGINE_RENDERSNAPSHOTBUILDER_H

#include "PluEngine/Core/Objects/EngineObject.h"
#include "RenderSnapshotBuilder.generated.h"
#include "PluEngine/PluTypes.h"
#include "PluEngine/Render/RenderThreading.h"
#include "PluEngine/Core/Threading/TripleBuffer.h"

namespace Plu
{
    class CameraComponent;
    class IRendererCamera;
    class GameObject;
    class SceneWorld;
    class SkeletalMeshComponent;
    struct RenderSnapshot;


    PLU_CLASS()
    class PLUGAMEPLAY_API RenderSnapshotBuilder : public EngineObject
    {
        REFLECTION_BODY_RENDERSNAPSHOTBUILDER()
    private:
        TripleBuffer<RenderSnapshot*>* mTripleBuffer;
        ApplicationInfo* mAppInfo;

        // Batching instancingu static meshy: hash klucza (MeshUUID, MaterialUUID, CastsShadow) ->
        // indeksy w RenderSnapshot::StaticMeshBatches. Member (nie lokalna per klatka) — Clear()
        // na starcie każdego builda, reużywany bufor zamiast realokacji co klatkę. Kolizje: małe
        // kubełki porównywane po pełnym kluczu w BuildSnapshotAndPublish.
        HashMap<UInt64, DynamicArray<UInt32>> mBatchLookup;

        // Scratch dla dwuprzebiegowej emisji batchy (bucketing -> prefix-sum + sort batchy ->
        // scatter). Member, reużywany między klatkami — unika alokacji DynamicArray per batch.
        struct StaticMeshBatchScratchEntry
        {
            UInt32 BatchIndex;
            InstanceGPUData Instance;
            InstanceCullData Bounds;
        };
        DynamicArray<StaticMeshBatchScratchEntry> mBatchScratch;

        // Deduplikacja "ensure loaded" per klatka: IsAssetLoaded bierze shared_lock na mutexie
        // AssetManagera, więc wołanie go per komponent (2x per StaticMeshComponent) przy tysiącach
        // komponentów to tysiące locków na klatkę o identycznym wyniku. Sprawdzamy raz per unikalny
        // UUID; Clear() na starcie builda, member dla reużycia pojemności.
        HashSet<UInt64> mEnsuredAssets;

#ifdef PLU_ENGINE_EDITOR_BUILD
        // Akumulatory liczników "hottest" assetów (RenderUsageStats) per klatka: UUID -> liczba
        // użyć. Flush raz po pętlach batchowania — per-komponentowe chodzenie po uniformach
        // materiału (porównania stringów "sampler2D") było O(komponenty x uniformy) na klatkę.
        HashMap<UInt64, UInt32> mFrameMeshUses;
        HashMap<UInt64, UInt32> mFrameMaterialUses;
#endif

        [[nodiscard]] Matrix4 GetProjectionMatrix(IRendererCamera* camera) const;
        [[nodiscard]] Matrix4 GetViewMatrix(IRendererCamera* camera) const;

        // Frame phase that has to complete before ANY renderable is collected: builds every skeletal
        // pose for this frame and snaps skeletal-attached GameObjects onto it. See the definitions.
        void EvaluateSkeletalPosesAndAttachments(const TUsePointer<SceneWorld>& sceneWorld, float deltaTime);
        void EvaluateSkeletalPose(SkeletalMeshComponent* component, float deltaTime);
        [[nodiscard]] static UInt32 GetAttachmentDepth(const GameObject* object);
    public:
        RenderSnapshotBuilder();
        RenderSnapshotBuilder(TripleBuffer<RenderSnapshot*>* tripleBuffer, ApplicationInfo* applicationInfo);
        virtual ~RenderSnapshotBuilder() override;

        static Matrix4 GetLastFrameProjectionMatrix();
        static Matrix4 GetLastFrameViewMatrix();

        void BuildSnapshotAndPublish(float deltaTime = 0.0f);
        // For a frame that ticked but publishes no snapshot (power saving probe frame). Drops
        // the per-frame debug geometry the tick queued for BuildSnapshotAndPublish to drain —
        // left alone it would pile up, one frame's worth per skipped frame.
        void DiscardFrame();
    };
}

#endif //PLUENGINE_RENDERSNAPSHOTBUILDER_H
