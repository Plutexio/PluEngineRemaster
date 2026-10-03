//
// Created by Plutex on 5/29/26.
//

#ifndef PLUENGINE_SCENEMANAGER_H
#define PLUENGINE_SCENEMANAGER_H
#include "PluEngine/Core.h"
#include "PluEngine/Core/Objects/EngineObject.h"
#include "SceneManager.generated.h"
#include "PluEngine/PluTypes.h"
#include "PluEngine/Core/Reflection/ClassPointer.h"

namespace Plu
{
    class IModifiableCamera;
    class GameObject;
    class WorldComponent;
    class GameClient;
    class IRendererCamera;
    struct SceneInfo;
    class SceneWorld;
    // Defined in WorldComponent.h; only the rule value travels through PendingAttachment, so the
    // whole component header does not have to be pulled in here.
    enum class EAttachmentRule : UInt8;

    PLU_CLASS()
    class PLUGAMEPLAY_API SceneManager : public EngineObject
    {
        REFLECTION_BODY_SCENEMANAGER()
    private:
        TOwningPointer<SceneWorld> mActiveScene;
#ifdef PLU_ENGINE_EDITOR_BUILD
        TOwningPointer<SceneWorld> mOverlayScene;
        TOwningPointer<SceneWorld> mActivePIEScene;

        bool mIsInPIE = false;

        // Kamera, z której renderuje się viewport edytora poza PIE. Dawniej przekazywana do
        // Renderer::SetCamera; w modelu snapshot RenderSnapshotBuilder pobiera ją stąd, gdy
        // nie ma kontrolera/pucharka. Nie-właścicielski wskaźnik (właścicielem jest edytor).
        IRendererCamera* mEditorCamera = nullptr;
#endif

        HashMap<String, TUsePointer<SceneInfo>> mRegisteredScenesByURL;

        //Helpers
        void UnloadScene(TUsePointer<SceneWorld> sceneWorld);
        // cloneSource, when given, populates the new world by duplicating that world's objects in
        // memory instead of reading the scene asset off disk — that is how PIE starts from the
        // editor world's live state.
        void LoadScene(String url, TOwningPointer<SceneWorld>* field, bool play = true, TUsePointer<SceneWorld> cloneSource = nullptr);

        TUsePointer<EngineObjectManager> mObjectManager;
        TUsePointer<GameClient> mClient;
        TUsePointer<EngineAssetManager> mAssetManager;
        TUsePointer<IShaderManager> mShaderManager;

        // One context is built per scene load and threaded through every object/component below it —
        // it holds nothing per-object, and allocating one per component was pure overhead.
        DeserializationContext MakeDeserializationContext();

        // An attachment read out of a scene JSON that cannot be applied yet: the parent object may
        // still be further down the "gameObjects" array. Collected during the load and applied by
        // ResolvePendingAttachments once every object of the batch exists.
        struct PendingAttachment
        {
            TUsePointer<GameObject> Object;
            UInt64 ParentUuid = 0;
            String ParentComponentName; // empty -> attached to the parent object's own transform
            String SocketName;
            // KeepRelative for a load: the stored transform is already the offset from the parent, so
            // re-deriving it would move the object. The hot-reload path overrides it — see
            // CapturePythonReloadRiders. Value is EAttachmentRule::KeepRelative, spelled out in the
            // .cpp where the enum is complete.
            EAttachmentRule Rule;
            PendingAttachment();
        };
        DynamicArray<PendingAttachment> mPendingAttachments;
        void ResolvePendingAttachments(const TUsePointer<SceneWorld>& sceneWorld);

        void DeserializeWorldComponent(DeserializationContext* dc, const JSON& j, TUsePointer<WorldComponent> parentComponent, TUsePointer<GameObject> parentObject);
        void LoadGameObjectFromJSON(DeserializationContext* dc, TUsePointer<SceneWorld> sceneWorld, const JSON& j);
        void LoadSceneFromFile(TUsePointer<SceneWorld> sceneWorld);
        void LoadSceneFromJson(TUsePointer<SceneWorld> sceneWorld, const JSON& j);

