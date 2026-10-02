//
// Created by Plutex on 2026-02-08.
//

#include "PluEngine/Render/RenderingManager.h"

#include "imgui_impl_opengl3.h"
#include "imgui_impl_sdl3.h"
#include "PluEngine/Core/ApplicationInfo.h"
#include "PluEngine/Engine.h"
#include "PluEngine/AssetCore/AssetDescriptor.h"
#include "PluEngine/AssetCore/EngineAssetManager.h"
#include "PluEngine/AssetTypes/StaticMesh/StaticMesh.h"
#include "PluEngine/AssetTypes/SkeletalMesh/SkeletalMesh.h"
#include "PluEngine/AssetTypes/Texture/Texture.h"
#include "PluEngine/Render/ShadersManager.h"
#include "PluEngine/Core/Objects/EngineObjectManager.h"
#include "PluEngine/PluUtils.h"
#include "PluEngine/Render/GLTexture.h"
#include "PluEngine/Render/GPUProfiler.h"
#include "PluEngine/Render/ImGuiDrawSnapshot.h"
#include "PluEngine/Render/Renderer.h"
#include "PluEngine/Render/RenderThreading.h"
#include "PluEngine/Core/Threading/ThreadAffinity.h"
#include "PluEngine/FrameDemand.h"
#include "PluEngine/Platform/Window.h"

static Plu::TripleBuffer<Plu::RenderSnapshot *>* gTripleBuffer = nullptr;
static Plu::Renderer* gRenderer = nullptr;
static std::atomic<bool> gIsRendererGut(false);

void Plu::RenderingManager::RenderThreadEnter()
{
	PLU_CORE_TRACE("Render Thread Started");
	// Nazwa dla diagnostyki — profiler grupuje po niej wpisy (filtr wątku w panelu).
	RegisterThreadName("Render");
	if (!mApplicationInfo) return;
	mApplicationInfo->AppWindow->MakeGLContextCurrent();
	// Stan GL (depth test, blending, debug output) jest per-kontekst — ustawiany tutaj,
	// bo to ten wątek trzyma kontekst przez całe życie renderera.
	mGLState.Initialize();
	gRenderer = new Renderer();
	gRenderer->Initialize(mApplicationInfo);

	// The OpenGL ImGui backend lives on the render thread: ImGui_ImplOpenGL3_Init queries
	// glGetString(GL_VERSION) and every RenderDrawData call issues GL commands, so it must
	// run where the GL context is current. The SDL3/Win32 (platform/input) backend stays on Main.
	// Window 0's context was created before this thread started and is already queued; every
	// later window arrives through the same queue, drained once per frame.
	ProcessImGuiBackendQueues();

	gIsRendererGut = true;
	while (mIsRendererRunning) {
		if (!RenderThreadLoop()) WaitForFrameWork();
	}
	gIsRendererGut = false;
	RenderThreadExit();
}

void Plu::RenderingManager::NotifyFrameAvailable()
{
	{
		std::lock_guard<std::mutex> lock(mFrameWorkMtx);
		mFrameWorkPending = true;
	}
	mFrameWorkCv.notify_one();
}

void Plu::RenderingManager::WaitForFrameWork()
{
	PLU_PROFILE_SCOPE("Render Idle Wait");
	// The timeout keeps the housekeeping at the top of RenderThreadLoop (request queues, window
	// lifecycle handshake, GPU timer results) moving for callers that do not notify.
	std::unique_lock<std::mutex> lock(mFrameWorkMtx);
	mFrameWorkCv.wait_for(lock, std::chrono::milliseconds(50), [this] {
		return mFrameWorkPending || !mIsRendererRunning;
	});
	mFrameWorkPending = false;
}

