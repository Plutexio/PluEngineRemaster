//
// Created by Plutex on 9/26/26.
//

#include "ParticlePreviewPanel.h"

#include "ParticleSystemViewport.h"
#include "EditorAppContext.h"
#include "Managers/Scene/EditorCamera.h"
#include "PluEngine/Application.h"
#include "PluEngine/Core/BoundingBox.h"
#include "PluEngine/Effects/Particles/ParticleSystem.h"
#include "PluEngine/Effects/Particles/ParticleSystemCompiler.h"
#include "PluEngine/Effects/Particles/ParticleEmitterInstance.h"
#include "PluEngine/Gameplay/Scenes/SceneManager.h"
#include "PluEngine/Gameplay/Scenes/SceneWorld.h"
#include "PluEngine/Render/GLFrameBuffer.h"
#include "PluEngine/Render/RenderingManager.h"
#include <glad/glad.h>
#include <chrono>
#include <cmath>

extern Plu::ApplicationInfo* gApplicationInfo;
extern Plu::EditorAppContext* gEditorAppContext;

void Plu::EditorParticleObject::OnSetupComponents()
{
	SpawnerComponent = AddComponent(ParticleSpawnerComponent::GetStaticClass(), "EditorParticleSpawner");
}

Plu::String Plu::ParticlePreviewPanel::GetPanelName() { return "Preview"; }
void Plu::ParticlePreviewPanel::OnClosed() {}

void Plu::ParticlePreviewPanel::OnOpened()
{
	// Runs whenever the panel becomes visible again, like the mesh viewports: the overlay is a singleton
	// another viewport may have replaced meanwhile. Not during PIE — recreating the overlay there would stop
	// the game from ticking and rendering (Editor/CLAUDE.md), and the Parameters panel edits live PIE
	// instances from this very viewport.
	if (gEditorAppContext->EditorScenesManager->IsInPIE()) return;

	gEditorAppContext->EditorScenesManager->CreateOverlayScene();
	TUsePointer<SceneWorld> overlay = gEditorAppContext->EditorScenesManager->GetCurrentWorld();
	if (!overlay) return;
	mPreviewObject = overlay->SpawnGameObject(EditorParticleObject::GetStaticClass());
	if (ParticleSpawnerComponent* spawner = GetPreviewSpawner()) {
		TUsePointer<ParticleSystemViewport> viewport = DynamicCast<ParticleSystemViewport>(GetParentViewport());
		spawner->ParticleSystemAsset = viewport ? viewport->GetSystem() : nullptr;
		spawner->AutoActivate = false;
		spawner->Play();
	}
}

Plu::ParticleSpawnerComponent* Plu::ParticlePreviewPanel::GetPreviewSpawner()
{
	return mPreviewObject && mPreviewObject->SpawnerComponent ? mPreviewObject->SpawnerComponent.GetRaw() : nullptr;
}

void Plu::ParticlePreviewPanel::FrameCamera(Vec2 imageSize)
{
	EditorSceneCamera* camera = GetParentViewport() ? GetParentViewport()->GetEditorCamera() : nullptr;
	if (!camera || !mPreviewObject) return;

	// Effects have no mesh to fit: frame the asset's fixed bounds, or a 2 m box around the spawner.
	TUsePointer<ParticleSystemViewport> viewport = DynamicCast<ParticleSystemViewport>(GetParentViewport());
	TUsePointer<ParticleSystem> system = viewport ? viewport->GetSystem() : nullptr;
	const float radius = system && system->FixedBoundsRadius > 0.0f ? system->FixedBoundsRadius : 1.0f;
	BoundingBox box;
	box.X = Vec2(-radius, radius);
	box.Y = Vec2(-radius, radius);
	box.Z = Vec2(-radius, radius);
	camera->SetCameraLocation(box.FitCamera(mPreviewObject->GetObjectLocation(), camera->GetCameraRotation(), imageSize,
	                                        camera->GetCameraOptions()->FieldOfView));
}

