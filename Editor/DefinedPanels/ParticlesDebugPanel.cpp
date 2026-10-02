//
// Created by Plutex on 2026-09-15.
//

#include "ParticlesDebugPanel.h"
#include "PluEngine/FrameDemand.h"

#include <algorithm>
#include <cstdio>

#include "imgui.h"
#include "EditorAppContext.h"
#include "PluEngine/Timer.h"
#include "PluEngine/Gameplay/GameObject.h"
#include "PluEngine/Gameplay/Components/ParticleSpawnerComponent.h"
#include "PluEngine/Gameplay/Scenes/SceneManager.h"
#include "PluEngine/Gameplay/Scenes/SceneWorld.h"
#include "PluEngine/Render/RenderParticleStats.h"
#include "PluEngine/AssetCore/AssetDescriptor.h"
#include "PluEngine/AssetCore/EngineAssetManager.h"
#include "Managers/Assets/EditorAssetManager.h"
#include "UI/IconsFontAwesome7.h"

extern Plu::EditorAppContext* gEditorAppContext;

namespace
{
	// Past this the render thread is considered to have stopped publishing (main is not sending
	// fresh snapshots, so particles are frozen too).
	constexpr float kStaleStatsSeconds = 0.5f;

	const Vec3 kBoundsColor = Vec3(0.2f, 0.9f, 1.0f);
	const Vec3 kSelectedBoundsColor = Vec3(1.0f, 0.4f, 1.0f);

	// Axis-aligned box as 12 lines, interleaved pos(3)+color(3) like the other debug line buffers.
	void AppendBoxWireframe(DynamicArray<float>& outLineVerts, const Vec3& min, const Vec3& max, const Vec3& color)
	{
		const Vec3 corners[8] = {
			{min.x, min.y, min.z}, {max.x, min.y, min.z}, {max.x, max.y, min.z}, {min.x, max.y, min.z},
			{min.x, min.y, max.z}, {max.x, min.y, max.z}, {max.x, max.y, max.z}, {min.x, max.y, max.z},
		};
		constexpr int edges[12][2] = {
			{0, 1}, {1, 2}, {2, 3}, {3, 0},
			{4, 5}, {5, 6}, {6, 7}, {7, 4},
			{0, 4}, {1, 5}, {2, 6}, {3, 7},
		};
		for (const auto& edge : edges) {
			for (int end = 0; end < 2; end++) {
				const Vec3& corner = corners[edge[end]];
				outLineVerts.PushBack(corner.x);
				outLineVerts.PushBack(corner.y);
				outLineVerts.PushBack(corner.z);
				outLineVerts.PushBack(color.r);
				outLineVerts.PushBack(color.g);
				outLineVerts.PushBack(color.b);
			}
		}
	}

	const Plu::ParticleSpawnerDebugStats* FindSpawnerStats(const Plu::ParticleDebugStats& stats, UInt64 uuid)
	{
		for (const Plu::ParticleSpawnerDebugStats& spawner : stats.Spawners) {
			if (spawner.UUID == uuid) return &spawner;
		}
		return nullptr;
	}

	const Plu::ParticleSystemSpawnerDebugStats* FindSystemSpawnerStats(const Plu::ParticleDebugStats& stats, UInt64 uuid)
	{
		for (const Plu::ParticleSystemSpawnerDebugStats& spawner : stats.SystemSpawners) {
			if (spawner.UUID == uuid) return &spawner;
		}
		return nullptr;
	}

	UInt32 AliveParticles(const Plu::ParticleSystemSpawnerDebugStats& spawner)
	{
		UInt32 alive = 0;
		for (const Plu::ParticleEmitterDebugStats& emitter : spawner.Emitters) alive += emitter.AliveParticles;
		return alive;
	}

	const char* EmissionStateName(Plu::EParticleEmissionState state)
	{
		switch (state) {
			case Plu::EParticleEmissionState::Stopped: return "Stopped";
			case Plu::EParticleEmissionState::Playing: return "Playing";
			case Plu::EParticleEmissionState::Paused:  return "Paused";
		}
		return "?";
	}