bool Plu::RenderingManager::RenderThreadLoop()
{
	PLU_PROFILE_SCOPE("Render Thread Frame");
	const std::chrono::high_resolution_clock::time_point nowRenderFrame = std::chrono::high_resolution_clock::now();

	// Odbiera wyniki GPU_TIMESTAMP zapytań zleconych w poprzednich klatkach (async, patrz
	// GPUProfiler.h) zanim ten frame zleci nowe pod tymi samymi nazwami.
	GPUProfileScope::PollResults();

	// static double period = 0.000000003f;
	// double sineWave = (std::sin(period * std::chrono::high_resolution_clock::now().time_since_epoch().count()) + 1) / 2.0f;
	// glClearColor(sineWave, sineWave, sineWave, 1.0f);
	// glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	// Scena renderuje się TYLKO dla świeżo opublikowanego snapshotu. Stale snapshot (main nie
	// zdążył opublikować — render thread szybszy) oznacza identyczne dane wejściowe, a FBO sceny
	// (mMainBuffer) wciąż trzyma poprzedni obraz — re-render byłby czystą stratą GPU (i podwajał
	// liczniki Stat* akumulowane w snapshotcie). ImGui/blit/swap lecą co klatkę niezależnie —
	// backbuffer po SwapBuffer jest niezdefiniowany, więc prezentację trzeba powtarzać zawsze.
	// Wyjątek od pomijania stale'a: po resize głównego FBO (koniec tej funkcji) jego zawartość
	// jest niezdefiniowana — scenę trzeba przemalować nawet ze starego snapshotu, inaczej do
	// czasu publikacji maina blitowalibyśmy śmieci.
	static bool sSceneBufferInvalidated = false;

	// Power saving: present only when this iteration was handed something new. Not swapping leaves
	// the last presented image on screen; anything that damages it (expose, resize) reaches Main as
	// an event and comes back as a fresh ImGui frame. With power saving off every iteration
	// presents, as it always did.
	bool shouldPresent = !IsPowerSavingEnabled();

	bool freshSnapshot = false;
	RenderSnapshot* snapshot = gTripleBuffer->AcquireReadBuffer(&freshSnapshot);
	mApplicationInfo->AppRenderingManager->Tick(freshSnapshot && snapshot != nullptr);
	if (snapshot && (freshSnapshot || sSceneBufferInvalidated)) {
		// Time since the previous scene render, not renderDeltaTime: frames that skip a stale
		// snapshot would otherwise be lost, and the particle tick / `time` uniform would run in
		// slow motion whenever the render thread outpaces main.
		static std::chrono::high_resolution_clock::time_point lastSceneRender = nowRenderFrame;
		const float sceneDeltaTime = std::chrono::duration<float>(nowRenderFrame - lastSceneRender).count();
		lastSceneRender = nowRenderFrame;
		gRenderer->RenderSnapshot(snapshot, sceneDeltaTime);
		sSceneBufferInvalidated = false;
		shouldPresent = true;
	}

	// Windows created/closed since the last frame get (or lose) their GL-side ImGui backend here,
	// before anything below looks at mImGuiStates.
	ProcessImGuiBackendQueues();

	bool wasImGuiRendered = false;

	// ImGui overlay on top of the scene. The draw data was deep-copied on the Main thread
	// and handed over through the triple buffer; here we just upload any pending textures
	// (handled inside RenderDrawData via DrawData.Textures) and submit.
	if (mApplicationInfo->AppWindow->GetImGuiContext() && !IsImGuiRenderingIgnored()) {
		// Lockstep gate: while the Main thread is rebuilding the font atlas it parks this ImGui
		// section so it can mutate the shared ImTextureData/ImFontAtlas exclusively. We announce
		// we're parked, then wait for Main to grant exactly one pass (or to leave lockstep). Outside
		// a rebuild this is a single atomic load and we render as usual. See mImGuiLockstep.
		bool consumedGrant = false;
		bool skipImGui = false;
		if (mImGuiLockstep.load(std::memory_order_acquire)) {
			std::unique_lock<std::mutex> lock(mImGuiLockstepMtx);
			mImGuiRenderParked = true;
			mImGuiLockstepCv.notify_all();
			mImGuiLockstepCv.wait(lock, [this] {
				return mImGuiPassGranted || !mImGuiLockstep.load(std::memory_order_acquire) || !mIsRendererRunning;
			});
			mImGuiRenderParked = false;
			if (!mIsRendererRunning) {
				skipImGui = true;
			} else if (mImGuiPassGranted) {
				mImGuiPassGranted = false;
				consumedGrant = true;
			}
			// else: Main left lockstep without a pending grant -> fall through and render normally.
		}

		if (!skipImGui) {
			// The whole loop over windows is one "ImGui pass" as far as the lockstep is concerned:
			// they share the font atlas, so granting per window would let Main mutate it between
			// two windows of the same frame.
			bool freshGuiSnapshot = false;
			ImGuiFrameSnapshot* guiSnapshot = mImguiTripleBuffer.AcquireReadBuffer(&freshGuiSnapshot);
			// A granted lockstep pass has to run even on a stale snapshot: Main is blocked on it,
			// and it is what uploads the pending font-atlas textures.
			if (freshGuiSnapshot || consumedGrant) shouldPresent = true;
			if (guiSnapshot && shouldPresent) {
				for (UInt32 i = 0; i < guiSnapshot->ActiveCount; ++i) {
					const ImGuiWindowDrawSnapshot* entry = guiSnapshot->Windows[i];
					// The main window is presented last, together with the scene blit below.
					if (entry->WindowID == 0) continue;
					RenderImGuiWindow(entry, false, nullptr);
				}
				for (UInt32 i = 0; i < guiSnapshot->ActiveCount; ++i) {
					const ImGuiWindowDrawSnapshot* entry = guiSnapshot->Windows[i];
					if (entry->WindowID != 0) continue;
					RenderImGuiWindow(entry, true, &wasImGuiRendered);
				}
			}
		}

		if (consumedGrant) {
			std::lock_guard<std::mutex> lock(mImGuiLockstepMtx);
			mImGuiPassDone = true;
			mImGuiLockstepCv.notify_all();
		}
	}

	TUsePointer<IWindow> window = mApplicationInfo->AppWindow;
	// Secondary windows may have left their own GL context binding current.
	window->MakeGLContextCurrent();
	if (shouldPresent && !wasImGuiRendered) {
		gRenderer->GetMainFrameBuffer()->BlitToScreen(window->GetWidth(), window->GetHeight());
	}

	if (shouldPresent) {
		// Render-thread frame delta = time between presents, published for Main's frame pacing and
		// the editor panels. Only this thread runs RenderThreadLoop, so a function-local static is
		// race-free. A gap this long is an idle stretch, not a frame rate — keep the last real
		// value rather than report (and pace Main by) a fraction of a frame per second.
		static std::chrono::high_resolution_clock::time_point lastPresent = nowRenderFrame;
		const float renderDeltaTime = std::chrono::duration<float>(nowRenderFrame - lastPresent).count();
		lastPresent = nowRenderFrame;
		if (renderDeltaTime > 0.0f && renderDeltaTime < 0.1f) SetRenderThreadDeltaTime(renderDeltaTime);

		PLU_PROFILE_SCOPE("Render Thread Swap Buffers");
		// The swap interval belongs to the GL context, and every window shares one context — so N
		// vsynced swaps per frame would stall N times and divide the frame rate by N. Secondary
		// windows swap with interval 0 (RenderImGuiWindow) and the main window, swapped last,
		// restores the configured interval so overall pacing stays tied to it.
		const bool wantVSync = window->IsVSyncEnabled();
		if (mSwapIntervalVSync != wantVSync) {
			window->ApplySwapInterval(wantVSync);
			mSwapIntervalVSync = wantVSync;
		}
		window->SwapBuffer();
		// SwapBuffer applies a vsync change requested from Main (settings UI) right after swapping.
		mSwapIntervalVSync = window->IsVSyncEnabled();
	}

	int windowWidth = window->GetWidth();
	int windowHeight = window->GetHeight();
	int bufferWidth = gRenderer->GetMainFrameBuffer()->GetWidth();
	int bufferHeight = gRenderer->GetMainFrameBuffer()->GetHeight();
	if (windowWidth != bufferWidth || windowHeight != bufferHeight) {
		gRenderer->GetMainFrameBuffer()->Resize(windowWidth, windowHeight);
		sSceneBufferInvalidated = true;
		// The repaint happens next iteration — do not sleep in between.
		return true;
	}
	return shouldPresent;
}

