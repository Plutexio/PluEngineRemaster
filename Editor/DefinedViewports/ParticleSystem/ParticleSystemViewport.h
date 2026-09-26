//
// Created by Plutex on 9/26/26.
//

#ifndef PLUENGINE_PARTICLESYSTEMVIEWPORT_H
#define PLUENGINE_PARTICLESYSTEMVIEWPORT_H

#include "EditorViewports/IEditorViewport.h"
#include <imgui-node-editor/imgui_node_editor.h>
#include "NodeGraph/NodeGraphEditor.h"
#include "HashMap/HashMap.h"
#include "ParticleSystemViewport.generated.h"

namespace ImGuiNodeEditor = ax::NodeEditor;

namespace Plu
{
	struct ParticleSystem;
	struct ParticleEmitter;
	struct IParticleParameter;
	struct ParticleSystemInstance;

	// Viewport for ParticleSystem assets. The asset holds N emitters (one node graph each) plus shared
	// user parameters. Panels: Emitters (list), Graph (canvas of the selected emitter), Parameters,
	// Details (selection-driven) and Preview.
	//
	// ONE NodeGraphEditor serves every emitter, with one imgui-node-editor context per emitter (pan/zoom
	// stay per emitter). A NodeGraphEditor per emitter would be wrong: node positions are keyed by node
	// UUID and persisted to a single sidecar, so each editor would LoadLayout the whole file and
	// SaveLayout only its own subset, erasing the rest.
	PLU_CLASS()
	class ParticleSystemViewport : public IEditorViewport
	{
		REFLECTION_BODY_PARTICLESYSTEMVIEWPORT()
	private:
		NodeGraphEditor mNodeGraphEditor;
		HashMap<UInt64, ImGuiNodeEditor::EditorContext*> mEmitterContexts;

		PluUUID mSelectedEmitter = PluUUID(0);
		String mSelectedParameterName;

		// Which value set the parameter inspector resolves against: null = the asset's defaults, otherwise a
		// live PIE instance (ParticleSystemInstance::GetLiveInstances). Dropped once the instance leaves the
		// live registry (PIE ended).
		ParticleSystemInstance* mInspectedInstance = nullptr;

		bool    mLastNodeSelectionValid = false;
		PluUUID mLastSelectedNode = PluUUID(0);

		[[nodiscard]] StringW GetLayoutPath();
		void UpdateInspectorSelection();
	public:
		ParticleSystemViewport() = default;
		~ParticleSystemViewport() override = default;

		void OnClosed() override;
		void OnOpened() override;
		void OnPanelRegister() override;
		void OnUpdate(float deltaTime) override;

		NodeGraphEditor& GetNodeGraphEditor() { return mNodeGraphEditor; }
		// Lazily created; the context is destroyed with the emitter or the viewport.
		ImGuiNodeEditor::EditorContext* GetEmitterContext(const PluUUID& emitterUuid);
		void DestroyEmitterContext(const PluUUID& emitterUuid);

		[[nodiscard]] TUsePointer<ParticleSystem> GetSystem();
		// Null when nothing is selected or the emitter was deleted.
		[[nodiscard]] ParticleEmitter* GetSelectedEmitter();
		void SelectEmitter(const PluUUID& uuid);
		[[nodiscard]] const PluUUID& GetSelectedEmitterUuid() const { return mSelectedEmitter; }

		// Resolves the selected name against mInspectedInstance's live store when one is chosen, otherwise
		// against the asset (defaults).
		[[nodiscard]] TUsePointer<IParticleParameter> GetSelectedParameter();
		[[nodiscard]] ParticleSystemInstance* GetInspectedInstance() const { return mInspectedInstance; }
		void SetInspectedInstance(ParticleSystemInstance* instance) { mInspectedInstance = instance; }
		void SelectParameter(const TUsePointer<IParticleParameter>& parameter);
		[[nodiscard]] const String& GetSelectedParameterName() const { return mSelectedParameterName; }
		// After a parameter rename: keep the selection on it.
		void SetSelectedParameterName(const String& name) { mSelectedParameterName = name; }

		// Any edit that changes what the emitters would run: bumps CompileRevision and dirties the asset.
		void MarkGraphChanged();

		// The preview panel navigates the overlay scene with the shared editor camera.
		bool UsesEditorCamera() const override { return true; }
		// Fit the camera to the effect on the preview's next update (on open, or from its "Frame" button).
		bool NeedsFraming = true;
	};
}

#endif //PLUENGINE_PARTICLESYSTEMVIEWPORT_H
