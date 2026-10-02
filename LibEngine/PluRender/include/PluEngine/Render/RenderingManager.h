//
// Created by Plutex on 2026-02-08.
//

#ifndef PLUENGINE_RENDERINGMANAGER_H
#define PLUENGINE_RENDERINGMANAGER_H

#include "PluEngine/Core.h"
#include "PluEngine/Core/Objects/EngineObject.h"
#include "RenderingManager.generated.h"
#include "PluEngine/PluUUID.h"
#include "PluEngine/Render/ImGuiRenderState.h"
#include "PluEngine/Render/OpenGLRenderState.h"
#include "PluEngine/Core/Threading/TripleBuffer.h"
#include "Concurrent/ConcurrentQueue.h"

#include <mutex>
#include <condition_variable>

struct ImDrawData;

#define MAX_TEXTURES_LOAD_PER_FRAME 1

namespace Plu
{
	class FrameBuffer;
	class IWindow;
	struct RenderSnapshot;
	struct ImGuiDrawSnapshot;
	struct ImGuiWindowDrawSnapshot;
	struct ImGuiFrameSnapshot;
	struct StaticMesh;
	struct SkeletalMesh;
	class Texture;
	struct TextureInfo;
	//One of the few managers that isn't Editor or Runtime.
	//This manager keeps track of all textures, shaders and loaded meshes
	//Mostly textures are cared about right now
	PLU_CLASS(Abstract)
	class PLURENDER_API RenderingManager final : public EngineObject
	{
		REFLECTION_BODY_RENDERINGMANAGER()
	private:
		ApplicationInfo* mApplicationInfo;

		HashMap<UInt64, TOwningPointer<Texture>> mTextures;
		HashMap<UInt64, int> mTextureFramesWithNoUse;
		HashMap<UInt64, int> mTextureUsePerFrame;

		// The texture maps above (and the GL texture objects they own) belong to the render thread:
		// they are created/uploaded and destroyed with the GL context current, and the per-frame
		// use/eviction bookkeeping runs in Tick() on the render thread. Off-thread callers (e.g. the
		// editor texture-preview panel, which queries textures from the Main thread) must NOT touch
		// GL or these maps directly. mTextureMutex guards every access to the texture maps; a
		// Main-thread RequestTextureFromInfo() only enqueues a TextureInfo, and the render thread
		// drains the queue + does the GL load in Tick(). See RequestTextureFromInfo.
		//
		// The two pending queues are NOT under mTextureMutex — they carry their own synchronization.
		// That shortens the mutex's hold time considerably: enqueuing used to mean taking the
		// texture mutex and running a full O(n) dedupe scan over the pending list while holding it.
		std::mutex mTextureMutex;
		ConcurrentQueue<TUsePointer<TextureInfo>> mPendingTextureRequests;

		// A read-back-to-disk request. The GL readback (glGetTexImage) needs the context current, so
		// like the load queue this is filled on any thread and drained by the render thread in Tick().
		struct PendingTextureSave
		{
			TUsePointer<Texture> TargetTexture;
			Path SavePath;
		};
		ConcurrentQueue<PendingTextureSave> mPendingTextureSaves;

		// Render-thread only. Performs the actual GL upload + map insert. Caller must hold mTextureMutex.
		void LoadTextureFromInfo_NoLock(const TUsePointer<TextureInfo>& textureInfo);
		// Render-thread only. GL delete + map removal. Caller must hold mTextureMutex.
		void UnloadTextureForUUID_NoLock(UInt64 uuid);
		// Render-thread only. Drains mPendingTextureRequests and loads each texture. Takes
		// mTextureMutex itself, per load — the drain needs no lock at all.
		void ProcessPendingTextureRequests();
		// Render-thread only. Drains mPendingTextureSaves, writing each texture to disk. Needs no
		// lock: SaveTexture only touches the texture object it is handed.
		void ProcessPendingTextureSaves();

		HashMap<UInt64, TUsePointer<StaticMesh>> mStaticMeshes;
		HashMap<UInt64, int> mStaticMeshUsePerFrame;
		HashMap<UInt64, int> mStaticMeshFramesWithNoUse;