void Plu::RenderingManager::ProcessImGuiBackendQueues()
{
	DynamicArray<UInt32> toInit;
	DynamicArray<UInt32> toTearDown;
	{
		std::lock_guard<std::mutex> lock(mImGuiStatesMutex);
		if (mWindowsNeedingGLBackend.IsEmpty() && mWindowsToTearDownGL.IsEmpty()) return;
		toInit = mWindowsNeedingGLBackend;
		toTearDown = mWindowsToTearDownGL;
		mWindowsNeedingGLBackend.Clear();
		mWindowsToTearDownGL.Clear();
	}

	for (UInt32 windowID : toInit) {
		WindowImGuiState* state = nullptr;
		{
			std::lock_guard<std::mutex> lock(mImGuiStatesMutex);
			WindowImGuiState** found = mImGuiStates.Find(windowID);
			if (found) state = *found;
		}
		if (!state || state->State.IsRendererBackendInitialized()) continue;
		// Every context's GL backend lives in the one shared GL context, so which window is
		// current does not matter — but one must be, and window 0 always is on this thread.
		state->State.InitRendererBackend(state->State.GetContext());
	}

	for (UInt32 windowID : toTearDown) {
		WindowImGuiState* state = nullptr;
		{
			std::lock_guard<std::mutex> lock(mImGuiStatesMutex);
			WindowImGuiState** found = mImGuiStates.Find(windowID);
			if (found) state = *found;
			// Out of the map first: from here on no frame of this loop can pick the window up
			// again, even if the triple buffer hands us a stale snapshot that still lists it.
			mImGuiStates.Remove(windowID);
		}
		if (state) {
			state->State.ShutdownRendererBackend();
			// The context object itself is Main's to destroy (its platform backend lives there);
			// hand the entry back through mWindowsSafeToDestroy.
			std::lock_guard<std::mutex> lock(mImGuiStatesMutex);
			mWindowsSafeToDestroy.PushBack(windowID);
			mPendingDestroyStates.Insert(windowID, state);
		} else {
			std::lock_guard<std::mutex> lock(mImGuiStatesMutex);
			mWindowsSafeToDestroy.PushBack(windowID);
		}
	}
	// Window 0's context must be current again for the rest of this frame.
	if (!toTearDown.IsEmpty()) {
		std::lock_guard<std::mutex> lock(mImGuiStatesMutex);
		if (WindowImGuiState** main = mImGuiStates.Find(0u)) {
			ImGui::SetCurrentContext((*main)->State.GetContext());
		}
	}
}

void Plu::RenderingManager::RenderImGuiWindow(const ImGuiWindowDrawSnapshot* entry, bool isMainWindow, bool* outRendered)
{
	WindowImGuiState* state = nullptr;
	{
		std::lock_guard<std::mutex> lock(mImGuiStatesMutex);
		WindowImGuiState** found = mImGuiStates.Find(entry->WindowID);
		if (found) state = *found;
	}
	// No state = the window was closed (or its backend is not up yet). Skipping is the whole point
	// of the teardown handshake: a stale snapshot may still list a window Main has already dropped.
	if (!state || !state->Window || !state->State.IsRendererBackendInitialized()) return;

	TUsePointer<IWindow> window = state->Window;
	// Every window, not just the secondary ones: the shared GL context is still bound to whichever
	// window was drawn last, so without this the main window's UI would end up in another window's
	// drawable — and the main window would sit frozen on its last frame.
	window->MakeGLContextCurrent();
	ImGui::SetCurrentContext(state->State.GetContext());
	ImGui_ImplOpenGL3_NewFrame(); // lazily (re)creates the backend's GL device objects

	if (!isMainWindow) {
		// Secondary windows have no scene FBO to blit — they draw ImGui onto a cleared backbuffer.
		glViewport(0, 0, window->GetWidth(), window->GetHeight());
		glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	}

	if (entry->Draw.DrawData.Valid) {
		ImGui_ImplOpenGL3_RenderDrawData(const_cast<ImDrawData*>(&entry->Draw.DrawData));
		if (outRendered) *outRendered = true;
	}

	if (!isMainWindow) {
		// See the main window's swap: only one window per frame may wait on vsync.
		if (mSwapIntervalVSync) {
			window->ApplySwapInterval(false);
			mSwapIntervalVSync = false;
		}
		window->SwapBuffer();
	}
}