	Plu::String FormatBytes(UInt64 bytes)
	{
		char text[32];
		if (bytes >= 1024ull * 1024ull) std::snprintf(text, sizeof(text), "%.2f MB", static_cast<double>(bytes) / (1024.0 * 1024.0));
		else std::snprintf(text, sizeof(text), "%.1f KB", static_cast<double>(bytes) / 1024.0);
		return text;
	}

	Plu::String AssetName(const Plu::TUsePointer<Plu::ParticleSystem>& system)
	{
		if (!system) return "<none>";
		Plu::TUsePointer<Plu::AssetDescriptor> descriptor = gEditorAppContext->EditorAssetManager->GetAssetDescriptor(system->Uuid);
		return descriptor ? descriptor->AssetName : Plu::String("<unsaved>");
	}

	// Render-side stats only describe this world if the render thread last simulated it.
	bool StatsMatchWorld(const Plu::ParticleDebugStats& stats, const Plu::TUsePointer<Plu::SceneWorld>& world)
	{
		return stats.PublishCount > 0 && world && stats.SceneHandle == world->GetObjectHandle();
	}

	Plu::String GetOwnerName(const Plu::TOwningPointer<Plu::ParticleSpawnerComponent>& component)
	{
		Plu::TUsePointer<Plu::GameObject> owner = component->GetParentGameObject();
		if (!owner) return "<no owner>";
		if (owner->GetObjectName().IsEmpty()) return "<unnamed>";
		return owner->GetObjectName();
	}
}

Plu::String Plu::ParticlesDebugPanel::GetPanelName()
{
	return ICON_FA_WAND_MAGIC_SPARKLES " Debug Particles";
}

void Plu::ParticlesDebugPanel::OnUpdate(float deltaTime)
{
	if (BeginPanel()) {
		// Live numbers, no input behind them: keep them ticking under power saving.
		RequestRedrawAfter(kStatsRefreshSeconds);
		PLU_PROFILE_SCOPE("ParticlesDebugPanel::OnUpdate");

		// Renewed every frame the panel is drawn; the render thread gathers only while asked.
		RequestParticleDebugStats();
		if (mOpTimings) RequestParticleOpTimings();
		const ParticleDebugStats stats = GetParticleDebugStats();
		if (stats.PublishCount != mLastPublishCount) {
			mLastPublishCount = stats.PublishCount;
			mSecondsSincePublish = 0.0f;
		} else {
			mSecondsSincePublish += deltaTime;
		}

		TUsePointer<SceneWorld> world = gEditorAppContext->EditorScenesManager
		                              ? gEditorAppContext->EditorScenesManager->GetCurrentWorld()
		                              : TUsePointer<SceneWorld>();
		if (!world) {
			ImGui::TextDisabled("No scene open.");
		} else {
			DrawSummary(world, stats);
			DrawSpawnerTable(world, stats);
			DrawSelectedSpawner(world, stats);
			DrawBounds(world, stats);
		}
	}
	EndPanel();
}