		HashMap<UInt64, TUsePointer<SkeletalMesh>> mSkeletalMeshes;
		HashMap<UInt64, int> mSkeletalMeshUsePerFrame;
		HashMap<UInt64, int> mSkeletalMeshFramesWithNoUse;

		TOwningPointer<std::thread> mRenderThread;
		std::atomic<bool> mIsRendererRunning = false;
		std::atomic<bool> mSkipImGuiRendering = false;

		// Stan kontekstów renderowania, którym zarządza ten manager:
		// - mImGuiStates: kontekst ImGui per okno + styl (Main, CreateImGuiContextForWindow)
		//   i backend OpenGL3 (render thread, ProcessImGuiBackendQueues).
		// - mGLState: domyślny stan GL (depth test, blending) — per-kontekst, więc odpalany na
		//   render threadzie zaraz po MakeGLContextCurrent().
		struct WindowImGuiState
		{
			ImGuiRenderState State;
			// Non-owning: the window outlives every render-thread use of this entry because the
			// teardown handshake below removes the entry before Main is allowed to destroy it.
			TUsePointer<IWindow> Window;
		};
		// Heap entries, so the render thread may hold one while working outside the mutex.
		HashMap<UInt32, WindowImGuiState*> mImGuiStates;
		std::mutex mImGuiStatesMutex;

		// Window lifecycle handshake with the render thread. An ImGui context's renderer backend
		// (ImGui_ImplOpenGL3_*) lives in that context's io.BackendRendererUserData and needs the GL
		// context current, so both its creation and its teardown have to happen over here — while
		// the window object itself may only be destroyed by Main. Hence three queues:
		//   Main -> Render: "context created, init its GL backend"      (mWindowsNeedingGLBackend)
		//   Main -> Render: "stop drawing this window, drop its backend" (mWindowsToTearDownGL)
		//   Render -> Main: "done, the window is yours to destroy"       (mWindowsSafeToDestroy)
		DynamicArray<UInt32> mWindowsNeedingGLBackend;
		DynamicArray<UInt32> mWindowsToTearDownGL;
		DynamicArray<UInt32> mWindowsSafeToDestroy;
		// Entries the render thread is done with, waiting for Main to destroy the ImGui context.
		HashMap<UInt32, WindowImGuiState*> mPendingDestroyStates;

		OpenGLRenderState mGLState;

		// Main->Render handoff of one ImGui frame — all windows of that frame together, so the
		// render thread never mixes window A of frame N with window B of frame N-1. Writer is the
		// Main thread via Begin/Submit/EndImGuiFrameSubmit(), reader is the render thread loop.
		// Same lock-free triple-buffer pattern as the RenderSnapshot path.
		TripleBuffer<ImGuiFrameSnapshot*> mImguiTripleBuffer;

		// Swap interval currently applied to the shared GL context (render thread only) — see
		// RenderThreadLoop, where secondary windows swap without vsync.
		bool mSwapIntervalVSync = true;

		// Render thread. Drains mWindowsNeedingGLBackend / mWindowsToTearDownGL.
		void ProcessImGuiBackendQueues();
		// Render thread. Draws one window's ImGui snapshot and presents that window.
		void RenderImGuiWindow(const ImGuiWindowDrawSnapshot* entry, bool isMainWindow, bool* outRendered);

		// ImGui font-atlas rebuild lockstep. The lock-free ImGui handoff only deep-copies draw
		// lists, not texture state - the snapshot just references the live ImTextureData/ImFontAtlas
		// owned by the Main thread. As long as the atlas is static every texture sits at
		// ImTextureStatus_OK and neither thread mutates it, so the render thread can read it freely.
		// A font-size change rebuilds the atlas (creates a new texture, destroys the old one), and
		// then both threads touch the same texture objects at once -> ImGui asserts in the
		// create/destroy paths (imgui_draw.cpp). While such a rebuild is in flight the Main thread
		// drives the handoff in lockstep via Begin/Step/EndImGuiLockstep() so that only one thread
		// ever touches the atlas at a time. Steady state cost is a single atomic load per render loop.
		std::atomic<bool> mImGuiLockstep = false;
		std::mutex mImGuiLockstepMtx;
		std::condition_variable mImGuiLockstepCv;
		bool mImGuiPassGranted = false;   // Main -> Render: you may run exactly one ImGui pass.
		bool mImGuiPassDone = false;      // Render -> Main: that pass is complete.
		bool mImGuiRenderParked = false;  // Render -> Main: parked, not touching the atlas.