void Plu::RenderingManager::RenderThreadExit()
{
	// Tear down the GL ImGui backends on the render thread while its context is still current.
	{
		std::lock_guard<std::mutex> lock(mImGuiStatesMutex);
		for (auto& state : mImGuiStates) {
			state.second->State.ShutdownRendererBackend();
		}
	}
	gRenderer->Shutdown();
	delete gRenderer;

	DynamicArray<UInt64> textureIds;
	for (const auto& texture : mTextures) {
		textureIds.PushBack(texture.first);
	}
	for (const auto& txt : textureIds) {
		UnloadTextureForUUID(txt);
	}

	// Release render-owned shader programs on this (the render) thread, while the GL context is
	// still current — see IShaderManager::ReleaseRenderResources. Must run before ReleaseGLContext.
	if (mApplicationInfo->AppShaderManager) mApplicationInfo->AppShaderManager->ReleaseRenderResources();
	mApplicationInfo->AppWindow->ReleaseGLContext();
	PLU_CORE_TRACE("Render Thread Exit");
}

void Plu::RenderingManager::OnStaticMeshRender(StaticMesh *staticMesh)
{
	if (mStaticMeshUsePerFrame.Contains(staticMesh->Uuid)) {
		mStaticMeshUsePerFrame[staticMesh->Uuid]++;
	} else {
		mStaticMeshUsePerFrame.Insert(staticMesh->Uuid, 1);
	}
}

void Plu::RenderingManager::OnSkeletalMeshRender(SkeletalMesh *skeletalMesh)
{
	if (mSkeletalMeshUsePerFrame.Contains(skeletalMesh->Uuid)) {
		mSkeletalMeshUsePerFrame[skeletalMesh->Uuid]++;
	} else {
		mSkeletalMeshUsePerFrame.Insert(skeletalMesh->Uuid, 1);
	}
}

Plu::RenderingManager::RenderingManager(ApplicationInfo *applicationInfo)
{
	mApplicationInfo = applicationInfo;
	PLU_CORE_TRACE("Rendering Manager Init");
}

Plu::RenderingManager::~RenderingManager()
{
	PLU_CORE_TRACE("Rendering Manager Destroy");

	// The render thread has exited by now (Shutdown joins it), so the ImGui triple buffer has no
	// reader/writer anymore — free the lazily allocated snapshot slots (SubmitImGuiDrawData).
	for (ImGuiFrameSnapshot*& slot : mImguiTripleBuffer.GetBuffersForTeardown()) {
		delete slot;
		slot = nullptr;
	}

	// Whatever the window teardown handshake never claimed (window 0, which lives as long as the
	// app does). Contexts are not destroyed here: the process is going down and ImGui's own state
	// is gone by the time the manager is destroyed.
	for (auto& state : mPendingDestroyStates) delete state.second;
	mPendingDestroyStates.Clear();
	for (auto& state : mImGuiStates) delete state.second;
	mImGuiStates.Clear();
}

void Plu::RenderingManager::RequestTextureFromInfo(const TUsePointer<TextureInfo>& textureInfo)
{
	if (!textureInfo) return;

	// GL creation and the texture maps belong to the render thread. A request coming from any other
	// thread (the editor texture-preview panel queries from Main) must not issue GL or mutate the
	// maps here — instead enqueue it and let the render thread do the load in Tick(). The render
	// thread's own callers (e.g. ShaderProgram during RenderSnapshot) load immediately.
	if (!IsOnMainThread() && !mLimitTextureLoadPerFrame.load()) {
		std::lock_guard<std::mutex> lock(mTextureMutex);
		LoadTextureFromInfo_NoLock(textureInfo);
		return;
	}

	{
		std::lock_guard<std::mutex> lock(mTextureMutex);
		// Already loaded — nothing to do.
		if (mTextures.Contains(textureInfo->Uuid)) return;
	}
	// Already queued? The panel re-requests every frame until the texture appears, so dedupe keeps
	// the pending queue from growing while the render thread catches up. The scan happens inside the
	// queue's own lock — mTextureMutex is no longer held for it. Identity is the UUID, not the
	// TUsePointer, because the same texture can be requested through two different info handles.
	mPendingTextureRequests.PushBackUniqueIf(textureInfo, [&textureInfo](const TUsePointer<TextureInfo>& queued) {
		return queued && queued->Uuid == textureInfo->Uuid;
	});
	NotifyFrameAvailable();
}

void Plu::RenderingManager::LoadTextureFromInfo_NoLock(const TUsePointer<TextureInfo>& textureInfo)
{
	if (!textureInfo) return;
	if (mTextures.Contains(textureInfo->Uuid)) return;
	TUsePointer<Texture> texture = mApplicationInfo->AppObjectManager->CreateObject(Texture::GetStaticClass());
	texture->CreateFromInfo(textureInfo.GetRaw(), false);
	mTextures.Insert(textureInfo->Uuid, mApplicationInfo->AppObjectManager->GetObjectAsOwner<Texture>(*texture->GetEngineObjectHandle()));
	mTextureUsePerFrame[textureInfo->Uuid] = 0;
	PLU_CORE_INFO("Texture {} Loaded!", textureInfo->Uuid.getUUID());
}