        // In-memory duplication of a live world, used instead of a serialize/deserialize round-trip.
        // Mirrors the JSON path step for step (spawn, copy properties, match components by name,
        // recurse into the component tree) so a cloned world and a loaded one come out the same.
        void CloneSceneInto(TUsePointer<SceneWorld> target, TUsePointer<SceneWorld> source);
        // Returns the clone so CloneSceneInto can map source -> clone and rebuild attachments in a
        // second pass (a parent may be cloned after its child, and clones get fresh UUIDs anyway).
        TUsePointer<GameObject> CloneGameObjectInto(TUsePointer<SceneWorld> targetWorld, TUsePointer<GameObject> source);
        void CloneWorldComponent(TUsePointer<WorldComponent> source, TUsePointer<WorldComponent> parentComponent, TUsePointer<GameObject> targetObject);

#ifdef PLU_ENGINE_EDITOR_BUILD
        // Objects riding something that ReloadPythonInstances is about to destroy: they survive the
        // reload, but GameObject::Cleanup detaches them (KeepWorld) on the way out, so the attachment
        // has to be re-applied afterwards. Queued into mPendingAttachments with KeepWorld — the
        // riders' transforms are world values by then, and the recreated parent lands on the same
        // transform, so the original offset comes back out of the recomputation.
        void CapturePythonReloadRiders(const TUsePointer<GameObject>& parent, const HashSet<UInt64>& victimUuids);
        // In-place swap of every component of one of the reloaded python classes on an object that
        // itself survived. Returns how many components were recreated.
        UInt32 ReloadPythonComponents(DeserializationContext* dc, const TUsePointer<GameObject>& object, const DynamicArray<String>& componentClasses);
#endif
    public:
        SceneManager();
        virtual ~SceneManager() override;

        //Lifetime
        void Initialize(ApplicationInfo* appInfo);
        void OnUpdate(float deltaTime);

        //Getters
        String GetCurrentWorldName();
        TUsePointer<SceneWorld> GetCurrentWorld();

        //Utils
        void DisconnectFromWorld();
        bool ConnectToWorld(String URL, bool startPlayOnLoad = true); //URL can be SceneName or IP address
        bool IsAnySceneOpen();
        void RegisterSceneInfo(TUsePointer<SceneInfo> sceneInfo);
        void LoadGameObjectFromJSON(TUsePointer<SceneWorld> sceneWorld, JSON j);

#ifdef PLU_ENGINE_EDITOR_BUILD
        // Full JSON of the active scene. base carries any keys of the scene asset we do not own and
        // must not drop (SaveActiveScene passes the file's current contents); PIE passes an empty
        // one, since it only ever reads back the keys written here.
        JSON SerializeActiveScene(JSON base);
        void SaveActiveScene();

        void CreateOverlayScene();
        void UnloadOverlayScene();

        bool EnterPIE();
        void ExitPIE();

        bool IsInPIE() const;
        TUsePointer<SceneWorld> GetBaseSceneWorld();

        /**
         * Python hot reload: recreates every live instance of the given python classes from the
         * freshly reloaded class objects, so edited `OnUpdate` / `OnBeginPlay` code starts running on
         * objects that are already in the scene.
         *
         * A `GameObject` class is recreated whole (serialize → destroy → spawn with the same UUID),
         * a `GameObjectComponent` class is swapped in place on its owner. Preserved: reflected
         * (`PLU_PROPERTY`) values, object name, transform, UUID and attachments in both directions.
         * **Not** preserved: python instance attributes (`self.x = ...`), which the new `__init__`
         * creates from scratch.
         *
         * Refuses to run during PIE — possession, physics state and timers are not reconstructible
         * from reflected properties. Operates on the edit world only.
         */
        void ReloadPythonInstances(const DynamicArray<String>& typeNames);

        void SetEditorRenderCamera(IRendererCamera* camera);
        IRendererCamera* GetEditorRenderCamera() const;
#endif
    };

    PLU_FUNCTION()
    PLUGAMEPLAY_API TUsePointer<SceneWorld> GetCurrentWorld();

    PLU_FUNCTION()
    PLUGAMEPLAY_API void ConnectToWorld(const String &URL);
}

#endif //PLUENGINE_SCENEMANAGER_H
