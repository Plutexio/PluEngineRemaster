//
// Created by Plutex on 9/26/26.
//

#ifndef PLUENGINE_PARTICLEPREVIEWPANEL_H
#define PLUENGINE_PARTICLEPREVIEWPANEL_H
#include "EditorViewports/IEditorPanel.h"
#include "PluEngine/PluTypes.h"
#include "PluEngine/Effects/Particles/CompiledParticleSystem.h"
#include "PluEngine/Gameplay/GameObject.h"
#include "PluEngine/Gameplay/Components/ParticleSpawnerComponent.h"
#include "ParticlePreviewPanel.generated.h"
#include "PluEngine/Core.h"

namespace Plu
{
	// The object the preview spawns into the overlay scene: one spawner playing the open asset.
	PLU_CLASS()
	class EditorParticleObject : public GameObject
	{
		REFLECTION_BODY_EDITORPARTICLEOBJECT()
	public:
		EditorParticleObject() = default;
		~EditorParticleObject() override = default;

		void OnSetupComponents() override;

		TUsePointer<ParticleSpawnerComponent> SpawnerComponent;
	};

	// Live preview: the open asset played by an EditorParticleObject in the editor overlay scene, rendered
	// through the normal scene path into the main frame buffer (same setup as StaticMeshViewportPanel).
	// Edits reach it with no extra wiring: CompileRevision++ -> the next snapshot recompiles -> the render
	// thread adopts the new program. Frozen during PIE (one overlay, one frame buffer — Editor/CLAUDE.md).
	//
	// Below it, collapsed, the CPU simulation benchmark: compile the open asset, tick every emitter N frames
	// on this thread and report alive count, bounds and per-op timings.
	PLU_CLASS()
	class ParticlePreviewPanel : public IEditorPanel
	{
		REFLECTION_BODY_PARTICLEPREVIEWPANEL()
	private:
		TUsePointer<EditorParticleObject> mPreviewObject;
		// Restart the effect as soon as it finishes (one-shot effects would otherwise play once).
		bool mLoopPreview = true;
		// Move the spawner round a circle (ribbons need a moving spawner to show anything).
		bool mOrbit = false;
		float mOrbitAngle = 0.0f;

		ParticleSpawnerComponent* GetPreviewSpawner();
		void FrameCamera(Vec2 imageSize);
		void DrawPreview(float deltaTime);
		void DrawBenchmark();

		struct OpRow
		{
			String Name;
			double TotalMs = 0.0;
		};
		struct EmitterResult
		{
			String Name;
			UInt32 Alive = 0;
			UInt32 MaxParticles = 0;
			bool HasBounds = false;
			Vec3 BoundsMin = Vec3(0.0f);
			Vec3 BoundsMax = Vec3(0.0f);
			double TotalMs = 0.0;
			UInt64 Bytes = 0;
			DynamicArray<OpRow> Ops;
		};

		int mFrames = 60;
		float mDeltaTime = 1.0f / 60.0f;
		int mMaxParticlesOverride = 0; // 0 = as authored
		int mStartBurst = 0;           // extra particles spawned on the first tick (stress)
		bool mUseCustomParameters = false;

		bool mHasResults = false;
		double mCompileMs = 0.0;
		DynamicArray<EmitterResult> mResults;
		// Layout + values of the parameter block used by the test (edited below, fed to the executor).
		DynamicArray<CompiledParameterSlot> mParameterLayout;
		DynamicArray<float> mParameterValues;
		DynamicArray<float> mParameterDefaults;

		void RunSimulation();
	public:
		ParticlePreviewPanel() = default;
		~ParticlePreviewPanel() override = default;

		String GetPanelName() override;
		void OnClosed() override;
		void OnOpened() override;
		void OnUpdate(float deltaTime) override;
	};
}

#endif //PLUENGINE_PARTICLEPREVIEWPANEL_H