void Plu::RenderingManager::ProcessPendingTextureRequests()
{
	if (mPendingTextureRequests.IsEmpty()) return;

	// A LOCAL scratch buffer, never a member — the queue's contract (re-entrancy, and not
	// keeping the batch alive past this call). Drain is an O(1) buffer swap, so the queue's
	// lock is held for a constant time regardless of how deep it got.
	Queue<TUsePointer<TextureInfo>> scratch;
	mPendingTextureRequests.Drain(scratch);

	const bool limitLoads = mLimitTextureLoadPerFrame.load();
	int loadedTexturesThisFrame = 0;

	// Newest request first, as before — it is the one something on screen is waiting for.
	for (Int64 i = static_cast<Int64>(scratch.Size()) - 1; i >= 0; i--) {
		if (limitLoads && loadedTexturesThisFrame >= MAX_TEXTURES_LOAD_PER_FRAME) {
			// Over this frame's budget. Put the untouched remainder back, oldest first, so the
			// next drain sees them in the original order. Requests that arrived while we were
			// loading are already in the queue — hence the dedupe on the way back in.
			for (Int64 j = 0; j <= i; j++) {
				const TUsePointer<TextureInfo>& requeued = scratch[j];
				if (!requeued) continue;
				mPendingTextureRequests.PushBackUniqueIf(requeued, [&requeued](const TUsePointer<TextureInfo>& queued) {
					return queued && queued->Uuid == requeued->Uuid;
				});
			}
			break;
		}

		// One short lock hold per texture instead of one spanning the whole batch.
		std::lock_guard<std::mutex> lock(mTextureMutex);
		LoadTextureFromInfo_NoLock(scratch[i]);
		loadedTexturesThisFrame++;
	}
	// The UI that asked for these is drawing a placeholder until it gets another frame.
	if (loadedTexturesThisFrame > 0) RequestRedraw();
	// Over-budget remainder was requeued: come straight back for it instead of sleeping.
	if (!mPendingTextureRequests.IsEmpty()) NotifyFrameAvailable();
}

void Plu::RenderingManager::RequestTextureSave(const TUsePointer<Texture>& texture, const Path& path)
{
	if (!texture) return;
	// GL readback belongs to the render thread. A render-thread caller can save immediately; anyone
	// else (e.g. the editor texture-preview panel on Main) enqueues and lets Tick() do it with the
	// GL context current. Same split as RequestTextureFromInfo.
	if (!IsOnMainThread()) {
		texture->SaveTexture(path);
		return;
	}
	mPendingTextureSaves.PushBack(PendingTextureSave{texture, path});
	NotifyFrameAvailable();
}

void Plu::RenderingManager::ProcessPendingTextureSaves()
{
	if (mPendingTextureSaves.IsEmpty()) return;

	Queue<PendingTextureSave> scratch; // local, see ProcessPendingTextureRequests
	mPendingTextureSaves.Drain(scratch);

	// Disk I/O with no lock held at all — SaveTexture only touches the texture it is handed.
	for (const PendingTextureSave& pending : scratch) {
		if (pending.TargetTexture) {
			pending.TargetTexture->SaveTexture(pending.SavePath);
		}
	}
}

Plu::TUsePointer<Plu::Texture> Plu::RenderingManager::GetTextureForInfo(const TUsePointer<TextureInfo>& textureInfo)
{
	if (!textureInfo) return nullptr;
	std::lock_guard<std::mutex> lock(mTextureMutex);
	if (mTextures.Contains(textureInfo->Uuid)) {
		mTextureUsePerFrame[textureInfo->Uuid]++;
		return mTextures[textureInfo->Uuid];
	}
	return nullptr;
}

void Plu::RenderingManager::UnloadTextureForUUID(UInt64 uuid)
{
	std::lock_guard<std::mutex> lock(mTextureMutex);
	UnloadTextureForUUID_NoLock(uuid);
}

void Plu::RenderingManager::UnloadTextureForUUID_NoLock(UInt64 uuid)
{
	if (!mTextures.Contains(uuid)) return;
	TOwningPointer<Texture> texture = mTextures[uuid];
	texture->Destroy();
	mApplicationInfo->AppObjectManager->DestroyObject(*texture->GetEngineObjectHandle());
	mTextures.Remove(uuid);
	mTextureUsePerFrame.Remove(uuid);
	mTextureFramesWithNoUse.Remove(uuid);
	PLU_CORE_INFO("Unloaded {}", uuid);
}

void Plu::RenderingManager::RequestStaticMeshLoad(PluUUID uuid)
{
	TUsePointer<EngineAssetManager> assetManager = mApplicationInfo->AppAssetManager;
	if (!assetManager) return;
	if (!assetManager->AssetExists(uuid)) return;
	TUsePointer<AssetDescriptor> assetDesc = assetManager->GetAssetDescriptor(uuid);
	if (!assetDesc->AssetType->IsDerivedOfOrSame(StaticMesh::GetStaticClass())) return;
	// Render thread: read cache only. If the CPU data isn't loaded yet, post a deferred load
	// request (drained on the main thread) and bail — we'll do the GL setup once it's available.
	TUsePointer<StaticMesh> staticMesh = assetManager->GetAssetDataNoLoad(uuid);
	if (!staticMesh) {
		assetManager->RequestAssetDataLoad(uuid);
		return;
	}
	if (staticMesh->IsLoaded) return;
	SetupStaticMeshGL(&staticMesh->StaticMeshData, staticMesh.GetRaw());
	mStaticMeshes.Insert(staticMesh->Uuid, staticMesh);
	PLU_CORE_INFO("Static Mesh {} Loaded!", staticMesh->Uuid.getUUID());
}

void Plu::RenderingManager::UnloadStaticMesh(PluUUID uuid)
{
	if (!mStaticMeshes.Contains(uuid)) return;
	CleanupStaticMeshGL(mStaticMeshes[uuid].GetRaw());
	mStaticMeshes.Remove(uuid);
	mStaticMeshFramesWithNoUse[uuid] = 0;
	PLU_CORE_INFO("Static Mesh {} Unloaded", uuid.getUUID());
}