void Plu::ParticlesDebugPanel::DrawSummary(const TUsePointer<SceneWorld>& world, const ParticleDebugStats& stats)
{
	TUsePointer<SceneManager> scenes = gEditorAppContext->EditorScenesManager;
	ImGui::Text("World: %s%s", scenes->GetCurrentWorldName().CStr(), scenes->IsInPIE() ? " (PIE)" : "");

	const bool statsValid = StatsMatchWorld(stats, world);
	if (stats.PublishCount == 0) {
		ImGui::TextDisabled("Waiting for the render thread...");
	} else if (!statsValid) {
		ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.2f, 1.0f), ICON_FA_TRIANGLE_EXCLAMATION " Render thread last simulated a different world.");
	} else if (mSecondsSincePublish > kStaleStatsSeconds) {
		ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.2f, 1.0f), ICON_FA_TRIANGLE_EXCLAMATION " No render update for %.1f s — particles are frozen.", mSecondsSincePublish);
		ImGui::SetItemTooltip("Spawners tick only when the render thread gets a fresh snapshot from main.");
	}

	UInt64 aliveTotal = 0;
	UInt64 poolTotal = 0;
	UInt64 cpuBytes = 0, gpuBytes = 0;
	if (statsValid) {
		for (const ParticleSpawnerDebugStats& spawner : stats.Spawners) {
			aliveTotal += spawner.AliveParticles;
			poolTotal += spawner.PoolSize;
		}
		for (const ParticleSystemSpawnerDebugStats& spawner : stats.SystemSpawners) {
			for (const ParticleEmitterDebugStats& emitter : spawner.Emitters) {
				aliveTotal += emitter.AliveParticles;
				cpuBytes += emitter.CpuBytes;
				gpuBytes += emitter.GpuBytes;
			}
		}
	}

	ImGui::SeparatorText("Summary");
	ImGui::Text("Spawner components: %u", static_cast<UInt32>(world->GetParticleSpawnerComponents().Size()));
	ImGui::Text("Simulated spawners: %u", statsValid ? static_cast<UInt32>(stats.Spawners.Size() + stats.SystemSpawners.Size()) : 0u);
	ImGui::SetItemTooltip("Spawners the render thread holds for this world. Can briefly differ from the component count while a spawner is being created or destroyed.");
	ImGui::Text("Alive particles: %llu", static_cast<unsigned long long>(aliveTotal));
	ImGui::Text("Pool slots: %llu", static_cast<unsigned long long>(poolTotal));
	ImGui::SetItemTooltip("Legacy spawners only: allocated particle slots (alive + free). Pools never shrink, so this is the peak particle count.");
	if (statsValid && !stats.SystemSpawners.IsEmpty()) {
		ImGui::Text("Asset emitters memory: CPU %s, GPU %s", FormatBytes(cpuBytes).CStr(), FormatBytes(gpuBytes).CStr());
		ImGui::SetItemTooltip("SoA columns + ribbon history on the CPU; point / instance / trail buffers on the GPU. Both grow and never shrink.");
	}
	if (statsValid) {
		ImGui::TextDisabled("Particle dt: %.2f ms", stats.DeltaTime * 1000.0f);
	}

	if (stats.OtherWorldSpawners > 0) {
		ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.2f, 1.0f), ICON_FA_TRIANGLE_EXCLAMATION " %u spawner(s) with %llu particle(s) held for other worlds",
		                   stats.OtherWorldSpawners, static_cast<unsigned long long>(stats.OtherWorldAliveParticles));
		ImGui::SetItemTooltip("The renderer only reconciles a world's spawners when that world publishes a snapshot, so spawners of a world that stopped rendering (e.g. PIE after it ends) stay alive until shutdown.");
	}

	ImGui::SeparatorText("Options");
	ImGui::Checkbox("Draw particle bounds", &mDrawBounds);
	ImGui::BeginDisabled(!mDrawBounds);
	ImGui::SameLine();
	ImGui::Checkbox("Selected only", &mDrawBoundsSelectedOnly);
	ImGui::EndDisabled();
	ImGui::Checkbox("Per-op timings (asset emitters)", &mOpTimings);
	ImGui::SetItemTooltip("Times every op of every asset emitter on the render thread (a clock read per op per block) and records "
	                      "them to the Profiler as Particles/<emitter>/<stage> <i> <Op>. Leave off when measuring the total.");
}

