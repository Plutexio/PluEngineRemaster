//
// Created by Plutex on 1/14/26.
//

#ifndef PLUENGINE_SCENESTRUCTUREPANEL_H
#define PLUENGINE_SCENESTRUCTUREPANEL_H
#include "EditorViewports/IEditorPanel.h"
#include "PluEngine/PluTypes.h"
#include "PluEngine/Core/Events/EventDispatcher.h"
#include "PluEngine/Core/Objects/EngineObjectHandle.h"
#include "PluSTL/Array/Array.h"
#include "SceneStructurePanel.generated.h"

namespace Plu
{
	class GameObject;
	class SceneWorld;

	// ImGui drag&drop payload ids shared by the scene panels — the drags cross panels: an object row
	// dragged out of this panel is dropped on a component row in the Inspector to attach the whole
	// object to that component (GameObject::AttachToComponent — a rifle onto a character's camera
	// component, say). Both payloads carry a raw EngineObjectHandle (POD: index + generation), never
	// a pointer, so a handle whose object died mid-drag simply stops resolving.
	inline constexpr const char* CSceneObjectDragPayload = "PLU_STRUCTURE_OBJECT";
	inline constexpr const char* CSceneComponentDragPayload = "PLU_WORLD_COMPONENT";

	PLU_CLASS()
	class SceneStructurePanel : public IEditorPanel
	{
		REFLECTION_BODY_SCENESTRUCTUREPANEL()
	public:
		SceneStructurePanel() = default;
		// Subskrypcja łapie `this`, więc musi zejść razem z panelem — nie tylko w OnClosed.
		~SceneStructurePanel() override;

		String GetPanelName() override;
		void OnClosed() override;
		void OnOpened() override;
		void OnUpdate(float deltaTime) override;

		/** Kasuje całe zaznaczenie panelu (multi-selection) i zeruje primary.
		 *  Woła to SceneViewport spod klawisza Delete — multi-selection żyje tylko tutaj. */
		void DeleteSelectedObjects();

	private:
		/** Ustawienia popupu "Randomize Transforms". */
		struct RandomizeTransformsSettings
		{
			bool  RandomizeLocation = true;
			Vec3  LocationMin       = Vec3(-5.0f, 0.0f, -5.0f);
			Vec3  LocationMax       = Vec3(5.0f, 0.0f, 5.0f);
			bool  LocationRelative  = false;   // true = offset względem obecnej lokacji

			bool  RandomizeRotation = false;
			Vec3  RotationMin       = Vec3(0.0f);
			Vec3  RotationMax       = Vec3(0.0f, 360.0f, 0.0f);

			bool  RandomizeScale    = false;
			bool  UniformScale      = true;
			float UniformScaleMin   = 0.8f;
			float UniformScaleMax   = 1.2f;
			Vec3  ScaleMin          = Vec3(0.8f);
			Vec3  ScaleMax          = Vec3(1.2f);

			bool  UseFixedSeed      = false;
			int   Seed              = 1234;
		};

		/** Czy handle jest w zaznaczeniu; `-1` gdy nie ma. */
		[[nodiscard]] Int64 FindInSelection(const EngineObjectHandle& handle) const;
		[[nodiscard]] bool  IsSelected(const EngineObjectHandle& handle) const;

		/** Wyrzuca z zaznaczenia handle nieżywych obiektów i podchwytuje zaznaczenie zrobione poza panelem. */
		void SyncSelection(const DynamicArray<TUsePointer<GameObject>>& objects);

		void SelectSingle(const EngineObjectHandle& handle, Int64 index);
		void ToggleSelection(const EngineObjectHandle& handle, Int64 index);
		void SelectRangeTo(const DynamicArray<TUsePointer<GameObject>>& objects, Int64 index);

		/** Primary selection = to, co widzi reszta edytora (details panel, gizmo). */
		void SetPrimarySelection(const EngineObjectHandle& handle);

		void DrawRandomizeWindow();
		void ApplyRandomTransforms();

		/** Przepina subskrypcję "GameObjectsChanged" na podany świat (PIE/overlay zmieniają świat). */
		void EnsureSubscribedTo(const TUsePointer<SceneWorld>& world);
		void UnsubscribeFromWorld();
		/** Odbudowuje i sortuje `mListObjects`/`mListNames`. Woła się tylko gdy `mListDirty`. */
		void RefreshObjectList(const TUsePointer<SceneWorld>& world);

		/** Wchodzi w tryb edycji nazwy wiersza (podwójny klik albo "Rename" z menu). */
		void BeginRename(const EngineObjectHandle& handle, const String& currentName);
		/** Zatwierdza nazwę z bufora. Pustą i niezmienioną ignoruje — dzięki temu Esc
		 *  (ImGui przywraca wtedy starą wartość) nie brudzi assetu. */
		void CommitRename(const TUsePointer<GameObject>& object);

		/** Posortowana lista wierszy — budowana tylko przy zmianie sceny, nie co klatkę.
		 *  Unieważnia ją event "GameObjectsChanged" (spawn/destroy, zmiana nazwy) i zmiana świata. */
		DynamicArray<TUsePointer<GameObject>> mListObjects;
		DynamicArray<String>                  mListNames;
		/** Attachment depth per row — drives the indent, and rows are ordered parent-first. */
		DynamicArray<UInt32>                  mListDepths;
		bool                                  mListDirty = true;
		/** Świat, na którym wisi subskrypcja — trzymany, żeby dało się ją zdjąć. */
		TUsePointer<SceneWorld>               mSubscribedWorld;
		EventHandle                           mGameObjectsChangedHandle = 0;

		DynamicArray<EngineObjectHandle> mSelectedObjects;
		EngineObjectHandle               mRenamingObject;
		bool                             mRenameFocusPending   = false;
		char                             mRenameBuffer[128]    = {};
		Int64                            mSelectionAnchor      = -1;
		bool                             mShowRandomizeWindow  = false;
		/** Stan zadokowania z poprzedniej klatki — obwódkę trzeba wypchnąć przed Begin(). */
		bool                             mRandomizeWindowDocked = false;
		RandomizeTransformsSettings      mRandomizeSettings;
	};
}

#endif //PLUENGINE_STATICMESHDETAILSPANEL_H