void Plu::ParticlePreviewPanel::DrawPreview(float deltaTime)
{
	TUsePointer<ParticleSystemViewport> viewport = DynamicCast<ParticleSystemViewport>(GetParentViewport());
	const bool inPIE = gEditorAppContext->EditorScenesManager->IsInPIE();
	ParticleSpawnerComponent* spawner = inPIE ? nullptr : GetPreviewSpawner();

	if (spawner) {
		// Follow the asset object (a reload replaces it); the render side keys everything by uuid + revision.
		spawner->ParticleSystemAsset = viewport ? viewport->GetSystem() : nullptr;
		if (mLoopPreview && spawner->IsFinished()) spawner->Play();

		// 1 m radius, one lap in ~3 s, facing along the motion (forward = -Z, like every spawner).
		constexpr float kOrbitRadius = 1.0f;
		constexpr float kOrbitSpeed = 2.0f; // rad/s
		if (mOrbit) mOrbitAngle = std::fmod(mOrbitAngle + kOrbitSpeed * deltaTime, 6.2831853f);
		else mOrbitAngle = 0.0f;
		const Vec3 location = mOrbit ? Vec3(std::cos(mOrbitAngle) * kOrbitRadius, 0.0f, std::sin(mOrbitAngle) * kOrbitRadius) : Vec3(0.0f);
		if (location != mPreviewObject->GetObjectLocation()) {
			mPreviewObject->SetObjectLocation(location);
			mPreviewObject->SetObjectRotation(mOrbit ? Vec3(0.0f, -glm::degrees(mOrbitAngle) + 180.0f, 0.0f) : Vec3(0.0f));
		}
	}

	ImGui::BeginDisabled(spawner == nullptr);
	if (ImGui::Button("Restart") && spawner) spawner->Play();
	ImGui::SameLine();
	const bool paused = spawner && spawner->GetEmissionState() == EParticleEmissionState::Paused;
	if (ImGui::Button(paused ? "Resume" : "Pause") && spawner) {
		if (paused) spawner->Resume();
		else spawner->Pause();
	}
	ImGui::SameLine();
	if (ImGui::Button("Stop") && spawner) spawner->Stop();
	ImGui::SameLine();
	if (ImGui::Button("Frame") && viewport) viewport->NeedsFraming = true;
	ImGui::SameLine();
	ImGui::Checkbox("Loop", &mLoopPreview);
	ImGui::SameLine();
	ImGui::Checkbox("Orbit", &mOrbit);
	ImGui::EndDisabled();
	ImGui::SameLine();
	if (inPIE) ImGui::TextDisabled("Preview is frozen during Play In Editor");
	else ImGui::Text("%d particles", spawner ? spawner->GetAliveParticles() : 0);

	if (ImGui::CollapsingHeader("CPU benchmark")) DrawBenchmark();

	FrameBuffer* renderFBO = gApplicationInfo->AppRenderingManager->RequestMainFrameBuffer().GetRaw();
	if (!renderFBO || renderFBO->GetHeight() == 0) return;

	const ImVec2 available = ImGui::GetContentRegionAvail();
	if (available.x <= 1.0f || available.y <= 1.0f) return;
	const float textureAspect = static_cast<float>(renderFBO->GetWidth()) / static_cast<float>(renderFBO->GetHeight());
	ImVec2 imageSize;
	if (available.x / available.y > textureAspect) {
		imageSize.y = available.y;
		imageSize.x = imageSize.y * textureAspect;
	} else {
		imageSize.x = available.x;
		imageSize.y = imageSize.x / textureAspect;
	}

	const ImVec2 imagePos = ImGui::GetCursorScreenPos();
	const ImTextureID texture = (ImTextureID)(intptr_t)renderFBO->GetColorTexture()->GetID();
	ImGui::Image(texture, imageSize, ImVec2(0, 1), ImVec2(1, 0));

	if (viewport && viewport->NeedsFraming && spawner) {
		FrameCamera(Vec2(imageSize.x, imageSize.y));
		viewport->NeedsFraming = false;
	}

	// Camera stays put in PIE: the game renders to the same frame buffer (Editor/CLAUDE.md, "PIE").
	const bool hovered = ImGui::IsMouseHoveringRect(imagePos, ImVec2(imagePos.x + imageSize.x, imagePos.y + imageSize.y));
	if (hovered && !inPIE) {
		if (EditorSceneCamera* camera = viewport ? viewport->GetEditorCamera() : nullptr) camera->OnUpdate(deltaTime);
	}
}

