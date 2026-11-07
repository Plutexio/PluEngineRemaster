//
// Created by Plutex on 1/13/26.
//

#ifndef PLUENGINE_SCENEVIEWPORT_H
#define PLUENGINE_SCENEVIEWPORT_H
#include "EditorViewports/IEditorViewport.h"
#include "SceneViewport.generated.h"

namespace Plu
{
	class SceneWorld;

	PLU_CLASS()
	class SceneViewport : public IEditorViewport
	{
		REFLECTION_BODY_SCENEVIEWPORT()
	private:
		// The world the "GameObjectsChanged" subscription hangs on. Kept explicitly instead of asking
		// the scene manager again at unsubscribe time: the world gets swapped under the viewport
		// (opening another scene reuses this viewport), and unsubscribing from the new world would
		// leave the handler on the old one.
		TUsePointer<SceneWorld> mSubscribedWorld;
		EventHandle mGameObjectsChangedHandle = 0;
		EventHandle mNewPythonTypeHandle;
		EventHandle mNewWorldHandle;

		// Set while the viewport itself is rearranging the world (python hot reload) — those spawns and
		// destroys are not user edits, so they must not dirty the scene asset.
		bool mSuppressDirtyFromWorld = false;

		void SubscribeToCurrentWorld();
		void UnsubscribeFromWorld();

		// Python classes re-registered since the last frame (script hot reload). Collected instead of
		// acted upon right away: "NewPythonType" is dispatched from RegisterPluClass, i.e. in the middle
		// of RunProjectScripts, when the other project modules are already popped out of sys.modules and
		// not re-imported yet — constructing a python object there can raise ImportError, and every
		// class of the batch would kick off its own full recreate pass.
		DynamicArray<String> mPendingPythonTypeReloads;

		void FlushPendingPythonTypeReloads();

		// Appends the inner + outer cone wireframes of the selected SpotLight to the world's
		// per-frame editor debug-line channel. Lives here rather than in SceneViewportPanel
		// because the panel is drawn once per hosting window, and the cone must be appended
		// exactly once per frame or it would stack up in the buffer.
		void DrawSelectedSpotLightGizmo();
		// Launch cone of every ParticleSpawnerComponent on the selected object, into the same
		// channel and for the same once-per-frame reason as DrawSelectedSpotLightGizmo.
		void DrawSelectedParticleSpawnerGizmos();
	public:
		SceneViewport() = default;
		virtual ~SceneViewport() override = default;

		bool UsesEditorCamera() const override { return true; }

		void OnInit() override;
		void OnClosed() override;
		void OnOpened() override;
		void OnPanelRegister() override;
		void OnUpdate(float deltaTime) override;
	};
}

#endif //PLUENGINE_SCENEVIEWPORT_H