void Plu::ParticlesDebugPanel::DrawSpawnerTable(const TUsePointer<SceneWorld>& world, const ParticleDebugStats& stats)
{
	ImGui::SeparatorText("Spawners");

	const auto& components = world->GetParticleSpawnerComponents();
	if (components.IsEmpty()) {
		ImGui::TextDisabled("No ParticleSpawnerComponent in this world.");
		return;
	}

	const bool statsValid = StatsMatchWorld(stats, world);
	const ImGuiTableFlags tableFlags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable |
	                                   ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingFixedFit;
	const float tableHeight = std::min(ImGui::GetTextLineHeightWithSpacing() * (static_cast<float>(components.Size()) + 2.0f), 260.0f);
	if (!ImGui::BeginTable("##particle_spawners", 8, tableFlags, ImVec2(0.0f, tableHeight))) return;

	ImGui::TableSetupScrollFreeze(0, 1);
	ImGui::TableSetupColumn("Object", ImGuiTableColumnFlags_WidthStretch);
	ImGui::TableSetupColumn("Component", ImGuiTableColumnFlags_WidthStretch);
	ImGui::TableSetupColumn("Effect", ImGuiTableColumnFlags_WidthStretch);
	ImGui::TableSetupColumn("Alive");
	ImGui::TableSetupColumn("Pool");
	ImGui::TableSetupColumn("Requested");
	ImGui::TableSetupColumn("Pending");
	ImGui::TableSetupColumn("Burst");
	ImGui::TableHeadersRow();

	for (const auto& entry : components) {
		const TOwningPointer<ParticleSpawnerComponent>& component = entry.second;
		if (!component) continue;
		const ParticleSpawnerDebugStats* spawnerStats = statsValid ? FindSpawnerStats(stats, entry.first) : nullptr;
		const ParticleSystemSpawnerDebugStats* systemStats = statsValid ? FindSystemSpawnerStats(stats, entry.first) : nullptr;

		ImGui::PushID(static_cast<int>(entry.first ^ (entry.first >> 32)));
		ImGui::TableNextRow();

		ImGui::TableNextColumn();
		const bool isSelected = mSelectedSpawnerUuid == entry.first;
		if (ImGui::Selectable(GetOwnerName(component).CStr(), isSelected, ImGuiSelectableFlags_SpanAllColumns)) {
			mSelectedSpawnerUuid = entry.first;
			// Mirror the pick into the editor selection, so the viewport gizmo shows this spawner.
			if (TUsePointer<GameObject> owner = component->GetParentGameObject()) {
				gEditorAppContext->EditorState.SelectedGameObject = owner->GetObjectHandle();
				gEditorAppContext->EditorState.SelectedGameObjectComponent = component->GetObjectHandle();
			}
		}

		ImGui::TableNextColumn();
		ImGui::TextUnformatted(component->GetComponentName().CStr());

		ImGui::TableNextColumn();
		if (component->ParticleSystemAsset) ImGui::TextUnformatted(AssetName(component->ParticleSystemAsset).CStr());
		else ImGui::TextDisabled("legacy");

		ImGui::TableNextColumn();
		if (spawnerStats) ImGui::Text("%u", spawnerStats->AliveParticles);
		else if (systemStats) ImGui::Text("%u", AliveParticles(*systemStats));
		else ImGui::TextDisabled("-");

		ImGui::TableNextColumn();
		if (spawnerStats) ImGui::Text("%u", spawnerStats->PoolSize);
		else ImGui::TextDisabled("-");

		ImGui::TableNextColumn();
		ImGui::Text("%llu", static_cast<unsigned long long>(component->GetRequestedParticles()));

		// Requested on main but not yet consumed by the render thread — normally 0 or one frame's worth.
		ImGui::TableNextColumn();
		if (spawnerStats || systemStats) {
			const UInt64 requested = component->GetRequestedParticles();
			const UInt64 synced = spawnerStats ? spawnerStats->SyncedRequestedParticles : systemStats->SyncedRequestedParticles;
			ImGui::Text("%llu", static_cast<unsigned long long>(requested > synced ? requested - synced : 0));
		} else {
			ImGui::TextDisabled("-");
		}

		ImGui::TableNextColumn();
		ImGui::Text("%d", component->GetLastBurstSize());

		ImGui::PopID();
	}
	ImGui::EndTable();
	ImGui::TextDisabled("Requested = cumulative count asked for by gameplay | Pending = not yet spawned on the render thread");
}

