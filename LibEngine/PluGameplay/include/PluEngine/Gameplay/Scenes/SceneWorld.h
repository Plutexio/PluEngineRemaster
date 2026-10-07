//
// Created by Plutex on 5/29/26.
//

#ifndef PLUENGINE_SCENEWORLD_H
#define PLUENGINE_SCENEWORLD_H
#include "PluEngine/Core.h"
#include "PluEngine/Core/Objects/EngineObject.h"
#include "SceneWorld.generated.h"
#include "PluEngine/Gameplay/Controller.h"
#include "PluEngine/Gameplay/GameMode.h"
#include "PluEngine/Gameplay/RenderSnapshotBuilder.h"
#include "PluEngine/Gameplay/RaycastInfo.h"
#include "PluEngine/Gameplay/Debug/DebugDrawAdapter.h"

namespace Plu
{
	class ParticleSpawnerComponent;
	class CameraComponent;
	class DirectionalLight;
	class SpotLight;
	class StaticMeshComponent;
	class InstancedStaticMeshComponent;
	class SkeletalMeshComponent;
	class IRenderable;
	struct SceneInfo;
	class GameClient;
	class Renderer;
	class PhysicsWorld;

	PLU_CLASS(PyExport)
	class PLUGAMEPLAY_API SceneWorld final : public EngineObject
	{
		REFLECTION_BODY_SCENEWORLD()
	protected:
		HashMap<UInt64, TOwningPointer<GameObject>> mGameObjects;
		HashMap<UInt16, TUsePointer<Controller>> mControllers;
		DynamicArray<TUsePointer<GameObject>> mObjectsToBegin;
		// Objects spawned while the tick loop below is running. Inserting into mGameObjects
		// mid-iteration rehashes the map and invalidates the iterator, so they wait here until the
		// loop ends. Empty outside of the tick loop.
		DynamicArray<TOwningPointer<GameObject>> mPendingSpawns;
		bool mTickingGameObjects = false;
		DynamicArray<std::pair<TUsePointer<GameObject>, bool>> mObjectsToDestroy;

		TUsePointer<EngineObjectManager> mEngineObjectManager;
		TUsePointer<GameClient> mClient;
		TOwningPointer<PhysicsWorld> mPhysicsWorld;

		TUsePointer<GameMode> mGameMode;

		//Renderables
		HashMap<UInt64, DynamicArray<TOwningPointer<StaticMeshComponent>>> mStaticMeshRenderables;
		HashMap<UInt64, DynamicArray<TOwningPointer<InstancedStaticMeshComponent>>> mInstancedMeshRenderables;
		HashMap<UInt64, DynamicArray<TOwningPointer<SkeletalMeshComponent>>> mSkeletalMeshRenderables;
		TOwningPointer<DirectionalLight> mDirectionalLight;
		// Keyed by object UUID, like the renderable maps above. Unlike mDirectionalLight there is
		// no uniqueness assert — a scene may hold any number of spot lights; the shadow-slot
		// budget is resolved per frame on the render thread, not by limiting how many can exist.
		HashMap<UInt64, TOwningPointer<SpotLight>> mSpotLights;

		bool mIsPlaying = false;
		bool mNewGameObjectSpawned = false;

		// Per-world cache for GetAllGameObjectsOfClass. Was a function-local static
		// (globally shared across worlds + not thread-safe); kept per-world here.
		HashMap<String, DynamicArray<TUsePointer<GameObject>>> mGameObjectsPerClassCache;

		// Live particle spawner components by UUID. RenderSnapshotBuilder packs the full state of
		// each into every snapshot; the render thread creates/destroys its spawners to match.
		HashMap<UInt64, TOwningPointer<ParticleSpawnerComponent>> mParticleSpawnerComponents;

		//Debug
		DynamicArray<float> mDebugLineVerts;   // GL_LINES,  6 floatów / wierzchołek
		DynamicArray<float> mDebugPointVerts;  // GL_POINTS, 6 floatów / wierzchołek
		float mDebugPointSize = 10.0f;
		// Every debug draw goes through this adapter into the two buffers above. Declared after
		// them, so it binds to fully constructed arrays.
		DebugDrawAdapter mDebugDraw{&mDebugLineVerts, &mDebugPointVerts};

		friend void Controller::Possess(TUsePointer<Puppet> puppet);
		friend void Controller::Unpossess();
		// Whole class rather than just BuildSnapshotAndPublish: the builder walks these containers
		// from several frame phases now (pose evaluation and attachment resolution run before any
		// renderable is collected), and naming each one here would break on every refactor.
		friend class RenderSnapshotBuilder;