		// Power saving (FrameDemand.h): where the render thread sleeps after an iteration that had
		// nothing to present. mFrameWorkPending is guarded by the mutex and set by
		// NotifyFrameAvailable(), so a notify that lands just before the wait is not lost.
		std::mutex mFrameWorkMtx;
		std::condition_variable mFrameWorkCv;
		bool mFrameWorkPending = false;
		void WaitForFrameWork();

		void RenderThreadEnter();
		// Returns false when the iteration presented nothing (power saving, nothing new).
		bool RenderThreadLoop();
		void RenderThreadExit();

		friend PLURENDER_API void DrawStaticMesh(const StaticMesh* staticMesh, RenderingManager* renderingManager);
		friend PLURENDER_API void DrawStaticMeshInstanced(const StaticMesh* staticMesh, RenderingManager* renderingManager, UInt32 instanceCount);
		void OnStaticMeshRender(StaticMesh *staticMesh);

		friend PLURENDER_API void DrawSkeletalMesh(const SkeletalMesh* skeletalMesh, RenderingManager* renderingManager);
		void OnSkeletalMeshRender(SkeletalMesh *skeletalMesh);

		//FUN
		std::atomic<bool> mLimitTextureLoadPerFrame = false;
	public:
		RenderingManager(ApplicationInfo* applicationInfo);
		virtual ~RenderingManager() override;

		//Requesting Resources
		void RequestTextureFromInfo(const TUsePointer<TextureInfo>& textureInfo);
		TUsePointer<Texture> GetTextureForInfo(const TUsePointer<TextureInfo>& textureInfo);
		void UnloadTextureForUUID(UInt64 uuid);

		// Enqueue a texture read-back to a PNG on disk. Safe to call from any thread: the actual
		// glGetTexImage + write happens on the render thread (with the GL context current) in Tick().
		void RequestTextureSave(const TUsePointer<Texture>& texture, const Path& path);

		void RequestStaticMeshLoad(PluUUID uuid);
		void UnloadStaticMesh(PluUUID uuid);

		void RequestSkeletalMeshLoad(PluUUID uuid);
		void UnloadSkeletalMesh(PluUUID uuid);

		TUsePointer<FrameBuffer> RequestMainFrameBuffer();

		// Debug view of one directional shadow cascade layer (RenderGpuStatsPanel). The array
		// texture itself is not displayable by the ImGui backend (it only binds GL_TEXTURE_2D), so
		// the render thread copies the requested layer into a plain 2D depth texture. Call Request
		// every frame the viewer is open (-1 to stop); Get returns last frame's copy, or null.
		void RequestShadowCascadeView(Int32 layer);
		TUsePointer<Texture> GetShadowCascadeView();
		[[nodiscard]] Int32 GetShadowCascadeLayerCount() const;

		// Same viewer plumbing for a slot of the spot shadow atlas.
		void RequestSpotShadowView(Int32 slot);
		TUsePointer<Texture> GetSpotShadowView();
		[[nodiscard]] Int32 GetSpotShadowSlotCount() const;

		// Main thread, one frame's worth of ImGui for every window, in this order:
		//   BeginImGuiFrameSubmit();
		//   for each window: build the frame, then SubmitImGuiDrawData(id, ImGui::GetDrawData());
		//   EndImGuiFrameSubmit();
		// Submit deep-copies the live draw data (its ImDrawLists are recycled by the next
		// NewFrame()); End publishes the whole frame to the render thread. The engine does not
		// drive ImGui::NewFrame()/Render() itself — the app owns its ImGui frames.
		void BeginImGuiFrameSubmit();
		void SubmitImGuiDrawData(UInt32 windowID, ImDrawData* drawData);
		void EndImGuiFrameSubmit();