void Plu::RenderingManager::RequestSkeletalMeshLoad(PluUUID uuid)
{
	TUsePointer<EngineAssetManager> assetManager = mApplicationInfo->AppAssetManager;
	if (!assetManager) return;
	if (!assetManager->AssetExists(uuid)) return;
	TUsePointer<AssetDescriptor> assetDesc = assetManager->GetAssetDescriptor(uuid);
	if (!assetDesc->AssetType->IsDerivedOfOrSame(SkeletalMesh::GetStaticClass())) return;
	// Render thread: read cache only. If the CPU data isn't loaded yet, post a deferred load
	// request (drained on the main thread) and bail — we'll do the GL setup once it's available.
	TUsePointer<SkeletalMesh> skeletalMesh = assetManager->GetAssetDataNoLoad(uuid);
	if (!skeletalMesh) {
		assetManager->RequestAssetDataLoad(uuid);
		return;
	}
	if (skeletalMesh->IsLoaded) return;
	SetupSkeletalMeshGL(&skeletalMesh->MeshData, skeletalMesh.GetRaw());
	mSkeletalMeshes.Insert(skeletalMesh->Uuid, skeletalMesh);
	PLU_CORE_INFO("Skeletal Mesh {} Loaded!", skeletalMesh->Uuid.getUUID());
}

void Plu::RenderingManager::UnloadSkeletalMesh(PluUUID uuid)
{
	if (!mSkeletalMeshes.Contains(uuid)) return;
	CleanupSkeletalMeshGL(mSkeletalMeshes[uuid].GetRaw());
	mSkeletalMeshes.Remove(uuid);
	mSkeletalMeshFramesWithNoUse[uuid] = 0;
	PLU_CORE_INFO("Skeletal Mesh {} Unloaded", uuid.getUUID());
}

Plu::TUsePointer<Plu::FrameBuffer> Plu::RenderingManager::RequestMainFrameBuffer()
{
	if (gIsRendererGut) {
		return gRenderer->GetMainFrameBuffer();
	}
	return nullptr;
}

void Plu::RenderingManager::RequestShadowCascadeView(Int32 layer)
{
	if (gIsRendererGut) {
		gRenderer->RequestShadowCascadeView(layer);
	}
}

Plu::TUsePointer<Plu::Texture> Plu::RenderingManager::GetShadowCascadeView()
{
	if (gIsRendererGut) {
		return gRenderer->GetShadowCascadeView();
	}
	return nullptr;
}

Int32 Plu::RenderingManager::GetShadowCascadeLayerCount() const
{
	if (gIsRendererGut) {
		return gRenderer->GetShadowCascadeLayerCount();
	}
	return 0;
}

void Plu::RenderingManager::RequestSpotShadowView(Int32 slot)
{
	if (gIsRendererGut) {
		gRenderer->RequestSpotShadowView(slot);
	}
}

Plu::TUsePointer<Plu::Texture> Plu::RenderingManager::GetSpotShadowView()
{
	if (gIsRendererGut) {
		return gRenderer->GetSpotShadowView();
	}
	return nullptr;
}

Int32 Plu::RenderingManager::GetSpotShadowSlotCount() const
{
	if (gIsRendererGut) {
		return gRenderer->GetSpotShadowSlotCount();
	}
	return 0;
}

void Plu::RenderingManager::BeginImGuiFrameSubmit()
{
	// Writer side of the triple buffer (Main thread). Reuse the slot's snapshot object
	// (its previous clones are freed in CopyFrom -> Clear), mirroring RenderSnapshotBuilder.
	ImGuiFrameSnapshot*& slot = mImguiTripleBuffer.GetWriteBuffer();
	if (slot == nullptr) {
		slot = new ImGuiFrameSnapshot();
	}
	slot->BeginWrite();
}

void Plu::RenderingManager::SubmitImGuiDrawData(UInt32 windowID, ImDrawData *drawData)
{
	if (!drawData) return;
	ImGuiFrameSnapshot* slot = mImguiTripleBuffer.GetWriteBuffer();
	if (!slot) {
		PLU_CORE_ERROR("SubmitImGuiDrawData without BeginImGuiFrameSubmit!");
		return;
	}
	slot->AddWindow(windowID, drawData);
}

void Plu::RenderingManager::EndImGuiFrameSubmit()
{
	ImGuiFrameSnapshot* slot = mImguiTripleBuffer.GetWriteBuffer();
	if (!slot) return;
	slot->EndWrite();
	mImguiTripleBuffer.Publish();
	NotifyFrameAvailable();
}

void Plu::RenderingManager::SetImGuiRenderingIgnorance(bool ignore)
{
	mSkipImGuiRendering.store(ignore);
}

bool Plu::RenderingManager::IsImGuiRenderingIgnored() const
{
	return mSkipImGuiRendering.load();
}

void Plu::RenderingManager::BeginImGuiLockstep()
{
	std::unique_lock<std::mutex> lock(mImGuiLockstepMtx);
	if (!mIsRendererRunning) return;
	mImGuiPassGranted = false;
	mImGuiPassDone = false;
	mImGuiLockstep.store(true, std::memory_order_release);
	// The render thread may be asleep (power saving) rather than on its way to the park point.
	NotifyFrameAvailable();
	// Wait until the render thread reaches its park point (finished any in-flight ImGui pass and is
	// no longer touching the atlas) before Main starts mutating it. The timeout is only a deadlock
	// safety net for the case where the render thread isn't rendering ImGui at all (no context /
	// rendering ignored) - then it never parks, but it also never touches the atlas, so proceeding
	// is safe. In the normal editor case it parks within one frame.
	mImGuiLockstepCv.wait_for(lock, std::chrono::milliseconds(250),
		[this] { return mImGuiRenderParked || !mIsRendererRunning; });
}