void Plu::ParticlesDebugPanel::DrawSelectedSpawner(const TUsePointer<SceneWorld>& world, const ParticleDebugStats& stats)
{
	if (mSelectedSpawnerUuid == 0) return;

	const TOwningPointer<ParticleSpawnerComponent>* found = world->GetParticleSpawnerComponents().Find(mSelectedSpawnerUuid);
	if (!found || !*found) {
		mSelectedSpawnerUuid = 0;
		return;
	}
	const TOwningPointer<ParticleSpawnerComponent>& component = *found;

	ImGui::SeparatorText("Selected Spawner");
	ImGui::Text("%s / %s", GetOwnerName(component).CStr(), component->GetComponentName().CStr());
	ImGui::TextDisabled("UUID: %llu", static_cast<unsigned long long>(mSelectedSpawnerUuid));

	ImGui::SetNextItemWidth(120.0f);
	ImGui::DragInt("##burst_size", &component->NumParticlesToSpawn, 1.0f, 1, 100000);
	ImGui::SameLine();
	if (ImGui::Button(ICON_FA_BURST " Spawn Burst")) {
		component->SpawnParticles(component->NumParticlesToSpawn);
	}
	ImGui::SetItemTooltip("Calls SpawnParticles(NumParticlesToSpawn) on the component. Works outside PIE too.");

	if (component->ParticleSystemAsset) {
		DrawSelectedSystemSpawner(*component, StatsMatchWorld(stats, world) ? FindSystemSpawnerStats(stats, mSelectedSpawnerUuid) : nullptr);
		return;
	}

	const ParticleSpawnerDebugStats* spawnerStats = StatsMatchWorld(stats, world) ? FindSpawnerStats(stats, mSelectedSpawnerUuid) : nullptr;
	if (!spawnerStats) {
		ImGui::TextDisabled("Not simulated on the render thread yet.");
		return;
	}

	if (ImGui::BeginTable("##selected_spawner", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
		const auto row = [](const char* label) {
			ImGui::TableNextRow();
			ImGui::TableNextColumn();
			ImGui::TextDisabled("%s", label);
			ImGui::TableNextColumn();
		};

		row("Alive / Pool / Free");
		ImGui::Text("%u / %u / %u", spawnerStats->AliveParticles, spawnerStats->PoolSize, spawnerStats->FreeSlots);
		row("Synced requests");
		ImGui::Text("%llu", static_cast<unsigned long long>(spawnerStats->SyncedRequestedParticles));
		row("Location");
		ImGui::Text("%.2f, %.2f, %.2f", spawnerStats->Location.x, spawnerStats->Location.y, spawnerStats->Location.z);
		row("Launch direction");
		ImGui::Text("%.2f, %.2f, %.2f", spawnerStats->LaunchDirection.x, spawnerStats->LaunchDirection.y, spawnerStats->LaunchDirection.z);

		row("Loop");
		if (spawnerStats->Loop && spawnerStats->LastBurstSize > 0) {
			const float progress = spawnerStats->LoopLength > 0.0f ? spawnerStats->LoopTime / spawnerStats->LoopLength : 0.0f;
			char overlay[64];
			snprintf(overlay, sizeof(overlay), "%.2f / %.2f s (burst %d)", spawnerStats->LoopTime, spawnerStats->LoopLength, spawnerStats->LastBurstSize);
			ImGui::ProgressBar(std::clamp(progress, 0.0f, 1.0f), ImVec2(-FLT_MIN, 0.0f), overlay);
		} else {
			ImGui::TextDisabled(spawnerStats->Loop ? "Waiting for the first burst" : "Off");
		}

		if (spawnerStats->AliveParticles > 0) {
			const Vec3 size = spawnerStats->BoundsMax - spawnerStats->BoundsMin;
			row("Bounds size");
			ImGui::Text("%.2f x %.2f x %.2f m", size.x, size.y, size.z);
			row("Speed avg / max");
			ImGui::Text("%.2f / %.2f m/s", spawnerStats->AverageSpeed, spawnerStats->MaxSpeed);
			row("Lifetime left");
			ImGui::Text("%.2f .. %.2f s", spawnerStats->MinLifetimeLeft, spawnerStats->MaxLifetimeLeft);
		}
		ImGui::EndTable();
	}
}

void Plu::ParticlesDebugPanel::DrawSelectedSystemSpawner(ParticleSpawnerComponent& component, const ParticleSystemSpawnerDebugStats* stats)
{
	ImGui::Text("Effect: %s", AssetName(component.ParticleSystemAsset).CStr());

	// Lifecycle controls — the same calls gameplay makes. Work outside PIE too.
	if (ImGui::Button(ICON_FA_PLAY " Play")) component.Play();
	ImGui::SameLine();
	if (ImGui::Button("Deactivate")) component.Deactivate();
	ImGui::SetItemTooltip("Soft stop: no new particles, live ones finish.");
	ImGui::SameLine();
	if (ImGui::Button(ICON_FA_STOP " Stop")) component.Stop();
	ImGui::SetItemTooltip("Hard stop: live particles vanish now.");
	ImGui::SameLine();
	if (ImGui::Button(ICON_FA_PAUSE " Pause")) component.Pause();
	ImGui::SameLine();
	if (ImGui::Button("Resume")) component.Resume();

	const auto row = [](const char* label) {
		ImGui::TableNextRow();
		ImGui::TableNextColumn();
		ImGui::TextDisabled("%s", label);
		ImGui::TableNextColumn();
	};

	if (ImGui::BeginTable("##system_lifecycle", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_BordersInnerV)) {
		ImGui::TableSetupColumn("");
		ImGui::TableSetupColumn("Main (component)");
		ImGui::TableSetupColumn("Render (synced)");
		ImGui::TableHeadersRow();
		const auto both = [&](const char* label, const char* mainText, const char* renderText) {
			row(label);
			ImGui::TextUnformatted(mainText);
			ImGui::TableNextColumn();
			if (renderText) ImGui::TextUnformatted(renderText);
			else ImGui::TextDisabled("-");
		};
		char a[64], b[64];
		std::snprintf(a, sizeof(a), "%s", EmissionStateName(component.GetEmissionState()));
		std::snprintf(b, sizeof(b), "%s", stats ? EmissionStateName(stats->State) : "");
		both("State", a, stats ? b : nullptr);
		std::snprintf(a, sizeof(a), "%u", component.GetActivationVersion());
		std::snprintf(b, sizeof(b), "%u", stats ? stats->ActivationVersion : 0u);
		both("Activation version", a, stats ? b : nullptr);
		std::snprintf(a, sizeof(a), "%u", component.GetClearVersion());
		std::snprintf(b, sizeof(b), "%u", stats ? stats->ClearVersion : 0u);
		both("Clear version", a, stats ? b : nullptr);
		std::snprintf(a, sizeof(a), "%u (mirror)", component.GetCompletedActivationMirror());
		std::snprintf(b, sizeof(b), "%u", stats ? stats->CompletedActivationVersion : 0u);
		both("Completed activation", a, stats ? b : nullptr);
		std::snprintf(a, sizeof(a), "%s%s", component.IsFinished() ? "finished" : "running", component.WasSeenByRenderer() ? "" : " (not seen yet)");
		both("Run", a, nullptr);
		std::snprintf(a, sizeof(a), "%s", component.AutoDestroyWhenFinished ? "on" : "off");
		both("Auto destroy", a, nullptr);
		if (stats) {
			std::snprintf(b, sizeof(b), "rev %u", stats->Revision);
			both("Program", "", b);
		}
		ImGui::EndTable();
	}

	if (!stats) {
		ImGui::TextDisabled("Not simulated on the render thread yet.");
		return;
	}

	if (!stats->ParameterLayout.IsEmpty() && ImGui::TreeNodeEx("Parameters", ImGuiTreeNodeFlags_DefaultOpen)) {
		if (stats->UsingDefaults) {
			ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.2f, 1.0f), ICON_FA_TRIANGLE_EXCLAMATION " Running on asset defaults");
			ImGui::SetItemTooltip("The snapshot's value block did not match the program (a recompile landed in between). Normal for one frame.");
		}
		for (const CompiledParameterSlot& slot : stats->ParameterLayout) {
			char values[96] = {};
			int written = 0;
			for (UInt32 i = 0; i < slot.FloatCount && slot.FloatOffset + i < stats->ParameterValues.Size(); ++i)
				written += std::snprintf(values + written, sizeof(values) - written, i ? ", %.3f" : "%.3f", stats->ParameterValues[slot.FloatOffset + i]);
			ImGui::BulletText("%s = %s", slot.Name.CStr(), values);
		}
		ImGui::TreePop();
	}

	for (UInt32 e = 0; e < stats->Emitters.Size(); ++e) {
		const ParticleEmitterDebugStats& emitter = stats->Emitters[e];
		ImGui::PushID(static_cast<int>(e));
		char header[160];
		std::snprintf(header, sizeof(header), "%s  —  %u / %u%s###emitter", emitter.Name.CStr(), emitter.AliveParticles,
		              emitter.MaxParticles, emitter.Enabled ? "" : "  (disabled)");
		if (ImGui::TreeNodeEx(header, ImGuiTreeNodeFlags_DefaultOpen)) {
			if (ImGui::BeginTable("##emitter", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
				row("Emission");
				if (emitter.EmissionDone) ImGui::TextUnformatted("done");
				else ImGui::Text("t = %.2f s, rate %.1f/s, burst %u", emitter.Time, emitter.SpawnRate, emitter.BurstCount);
				row("Renderers");
				if (!emitter.HasSprite && !emitter.HasRibbon) ImGui::TextDisabled("none (points)");
				else ImGui::Text("%s%s%s", emitter.HasSprite ? "Sprite" : "", emitter.HasSprite && emitter.HasRibbon ? " + " : "",
				                 emitter.HasRibbon ? (emitter.RibbonMode == EParticleRibbonMode::PerParticle ? "Ribbon (per particle)" : "Ribbon (per emitter)") : "");
				row("Storage");
				if (emitter.HistorySamples > 0) ImGui::Text("%s, %u columns, trail %u samples", emitter.OrderedStorage ? "ordered" : "dense", emitter.UsedColumns, emitter.HistorySamples);
				else ImGui::Text("%s, %u columns", emitter.OrderedStorage ? "ordered" : "dense", emitter.UsedColumns);
				row("Memory");
				ImGui::Text("CPU %s, GPU %s", FormatBytes(emitter.CpuBytes).CStr(), FormatBytes(emitter.GpuBytes).CStr());
				row("Program");
				ImGui::Text("%u spawn ops, %u update ops", emitter.SpawnOpCount, emitter.UpdateOpCount);
				if (emitter.HasBounds) {
					const Vec3 size = emitter.BoundsMax - emitter.BoundsMin;
					row("Bounds size");
					ImGui::Text("%.2f x %.2f x %.2f m", size.x, size.y, size.z);
				}
				ImGui::EndTable();
			}
			if (!emitter.Ops.IsEmpty()) {
				float total = 0.0f;
				for (const ParticleOpDebugRow& op : emitter.Ops) total += op.Ms;
				ImGui::Text("Ops, last tick: %.3f ms", total);
				if (ImGui::BeginTable("##ops", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchProp)) {
					for (const ParticleOpDebugRow& op : emitter.Ops) {
						ImGui::TableNextRow();
						ImGui::TableNextColumn();
						ImGui::TextUnformatted(op.Name.CStr());
						ImGui::TableNextColumn();
						ImGui::Text("%.4f ms", op.Ms);
					}
					ImGui::EndTable();
				}
			} else if (mOpTimings) {
				ImGui::TextDisabled("Waiting for a timed tick...");
			}
			ImGui::TreePop();
		}
		ImGui::PopID();
	}
}