void Plu::ParticlePreviewPanel::RunSimulation()
{
	TUsePointer<ParticleSystemViewport> viewport = DynamicCast<ParticleSystemViewport>(GetParentViewport());
	TUsePointer<ParticleSystem> system = viewport ? viewport->GetSystem() : nullptr;
	if (!system) return;

	using Clock = std::chrono::high_resolution_clock;
	const auto compileStart = Clock::now();
	CompiledParticleSystem compiled;
	ParticleSystemCompiler::Compile(*system, compiled);
	mCompileMs = std::chrono::duration<double, std::milli>(Clock::now() - compileStart).count();

	// Keep edited values only while the layout still fits; otherwise restart from the asset defaults.
	mParameterLayout = compiled.ParameterLayout;
	mParameterDefaults = compiled.ParameterDefaults;
	if (mParameterValues.Size() != compiled.ParameterFloatCount) mParameterValues = compiled.ParameterDefaults;

	mResults.Clear();
	for (const CompiledEmitter& emitterProgram : compiled.Emitters) {
		CompiledEmitter program = emitterProgram;
		if (mMaxParticlesOverride > 0) program.MaxParticles = static_cast<UInt32>(mMaxParticlesOverride);

		ParticleEmitterInstance instance;
		instance.SetProgram(program);
		instance.SetProfileOps(true);

		ParticleTickParams tick;
		tick.DeltaTime = mDeltaTime;
		tick.ParameterDefaults = compiled.ParameterDefaults.Data();
		tick.ParameterDefaultCount = compiled.ParameterDefaults.Size();
		if (mUseCustomParameters) {
			tick.ParameterValues = mParameterValues.Data();
			tick.ParameterValueCount = mParameterValues.Size();
		}

		const auto simStart = Clock::now();
		for (int frame = 0; frame < mFrames; ++frame) {
			tick.ExtraSpawn = frame == 0 ? static_cast<UInt32>(mStartBurst) : 0u;
			instance.Tick(tick);
		}

		EmitterResult result;
		result.Name = emitterProgram.Name;
		result.TotalMs = std::chrono::duration<double, std::milli>(Clock::now() - simStart).count();
		result.Alive = instance.Alive();
		result.MaxParticles = program.MaxParticles;
		result.HasBounds = instance.ComputeBounds(result.BoundsMin, result.BoundsMax);
		result.Bytes = instance.GetStorage().GetAllocatedBytes();
		const ParticleOpTimings& timings = instance.GetOpTimings();
		for (UInt32 i = 0; i < program.SpawnOps.Size() && i < timings.SpawnMs.Size(); ++i)
			result.Ops.PushBack(OpRow{ String("spawn ") + String::FromInt(i) + " " + ParticleOpName(program.SpawnOps[i].Code), timings.SpawnMs[i] });
		for (UInt32 i = 0; i < program.UpdateOps.Size() && i < timings.UpdateMs.Size(); ++i)
			result.Ops.PushBack(OpRow{ String("update ") + String::FromInt(i) + " " + ParticleOpName(program.UpdateOps[i].Code), timings.UpdateMs[i] });
		mResults.PushBack(result);
	}
	mHasResults = true;
}

void Plu::ParticlePreviewPanel::OnUpdate(float deltaTime)
{
	if (BeginPanel()) DrawPreview(deltaTime);
	EndPanel();
}

void Plu::ParticlePreviewPanel::DrawBenchmark()
{
	{
		ImGui::SetNextItemWidth(120.0f);
		ImGui::DragInt("Frames", &mFrames, 1.0f, 1, 100000);
		ImGui::SetNextItemWidth(120.0f);
		ImGui::DragFloat("Delta time", &mDeltaTime, 0.0005f, 0.0001f, 1.0f, "%.4f");
		ImGui::SetNextItemWidth(120.0f);
		ImGui::DragInt("Max particles override (0 = asset)", &mMaxParticlesOverride, 1000.0f, 0, 16777216);
		ImGui::SetNextItemWidth(120.0f);
		ImGui::DragInt("Start burst (extra on frame 0)", &mStartBurst, 1000.0f, 0, 16777216);
		ImGui::Checkbox("Use custom parameter values", &mUseCustomParameters);

		if (ImGui::Button("Compile & Simulate")) RunSimulation();

		if (mUseCustomParameters && !mParameterLayout.IsEmpty()) {
			ImGui::TextUnformatted("Parameter block");
			for (const CompiledParameterSlot& slot : mParameterLayout) {
				if (slot.FloatOffset + slot.FloatCount > mParameterValues.Size()) continue;
				ImGui::SetNextItemWidth(200.0f);
				ImGui::DragScalarN(slot.Name.CStr(), ImGuiDataType_Float, &mParameterValues[slot.FloatOffset], slot.FloatCount, 0.01f);
			}
		}

		if (mHasResults) {
			ImGui::Separator();
			ImGui::Text("Compile: %.3f ms", mCompileMs);
			for (const EmitterResult& result : mResults) {
				ImGui::PushID(&result);
				const double avg = mFrames > 0 ? result.TotalMs / mFrames : 0.0;
				ImGui::Text("%s: alive %u / %u, %.3f ms total, %.4f ms/tick, %.1f MB", result.Name.CStr(), result.Alive,
				            result.MaxParticles, result.TotalMs, avg, static_cast<double>(result.Bytes) / (1024.0 * 1024.0));
				if (result.HasBounds)
					ImGui::Text("  bounds (%.2f %.2f %.2f) .. (%.2f %.2f %.2f)", result.BoundsMin.x, result.BoundsMin.y,
					            result.BoundsMin.z, result.BoundsMax.x, result.BoundsMax.y, result.BoundsMax.z);
				if (ImGui::TreeNode("Per-op time (total ms)")) {
					for (const OpRow& row : result.Ops) ImGui::Text("  %-28s %.4f", row.Name.CStr(), row.TotalMs);
					ImGui::TreePop();
				}
				ImGui::PopID();
			}
		}
	}
}