void Plu::RenderingManager::StepImGuiLockstep()
{
	std::unique_lock<std::mutex> lock(mImGuiLockstepMtx);
	if (!mIsRendererRunning || !mImGuiLockstep.load(std::memory_order_acquire)) return;
	mImGuiPassDone = false;
	mImGuiPassGranted = true;
	mImGuiLockstepCv.notify_all();
	// Block until the render thread completed exactly one ImGui pass (textures uploaded, draw data
	// submitted). Main must not mutate the atlas again until this returns. Timeout: deadlock safety
	// net (see BeginImGuiLockstep); harmless to proceed since a non-rendering render thread isn't
	// touching the atlas.
	mImGuiLockstepCv.wait_for(lock, std::chrono::milliseconds(250),
		[this] { return mImGuiPassDone || !mIsRendererRunning; });
}

void Plu::RenderingManager::EndImGuiLockstep()
{
	std::lock_guard<std::mutex> lock(mImGuiLockstepMtx);
	mImGuiLockstep.store(false, std::memory_order_release);
	mImGuiPassGranted = false;
	mImGuiLockstepCv.notify_all();
}

UInt32 Plu::RenderingManager::GetSnapshotDroppedCount() const
{
	return gTripleBuffer ? gTripleBuffer->GetDroppedSnapshotCount() : 0;
}

UInt32 Plu::RenderingManager::GetSnapshotReusedCount() const
{
	return gTripleBuffer ? gTripleBuffer->GetStaleFrameCount() : 0;
}

UInt32 Plu::RenderingManager::GetImGuiDroppedCount() const
{
	return mImguiTripleBuffer.GetDroppedSnapshotCount();
}

UInt32 Plu::RenderingManager::GetImGuiReusedCount() const
{
	return mImguiTripleBuffer.GetStaleFrameCount();
}

void Plu::RenderingManager::ResetTripleBufferTelemetry()
{
	if (gTripleBuffer) {
		gTripleBuffer->ResetTelemetry();
	}
	mImguiTripleBuffer.ResetTelemetry();
}

void Plu::RenderingManager::InitializeImGuiContext()
{
	if (!mApplicationInfo || !mApplicationInfo->AppWindow) {
		PLU_CORE_ERROR("InitializeImGuiContext called without a window!");
		return;
	}
	CreateImGuiContextForWindow(mApplicationInfo->AppWindow);
}

void Plu::RenderingManager::CreateImGuiContextForWindow(const TUsePointer<IWindow>& window)
{
	if (!window) return;
	const UInt32 windowID = window->GetWindowID();
	{
		std::lock_guard<std::mutex> lock(mImGuiStatesMutex);
		if (mImGuiStates.Contains(windowID)) return;
	}

	// Every window past the first shares window 0's font atlas: fonts are loaded once, and the
	// atlas-rebuild lockstep stays one concern instead of one per window.
	ImFontAtlas* sharedAtlas = nullptr;
	{
		std::lock_guard<std::mutex> lock(mImGuiStatesMutex);
		if (WindowImGuiState** main = mImGuiStates.Find(0u)) {
			ImGui::SetCurrentContext((*main)->State.GetContext());
			sharedAtlas = ImGui::GetIO().Fonts;
		}
	}

	WindowImGuiState* state = new WindowImGuiState();
	state->Window = window;
	ImGuiContext* ctx = state->State.CreateContext(window, sharedAtlas);
	window->SetImGuiContext(ctx);

	{
		std::lock_guard<std::mutex> lock(mImGuiStatesMutex);
		mImGuiStates.Insert(windowID, state);
		mWindowsNeedingGLBackend.PushBack(windowID);
	}
	NotifyFrameAvailable();

	// Leave the main window's context current — the rest of the frame (and every caller that does
	// not set the context itself) assumes it.
	if (windowID != 0 && mApplicationInfo->AppWindow) {
		ImGui::SetCurrentContext(mApplicationInfo->AppWindow->GetImGuiContext());
	}
}

void Plu::RenderingManager::RequestImGuiContextTeardown(UInt32 windowID)
{
	std::lock_guard<std::mutex> lock(mImGuiStatesMutex);
	if (mWindowsToTearDownGL.Contains(windowID)) return;
	if (!mImGuiStates.Contains(windowID)) {
		// Nothing on the render thread to unwind — report it safe straight away.
		if (!mWindowsSafeToDestroy.Contains(windowID)) mWindowsSafeToDestroy.PushBack(windowID);
		return;
	}
	mWindowsToTearDownGL.PushBack(windowID);
	NotifyFrameAvailable();
}

bool Plu::RenderingManager::IsImGuiContextTornDown(UInt32 windowID)
{
	std::lock_guard<std::mutex> lock(mImGuiStatesMutex);
	return mWindowsSafeToDestroy.Contains(windowID);
}

void Plu::RenderingManager::DestroyImGuiContextForWindow(UInt32 windowID)
{
	WindowImGuiState* state = nullptr;
	{
		std::lock_guard<std::mutex> lock(mImGuiStatesMutex);
		if (WindowImGuiState** found = mPendingDestroyStates.Find(windowID)) {
			state = *found;
		}
		mPendingDestroyStates.Remove(windowID);
		mWindowsSafeToDestroy.Remove(windowID);
	}
	if (!state) return;

	if (state->Window) state->Window->SetImGuiContext(nullptr);
	state->State.DestroyContext();
	delete state;

	// DestroyContext left no context current; put the main window's back.
	if (mApplicationInfo->AppWindow) {
		ImGui::SetCurrentContext(mApplicationInfo->AppWindow->GetImGuiContext());
	}
}

