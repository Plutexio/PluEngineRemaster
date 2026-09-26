//
// Created by Plutex on 2026-09-15.
//

#ifndef PLUENGINE_PARTICLESDEBUGPANEL_H
#define PLUENGINE_PARTICLESDEBUGPANEL_H
#include "Panels/EditorPanel.h"
#include "ParticlesDebugPanel.generated.h"

namespace Plu
{
	struct ParticleDebugStats;
	struct ParticleSpawnerDebugStats;
	struct ParticleSystemSpawnerDebugStats;
	class ParticleSpawnerComponent;
	class SceneWorld;

	// Particle spawners of the current world: the gameplay side (components, request counters, lifecycle)
	// read live on main, joined by UUID with the render-thread side (pools, alive particles, bounds,
	// per-emitter programs and op times) published through RenderParticleStats.h while the panel is open.
	// Covers both legacy ParticleClass spawners and ParticleSystem asset spawners.
	PLU_CLASS()
	class ParticlesDebugPanel : public EditorPanel
	{
		REFLECTION_BODY_PARTICLESDEBUGPANEL()
	public:
		using EditorPanel::EditorPanel;

		String GetPanelName() override;
		void OnUpdate(float deltaTime) override;
		void OnHide() override;
		void OnShow() override;

	private:
		void DrawSummary(const TUsePointer<SceneWorld>& world, const ParticleDebugStats& stats);
		void DrawSpawnerTable(const TUsePointer<SceneWorld>& world, const ParticleDebugStats& stats);
		void DrawSelectedSpawner(const TUsePointer<SceneWorld>& world, const ParticleDebugStats& stats);
		void DrawSelectedSystemSpawner(ParticleSpawnerComponent& component, const ParticleSystemSpawnerDebugStats* stats);
		void DrawBounds(const TUsePointer<SceneWorld>& world, const ParticleDebugStats& stats);

		// Component UUID of the spawner shown in the details section; 0 = none.
		UInt64 mSelectedSpawnerUuid = 0;
		bool mDrawBounds = true;
		bool mDrawBoundsSelectedOnly = false;
		// Asks the render thread to time every op of every asset emitter (costly, off by default).
		bool mOpTimings = false;

		// Staleness tracking: the render thread only publishes when it renders a fresh snapshot.
		UInt64 mLastPublishCount = 0;
		float mSecondsSincePublish = 0.0f;
	};
}

#endif //PLUENGINE_PARTICLESDEBUGPANEL_H