		// Wspólne ciało dla SpawnGameObject / SpawnGameObjectUnnamed / SpawnGameObjectWithUuid.
		// explicitUuid == nullptr -> świeży losowy UUID.
		TUsePointer<GameObject> SpawnGameObjectInternal(TClassPointer<GameObject> objectClass, bool generateDefaultName, const PluUUID* explicitUuid = nullptr);
	public:
		SceneWorld() = default;
		virtual ~SceneWorld() override;

		TUsePointer<SceneInfo> Info;

		// Editor grid toggle (like PhysicsWorld::PhysicsDebugRenderMode: written by editor UI,
		// read on MAIN by RenderSnapshotBuilder). View-only state — not serialized, does not
		// dirty the scene asset. Cell size is fixed at 1 m (EditorGrid.frag, engine scale).
		bool ShowEditorGrid = true;

		// Shadow cascade debug tint (same kind of view-only state as ShowEditorGrid: written by
		// editor UI, read on MAIN by RenderSnapshotBuilder, not serialized). Reaches the shaders
		// as ShadowData::DebugVisualizeCascades.
		bool ShowShadowCascades = false;

		// Live particle spawner components (main thread). Their particles are simulated on the
		// render thread — read those through GetParticleDebugStats (RenderParticleStats.h).
		[[nodiscard]] const HashMap<UInt64, TOwningPointer<ParticleSpawnerComponent>>& GetParticleSpawnerComponents() const { return mParticleSpawnerComponents; }

		// Reads the render thread's particle liveness ONCE for the whole world (not per component) and
		// applies it: latches each spawner as seen, mirrors its alive count, and deletes objects whose
		// AutoDestroyWhenFinished run completed. Runs every frame right before the snapshot is built.
		void UpdateParticleLiveness();

		// Debug drawing for this world (lines, shapes, raycasts; per frame or with a duration).
		// Main thread only. Editor gizmos draw through it too: the editor's OnTick runs before
		// BuildSnapshotAndPublish (Application.cpp), so what a panel draws this frame is already
		// in the buffers when the builder drains them.
		PLU_FUNCTION(PyExport)
		DebugDrawAdapter* GetDebugDraw() { return &mDebugDraw; }

		PLU_PROPERTY()
		TClassPointer<GameMode> GameModeClass = TClassPointer<GameMode>(GameMode::GetStaticClass());

		void Init(const TUsePointer<EngineObjectManager> &engineObjectManager, const TUsePointer<GameClient>& client);

		void LoadGameObjects();
		void UnloadGameObjects();
		void Play();

		void HandleBeginPlay();
		void HandleDestroy();
		// Moves objects spawned during the tick loop into mGameObjects. Until this runs they are
		// not visible to GetGameObjectByUUID / GetAllGameObjects*.
		void FlushPendingSpawns();

		PLU_FUNCTION(PyExport)
		TUsePointer<Controller> GetControllerByID(UInt16 playerID);

		void TickScene(float deltaTime);

		void NewGameObjectComponent(const TOwningPointer<GameObjectComponent>& component);
		void DeleteGameObjectComponent(const TOwningPointer<GameObjectComponent>& component);

		// Called when a game object's scale changes. While playing, this rebuilds the object's
		// physics body so its colliders match the new scale (Jolt shapes can't be scaled in place).
		void OnGameObjectScaleChanged(GameObject* gameObject);

		// Called when a world component's relative transform changes. While playing, the owning
		// object's body is queued for a rebuild — sub-shape offsets are baked into the compound
		// shape, so moving a collider component otherwise has no effect on physics.
		void OnComponentTransformChanged(GameObject* gameObject);

		PLU_FUNCTION(PyExport)
		TUsePointer<GameObject> SpawnGameObject(TClassPointer<GameObject> objectClass);

		/**
		 * Jak `SpawnGameObject`, ale **bez** nadawania domyślnej nazwy — obiekt wychodzi stąd
		 * z pustym `mObjectName` i wołający musi ją ustawić sam.
		 *
		 * Dla wczytywania sceny: `MakeDefaultObjectName` przechodzi po wszystkich obiektach sceny,
		 * więc spawn tysiąca obiektów to O(n^2), a przy wczytywaniu z JSON-a wynik i tak ląduje
		 * w koszu — `mObjectName` jest `PLU_PROPERTY` i deserializacja nadpisuje go milisekundę
		 * później. Na scenie ~1000 obiektów to było ~45 ms na każde wczytanie (i na każde wejście
		 * w PIE). Używaj **tylko** wtedy, gdy zaraz po spawnie nadajesz nazwę.
		 */
		TUsePointer<GameObject> SpawnGameObjectUnnamed(TClassPointer<GameObject> objectClass);