ImGuiContext* Plu::RenderingManager::GetImGuiContextForWindow(UInt32 windowID)
{
	std::lock_guard<std::mutex> lock(mImGuiStatesMutex);
	if (WindowImGuiState** found = mImGuiStates.Find(windowID)) {
		return (*found)->State.GetContext();
	}
	return nullptr;
}

void Plu::RenderingManager::Initialize(TripleBuffer<RenderSnapshot *> *tripleBuffer)
{
	mIsRendererRunning = true;
	gTripleBuffer = tripleBuffer;
	// Keep the thread joinable: shutdown synchronizes by join()ing it, which guarantees
	// RenderThreadExit() has fully torn down GL and released the context before Main touches
	// the window or destroys the SDL/GL context. Do NOT detach.
	mRenderThread = CreateOwning<std::thread>(&RenderingManager::RenderThreadEnter, this);
}

namespace
{
	// Jedno przejście księgowości użyć per typ assetu (dawniej 2 osobne pętle z potrójnymi
	// lookupami operator[] per wpis): bump/zerowanie licznika bezczynnych ticków + wyzerowanie
	// licznika użyć przez mutowalną referencję iteratora. Semantyka bez zmian: pierwszy
	// bezczynny tick wstawia licznik 0, użycie zeruje (liczymy *kolejne* bezczynne ticki —
	// uzasadnienie churnu tekstur panelu podglądu w komentarzu w RequestTextureFromInfo).
	void TickUseBookkeeping(Plu::HashMap<UInt64, int>& usePerFrame, Plu::HashMap<UInt64, int>& framesWithNoUse)
	{
		for (auto& entry : usePerFrame) {
			if (entry.second == 0) {
				if (int* idle = framesWithNoUse.Find(entry.first)) {
					(*idle)++;
				} else {
					framesWithNoUse.Insert(entry.first, 0);
				}
			} else {
				framesWithNoUse[entry.first] = 0;
				entry.second = 0;
			}
		}
	}
}

void Plu::RenderingManager::Tick(bool runUseBookkeeping)
{
	// Render thread. The Main->Render queues synchronize themselves, so draining them takes no
	// texture lock; only the GL load of each drained texture briefly re-takes mTextureMutex. The
	// per-frame use/eviction bookkeeping still runs under one lock hold, so off-thread Get/Request
	// calls never observe a half-mutated map mid-eviction. Unload via the _NoLock variant; the
	// public one re-locks.
	ProcessPendingTextureRequests();
	ProcessPendingTextureSaves();

	if (runUseBookkeeping) {
		std::lock_guard<std::mutex> lock(mTextureMutex);
		TickUseBookkeeping(mTextureUsePerFrame, mTextureFramesWithNoUse);
		for (const auto& textureIDp : mTextureFramesWithNoUse) {
			if (textureIDp.second > 100) {
				// Unload usuwa też wpis z mTextureFramesWithNoUse — bez ponownego zerowania
				// (dawne `[id] = 0` po unloadzie wskrzeszało osierocony wpis na zawsze).
				UnloadTextureForUUID_NoLock(textureIDp.first);
				break;
			}
		}
	}

	if (!runUseBookkeeping) return;

	//Meshes
	TickUseBookkeeping(mStaticMeshUsePerFrame, mStaticMeshFramesWithNoUse);
	for (const auto& mesh : mStaticMeshFramesWithNoUse) {
		if (mesh.second > 100) {
			UnloadStaticMesh(mesh.first); // zeruje licznik wewnątrz
			break;
		}
	}

	//Skeletal Meshes (mirror of the static-mesh eviction above)
	TickUseBookkeeping(mSkeletalMeshUsePerFrame, mSkeletalMeshFramesWithNoUse);
	for (const auto& mesh : mSkeletalMeshFramesWithNoUse) {
		if (mesh.second > 100) {
			UnloadSkeletalMesh(mesh.first); // zeruje licznik wewnątrz
			break;
		}
	}
}

void Plu::RenderingManager::Shutdown()
{
	PLU_CORE_TRACE("Rendering Manager Shutdown");
	mIsRendererRunning = false;
	// Wake the render thread if it's parked in the ImGui lockstep wait, so it can observe the stop
	// flag and exit instead of hanging the join below.
	{
		std::lock_guard<std::mutex> lock(mImGuiLockstepMtx);
		mImGuiLockstepCv.notify_all();
	}
	// Same for the power-saving idle wait.
	NotifyFrameAvailable();
	// Block until the render thread has run RenderThreadExit() to completion (GL torn down,
	// shader resources released, ReleaseGLContext()). After join the context is free, so it is
	// safe for Main to make it current and for Run() to delete the SDL/GL context afterwards.
	if (mRenderThread && mRenderThread->joinable()) mRenderThread->join();
	mApplicationInfo->AppWindow->MakeGLContextCurrent();

	// The render thread is gone and RenderThreadExit() already dropped every renderer backend, so
	// nothing can be drawing into a window anymore: hand all remaining ImGui contexts over to Main
	// so windows closed during shutdown still complete the teardown handshake.
	std::lock_guard<std::mutex> lock(mImGuiStatesMutex);
	mWindowsToTearDownGL.Clear();
	for (auto& state : mImGuiStates) {
		mPendingDestroyStates.Insert(state.first, state.second);
		if (!mWindowsSafeToDestroy.Contains(state.first)) mWindowsSafeToDestroy.PushBack(state.first);
	}
	mImGuiStates.Clear();
}

bool Plu::RenderingManager::IsLimitTextureLoadPerFrame() const
{
	return mLimitTextureLoadPerFrame.load();
}

void Plu::RenderingManager::SetLimitTextureLoadPerFrame(bool limit)
{
	mLimitTextureLoadPerFrame.store(limit);
}
