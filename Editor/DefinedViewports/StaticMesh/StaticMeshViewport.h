//
// Created by Plutex on 1/13/26.
//

#ifndef PLUENGINE_STATICMESHVIEWPORT_H
#define PLUENGINE_STATICMESHVIEWPORT_H
#include "EditorViewports/IEditorViewport.h"
#include "StaticMeshViewport.generated.h"
#include "PluEngine/Gameplay/Components/StaticMeshComponent.h"
#include "PluEngine/Gameplay/GameObject.h"

namespace Plu
{
	class PhysicsBodyComponent;
	PLU_CLASS()
	class EditorMeshObject : public GameObject
	{
		REFLECTION_BODY_EDITORMESHOBJECT()
	public:
		EditorMeshObject() = default;
		virtual ~EditorMeshObject() override = default;

		void OnSetupComponents() override;

		TUsePointer<StaticMeshComponent> MeshComponent;
		TUsePointer<PhysicsBodyComponent> BodyComponent;
	};

	struct MaterialInfo;
	PLU_CLASS()
	class StaticMeshViewport : public IEditorViewport
	{
		REFLECTION_BODY_STATICMESHVIEWPORT()
	private:
	public:
		StaticMeshViewport() = default;
		virtual ~StaticMeshViewport() override = default;

		TUsePointer<MaterialInfo> Material;
		bool ShowCollision = false;
		bool CollisionDirty = false;

		// Set true to (re)fit the shared editor camera to the mesh bounds on the viewport panel's
		// next update (on open, or when the user clicks "Frame"). Stays pending until the mesh has
		// loaded, then clears itself — see StaticMeshViewportPanel::OnUpdate.
		bool NeedsFraming = true;

		bool UsesEditorCamera() const override { return true; }

		void OnInit() override;
		void OnClosed() override;
		void OnOpened() override;
		void OnPanelRegister() override;
		void OnUpdate(float deltaTime) override;
	};
}

#endif //PLUENGINE_STATICMESHVIEWPORT_H