		/**
		 * Jak `SpawnGameObjectUnnamed`, ale obiekt dostaje **podany** UUID zamiast losowego.
		 *
		 * Dla ścieżek, które odtwarzają obiekt zachowując jego tożsamość (wczytywanie sceny z JSON-a,
		 * hot reload skryptów Pythona): UUID jest kluczem w `mGameObjects`, w mapach renderable'i
		 * i w attachmentach zapisanych jako `parentUuid`, więc świeży UUID zerwałby wszystkie te
		 * powiązania. UUID musi być nadany **przed** `OnSetupComponents`, dlatego nie da się tego
		 * zrobić z zewnątrz po spawnie.
		 *
		 * Gdy UUID jest już zajęty w tej scenie, obiekt dostaje losowy (i leci warning).
		 */
		TUsePointer<GameObject> SpawnGameObjectWithUuid(TClassPointer<GameObject> objectClass, PluUUID uuid);

		void DeleteGameObject(EngineObjectHandle gameObject, bool callEndPlay = true);

		/**
		 * Natychmiast wykonuje odroczone kasowanie (`DeleteGameObject` tylko kolejkuje, kolejka jest
		 * przetwarzana na końcu `TickScene`).
		 *
		 * Potrzebne, gdy obiekt trzeba skasować i **odtworzyć z tym samym UUID** w jednej operacji
		 * (hot reload skryptów): dopóki stary obiekt siedzi w `mGameObjects` pod tym UUID, nowy nie ma
		 * gdzie wejść, a późniejsze `HandleDestroy` wyrzuciłoby z mapy właśnie ten nowy.
		 * Nie wołaj z wnętrza ticka — kolejka jest tam przetwarzana sama.
		 */
		void FlushPendingDestroys();

		PLU_FUNCTION(PyExport)
		void DestroyGameObject(GameObject* gameObject);

		DynamicArray<TUsePointer<GameObject>> GetAllGameObjects();
		void GetFormattedGameObjectNames(DynamicArray<String>* result);

		/**
		 * Domyślna nazwa dla nowego obiektu: `TypeName` + **najniższy wolny** indeks w tej scenie
		 * (np. `StaticMeshActor0`, potem `StaticMeshActor1`). Numeracja jest lokalna dla sceny i
		 * liczona od stanu faktycznego, więc kasowanie obiektów zwalnia numerki zamiast je zawyżać.
		 */
		String MakeDefaultObjectName(TClassPointer<GameObject> objectClass);
		/**
		 * To samo, ale dla dowolnego prefiksu zamiast `TypeName` — dla obiektów, które mają już
		 * nadaną ręcznie nazwę (duplikat `Tree3` dostaje `Tree4`, a nie `StaticMeshActor7`).
		 */
		String MakeDefaultObjectNameFromBase(const String& base);
		/** Czy jakiś obiekt w scenie (łącznie z pending spawns) nosi już taką nazwę. */
		[[nodiscard]] bool IsObjectNameTaken(const String& name) const;
		TUsePointer<GameObject> GetGameObjectByUUID(PluUUID uuid);

		//Getters
		PLU_FUNCTION(PyExport)
		TUsePointer<GameObject> GetGameObjectOfClass(TClassPointer<GameObject> gameObjectClass);
		PLU_FUNCTION(PyExport)
		DynamicArray<TUsePointer<GameObject>> GetAllGameObjectsOfClass(TClassPointer<GameObject> gameObjectClass);

		// Casts a ray against this world's physics bodies and reports the closest hit. Answered by
		// the physics world through the "Raycast" event; without one (no physics module) it never hits.
		// Bodies only exist once the physics tick has built them, so objects spawned this frame are
		// not hit yet. Start/End are world-space, in metres. The bodies of IgnoredObjects are skipped
		// (null entries are fine) — pass the caster itself when the ray starts inside its own collider.
		PLU_FUNCTION(PyExport)
		RaycastHitInfo ShootRaycast(const Vec3& Start, const Vec3& End, const DynamicArray<GameObject*>& IgnoredObjects = DynamicArray<GameObject*>{});
		// Same as ShootRaycast, with the end point at Start + normalize(Direction) * Length.
		PLU_FUNCTION(PyExport)
		RaycastHitInfo ShootRaycastInDirection(const Vec3& Start, const Vec3& Direction, float Length, const DynamicArray<GameObject*>& IgnoredObjects = DynamicArray<GameObject*>{});

		void JoinPlayerLocally(UInt16 playerID);
	};
}

#endif //PLUENGINE_SCENEWORLD_H