void Plu::ParticlesDebugPanel::DrawBounds(const TUsePointer<SceneWorld>& world, const ParticleDebugStats& stats)
{
	if (!mDrawBounds || !StatsMatchWorld(stats, world)) return;

	// Bounds lag the simulation by the render thread's publish, which is fine for a debug overlay.
	for (const ParticleSpawnerDebugStats& spawner : stats.Spawners) {
		if (spawner.AliveParticles == 0) continue;
		const bool isSelected = spawner.UUID == mSelectedSpawnerUuid;
		if (mDrawBoundsSelectedOnly && !isSelected) continue;
		AppendBoxWireframe(world->EditorDebugLineVerts, spawner.BoundsMin, spawner.BoundsMax,
		                   isSelected ? kSelectedBoundsColor : kBoundsColor);
	}
	// Asset spawners: one box per emitter.
	for (const ParticleSystemSpawnerDebugStats& spawner : stats.SystemSpawners) {
		const bool isSelected = spawner.UUID == mSelectedSpawnerUuid;
		if (mDrawBoundsSelectedOnly && !isSelected) continue;
		for (const ParticleEmitterDebugStats& emitter : spawner.Emitters) {
			if (!emitter.HasBounds) continue;
			AppendBoxWireframe(world->EditorDebugLineVerts, emitter.BoundsMin, emitter.BoundsMax,
			                   isSelected ? kSelectedBoundsColor : kBoundsColor);
		}
	}
}

void Plu::ParticlesDebugPanel::OnHide()
{
}

void Plu::ParticlesDebugPanel::OnShow()
{
	SetCanClose(true);
}