		// Any thread. Tells a render thread that is asleep under power saving that there is
		// something for it: a published snapshot (scene or ImGui) or a queued Request*. Callers
		// that publish through this manager already do it; call it after publishing a
		// RenderSnapshot, which the manager does not see.
		void NotifyFrameAvailable();

		void SetImGuiRenderingIgnorance(bool ignore);
		bool IsImGuiRenderingIgnored() const;

		// ImGui font-atlas rebuild lockstep (call from the Main thread, around the ImGui frame that
		// rebuilds the atlas - e.g. a font-size change). Begin parks the render thread's ImGui
		// section so Main can mutate the atlas exclusively; Step grants exactly one render-thread
		// ImGui pass (texture upload + draw) and blocks until it finishes; End resumes the normal
		// lock-free free-running handoff. Stay in lockstep (Begin/build/Step each frame) until the
		// atlas has settled back to all-OK, then call End. No-ops are safe if the render thread is
		// not currently rendering ImGui. See mImGuiLockstep.
		void BeginImGuiLockstep();
		void StepImGuiLockstep();
		void EndImGuiLockstep();

		// TripleBuffer telemetry (thread-safe, callable from any thread). Cumulative since start
		// (or last reset). "Dropped" = published frames the consumer never picked up (producer
		// outran consumer). "Reused" = the consumer re-used the previous frame because nothing
		// new was published (consumer outran producer). Scene = Main->Render RenderSnapshot path;
		// ImGui = Main->Render draw-data path.
		[[nodiscard]] UInt32 GetSnapshotDroppedCount() const;
		[[nodiscard]] UInt32 GetSnapshotReusedCount() const;
		[[nodiscard]] UInt32 GetImGuiDroppedCount() const;
		[[nodiscard]] UInt32 GetImGuiReusedCount() const;
		void ResetTripleBufferTelemetry();

		// Main thread, po IWindow::Init(): tworzy kontekst ImGui (IO/DPI/styl + backend
		// platformowy) i wpina go w okno. Backend OpenGL3 dochodzi później na render
		// threadzie — patrz RenderThreadEnter(). Skrót na okno główne.
		void InitializeImGuiContext();

		// Main thread. Same thing for any window: creates its ImGui context (sharing the main
		// window's font atlas), stores it on the window, and asks the render thread to bring up its
		// GL backend. Safe to call while the render thread is running.
		void CreateImGuiContextForWindow(const TUsePointer<IWindow>& window);

		// Main thread, window teardown handshake (see mWindowsToTearDownGL):
		//   1. stop building ImGui frames for the window,
		//   2. RequestImGuiContextTeardown(id),
		//   3. poll IsImGuiContextTornDown(id) — until it is true the render thread may still be
		//      drawing into that window, so it must not be destroyed,
		//   4. DestroyImGuiContextForWindow(id), then destroy the window itself.
		// A window that never had an ImGui context reports torn down immediately.
		void RequestImGuiContextTeardown(UInt32 windowID);
		bool IsImGuiContextTornDown(UInt32 windowID);
		void DestroyImGuiContextForWindow(UInt32 windowID);

		[[nodiscard]] ImGuiContext* GetImGuiContextForWindow(UInt32 windowID);

		void Initialize(TripleBuffer<RenderSnapshot*>* tripleBuffer);
		// runUseBookkeeping = false, gdy ta klatka NIE renderuje sceny (stale snapshot — patrz
		// RenderThreadLoop): liczniki użyć assetów nie dostają wtedy bumpów z draw calli, więc
		// liczenie im "bezczynnych ticków" eksmitowałoby assety podczas dłuższego stalla maina
		// (breakpoint, modalny dialog). Kolejki pending (tekstury/zapisy) są drenowane zawsze.
		void Tick(bool runUseBookkeeping = true);
		void Shutdown();

		//FUN
		bool IsLimitTextureLoadPerFrame() const;
		void SetLimitTextureLoadPerFrame(bool limit);
	};
}

#endif //PLUENGINE_RENDERINGMANAGER_H
