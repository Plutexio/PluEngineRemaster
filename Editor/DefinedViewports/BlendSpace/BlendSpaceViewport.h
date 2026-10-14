//
// Created by Plutex on 10/14/26.
//

#ifndef PLUENGINE_BLENDSPACEVIEWPORT_H
#define PLUENGINE_BLENDSPACEVIEWPORT_H
#include "EditorViewports/IEditorViewport.h"
#include "BlendSpaceViewport.generated.h"
#include "PluEngine/Core.h"
#include "PluEngine/Gameplay/GameObject.h"
#include "PluEngine/Gameplay/Components/SkeletalMeshComponent.h"
#include "PluEngine/AssetTypes/Animation/BlendSpace.h"
#include "PluEngine/AssetTypes/AnimationGraph/AnimationGraph.h"
#include "PluEngine/PluUUID.h"

namespace Plu
{
	struct MaterialInfo;

	// Hosts the BlendSpace preview mesh in the overlay scene. Its own class (not
	// EditorSkeletalMeshObject) for the reason EditorAttachPointPreviewObject spells out: the overlay
	// scene is a shared singleton and other viewports find their object by class.
	PLU_CLASS()
	class EditorBlendSpacePreviewObject : public GameObject
	{
		REFLECTION_BODY_EDITORBLENDSPACEPREVIEWOBJECT()
	public:
		EditorBlendSpacePreviewObject() = default;
		virtual ~EditorBlendSpacePreviewObject() override = default;

		void OnSetupComponents() override;

		TUsePointer<SkeletalMeshComponent> MeshComponent;
	};

	// Editor for BlendSpace assets: a 3D preview (overlay scene, shared editor camera), the sample
	// canvas, and a details panel. The preview plays the asset through a transient in-memory
	// AnimationGraph (AnimBlendSpaceNode → output, X/Y float variables) — the exact runtime path,
	// phase sync included — with the canvas' preview cursor feeding the variables.
	PLU_CLASS()
	class BlendSpaceViewport : public IEditorViewport
	{
		REFLECTION_BODY_BLENDSPACEVIEWPORT()
	public:
		BlendSpaceViewport() = default;
		virtual ~BlendSpaceViewport() override = default;

		// --- View state (never marks the asset dirty) ---

		// Preview input in axis units, set by Ctrl+dragging on the canvas.
		Vec2 PreviewInput = Vec2(0.0f);

		// Canvas selection: index into BlendSpace::Samples, -1 for none. Shared with the details panel.
		Int64 SelectedSample = -1;

		bool IsPlaying = true;

		// Fit the editor camera to the preview mesh on the next preview update (initial value, the
		// "Frame" button, or a new preview mesh). Stays pending until the mesh has vertices.
		bool NeedsFraming = true;

		// Skeletal preview material (its vertex shader reads the bone SSBO), like SkeletalMeshViewport.
		TUsePointer<MaterialInfo> Material;

		[[nodiscard]] TUsePointer<BlendSpace> GetBlendSpace();

		// Transient graph playing this blend space — never an asset, owned by the viewport.
		[[nodiscard]] TUsePointer<AnimationGraph> GetPreviewGraph() { return mPreviewGraph; }

		// Re-wires the preview graph's X/Y pins after the blend space changed dimensions/axes
		// (no-op when its pins are current). Called by the preview panel each frame.
		void SyncPreviewGraph();

		bool UsesEditorCamera() const override { return true; }

		void OnOpened() override;
		void OnClosed() override;
		void OnPanelRegister() override;
		void OnUpdate(float deltaTime) override;

	private:
		void BuildPreviewGraph();

		TOwningPointer<AnimationGraph> mPreviewGraph;
		PluUUID mPreviewNodeUuid;
		PluUUID mPreviewXNodeUuid;
		PluUUID mPreviewYNodeUuid;
	};
}

#endif //PLUENGINE_BLENDSPACEVIEWPORT_H
