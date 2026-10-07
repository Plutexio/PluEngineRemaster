# MULTITHREADING.md

Dokumentacja architektury wielowątkowej PluEngine — ściąga do pracy przy wątkach.
Stan: pipeline TripleBuffer+RenderSnapshot (dawny „etap 2") wdrożony i zweryfikowany.

## Architektura w pigułce

Dwa wątki:

| | **Wątek main** | **Wątek renderu** |
|---|---|---|
| Robi | input, logika, scena, fizyka, ImGui (budowa klatki), I/O assetów | GL + particle simulation (see RenderSnapshot below) |
| Właściciel | `EngineObjectManager`-mutacje logiki, rejestr `EngineAssetManager`, `SceneWorld`, Input, Python, ImGuiContext (strona platformowa) | **kontekst GL**, FBO, tekstury, ShaderProgramy, VAO/VBO, backend ImGui OpenGL3 |
| Komunikacja → | `TripleBuffer<RenderSnapshot*>` + `TripleBuffer<ImGuiFrameSnapshot*>` + kolejki Request* | `RequestAssetDataLoad` (prośba o I/O na main) |

Kluczowy fakt: **main NIE ma kontekstu GL** — `Application::Run` woła `AppWindow->ReleaseGLContext()`
tuż po init (Application.cpp ~91). Wywołanie GL z maina to cichy no-op (puste/czarne wyniki, zero błędów).

`Renderer` NIE jest EngineObjectem — to zwykła klasa, żyje jako `static gRenderer` tworzony
i niszczony w całości na wątku renderu (`RenderingManager::RenderThreadEnter/Exit`).

## Przepływ klatki

**Main** (`Application::Run`, kolejność faktyczna):
1. Input Update
2. App OnTick (edytor: budowa klatki ImGui **każdego okna** + `Begin/Submit/EndImGuiFrameSubmit`)
3. Scenes Update (logika + fizyka)
4. `EngineAssetManager::ProcessPendingLoads()` — drenaż żądań I/O z renderu
5. `RenderSnapshotBuilder::BuildSnapshotAndPublish(dt)` — ekstrakcja stanu sceny do POD + Publish
6. Input EndFrame, frame pacing

**Render** (`RenderingManager::RenderThreadLoop`):
1. `GPUProfileScope::PollResults()` — odbiór async wyników GPU timerów z poprzednich klatek
2. `mSceneTripleBuffer->AcquireReadBuffer(&fresh)` — z flagą świeżości (patrz TripleBuffer niżej)
3. `Tick(fresh)` — drenaż kolejek Request* (tekstury/save'y) ZAWSZE; bookkeeping eviction tylko
   przy świeżym snapshotcie (patrz „Liczniki eviction" niżej)
4. **Tylko gdy snapshot świeży**: `gRenderer->RenderSnapshot(snapshot)`
   (palety skinningu → particle spawner sync + particle tick → pass cieni CSM → pass cieni spotów →
   depth prepass → pass główny (incl. particles) → editor grid → debug geometry → post-process (empty stub)).
   The particle tick runs before the shadow passes because they draw the same particle buffers.
   `RenderSnapshot` takes the time since the previous `RenderSnapshot` call (not the render-loop
   frame delta — skipped stale frames must not be lost), used by the particle tick and the
   `time` shader uniform. Depth prepass jest w całości render-thread'owy jak reszta:
   rysuje tę samą geometrię ze snapshotu, a jego tekstura głębi zasila contact shadows
   (early-Z świadomie NIE — patrz `HELPERS.md`).
   Stale snapshot (main nie opublikował nowego) = identyczne dane wejściowe, a FBO sceny trzyma
   poprzedni obraz — scena **nie jest re-renderowana** (oszczędność GPU, gdy render wyprzedza main).
5. `AcquireReadBuffer()` ImGui → pętla po oknach klatki: `MakeGLContextCurrent` +
   `SetCurrentContext` + `ImGui_ImplOpenGL3_NewFrame` → `RenderDrawData` → swap
6. Blit FBO sceny na ekran (gdy ImGui nie renderował) + SwapBuffer

Steps 5-6 (the present) run every iteration with power saving off: the backbuffer is undefined after a
swap, so a loop that swaps has to redraw. With power saving on they run only when the iteration was
handed something new — see "Power saving" below.

## Power saving (frames on demand)

API and the rule for UI code are in `HELPERS.md` ("Power saving / frames on demand"); this is the thread side.
Enabled only by the editor (`EditorSettings::PowerSaving`), bypassed while `WantsContinuousFrames()` (PIE).

**Main** (`Application::Run`). At the top of an iteration `ConsumeFrameDemand()` says whether a frame is
wanted. If not, Main blocks in `SDLWindow::WaitForEvents` / `WindowsWindow::WaitForEvents` for at most
`kIdleHeartbeatSeconds` (0.25 s), pumps events (each one calls `RequestRedraw()`), and asks again. Still
nothing: an **idle tick** — `ProcessPendingWindows`, `OnIdleTick`, `ProcessPendingLoads`,
`ProcessClosingWindows` — and back to sleep. No `OnTick`, no scene update, no `BuildSnapshotAndPublish`,
so nothing is published and the render thread stays asleep. The first frame after a wait gets a fixed
delta (`kFrameDeltaAfterIdle`), not the length of the nap.

**Probe frames.** Mouse motion asks for frames through `RequestProbe()` instead of `RequestRedraw()`.
A frame wanted only by a probe (`FrameDemand::MustPresent == false`, `IsProbeFrame()`) runs the whole
Main tick, but publishes less: `Application::Run` calls `RenderSnapshotBuilder::DiscardFrame()` instead of
`BuildSnapshotAndPublish` (no scene re-render; the per-frame debug-line buffers the tick filled are
dropped, since nothing drains them otherwise), and `PluEditor::OnTick` hashes every window's `ImDrawData`
(`HashImGuiDrawData`) and submits + publishes the ImGui frame only if the hash differs from the last one
published. Submission is deferred until after all windows are built — a context's draw data stays valid
until its own next `NewFrame()` — so an unchanged probe frame skips the deep copy too. Lockstep frames are
always published. The hash reads the raw `ImTextureRef`, never `GetTexID()`: that asserts on Main for a
texture the render thread has not uploaded yet. Net effect: moving the pointer over an idle UI costs an
ImGui build on Main and nothing on the render thread; a hover highlight or tooltip is a changed hash and
gets presented.

**Render** (`RenderingManager::RenderThreadLoop`). Housekeeping always runs: `PollResults`, `Tick()`
(request queues), `ProcessImGuiBackendQueues`, the FBO resize check. The present runs only when
`shouldPresent`: a scene snapshot was rendered (fresh, or the FBO was invalidated by a resize), the ImGui
snapshot is fresh, or a lockstep pass was granted. Not swapping leaves the last image on screen; damage
(expose, resize) reaches Main as an OS event and comes back as a fresh ImGui frame. An iteration that
presented nothing sleeps in `WaitForFrameWork()` — a condvar with a 50 ms timeout.

**Wake-ups, both directions:**

| Direction | Mechanism | Sent by |
|---|---|---|
| main → render | `RenderingManager::NotifyFrameAvailable()` (condvar + flag under mutex, so a notify just before the wait is not lost) | `EndImGuiFrameSubmit`, `Application::Run` after `BuildSnapshotAndPublish`, `RequestTextureFromInfo` / `RequestTextureSave` enqueue, window backend queues, `BeginImGuiLockstep`, `Shutdown` |
| render → main | `RequestRedraw()` off the main thread → wake callback → `SDL_PushEvent(SDL_EVENT_USER)` / `PostThreadMessage(WM_NULL)` | texture loads finished in `ProcessPendingTextureRequests`, `EngineAssetManager::RequestAssetDataLoad` |

The wake event itself does not count as activity (it would re-arm the settle window forever); whoever
pushed it already stated what it needs. Off-thread wakes are collapsed by a flag that
`ConsumeFrameDemand()` re-arms.

**Consequences to keep in mind:**
- **Frozen panel = nobody asked for a frame.** Particles, animation previews, stats: they only advance
  on frames, and frames only come on demand. See the rule in `HELPERS.md`.
- `GetRenderThreadFPS()` is the time between **presents**, and gaps over 0.1 s are not published. Main's
  frame pacing divides by it — an idle render thread must not be able to stretch Main's sleep (which is
  additionally clamped to 50 ms).
- The lockstep (`BeginImGuiLockstep`) notifies the idle condvar: a sleeping render thread would otherwise
  only reach its park point at the next 50 ms timeout. A granted pass presents even on a stale snapshot.
- The window teardown handshake keeps working while idle: the render thread drains its queues every
  50 ms, Main polls `ProcessClosingWindows` on every idle tick.
- File watchers (`CheckForShaderChanges` / `CheckForScriptsChanges`) are polled on a 1.5 s timer from
  both `OnTick` and `OnIdleTick` — a frame counter would stop with the frames.
- Noisy input devices (a drifting gamepad axis) produce a steady stream of OS events and keep the loop
  awake. Not filtered today.

## Klocki

### ThreadAffinity (`Threading/ThreadAffinity.h`)
`RegisterMainThread()` (1. linia EngineInit) / `IsOnMainThread()` / `GetMainThreadId()`.
`IsOnMainThread()` zwraca `true` przed rejestracją — pre-init i narzędzia standalone nie strzelają.
Konwencja detekcji wątku w kodzie silnika: `if (!IsOnMainThread())` = „jestem na renderze".
(Osobnego `IsOnRenderThread()` już NIE ma — usunięty razem z lockstepem.)

Nazwy wątków (diagnostyka, NIE affinity): `RegisterThreadName(name)` / `GetCurrentThreadName()`.
`RegisterMainThread()` nazywa maina `"Main"`, `RenderThreadEnter()` nazywa render thread `"Render"`.
Nazwa jest thread-local i służy `Profilerowi` do rozdzielania wpisów per wątek (panel Profiler ma
filtr po wątku; GPU timery lądują pod pseudo-wątkiem `"GPU"`). Dokładając nowy wątek, który cokolwiek
profiluje, zawołaj `RegisterThreadName` na jego wejściu — inaczej pokaże się jako `Thread <id>`.
Rejestr `Profilera` to `ConcurrentHashMap<String, ProfilerEntry>` z kluczem `"wątek|nazwa"` —
paskowanie po kluczu sprawia, że timery maina i renderu lądują na różnych paskach i praktycznie
przestają ze sobą konkurować (wcześniej jeden `std::mutex` obejmował i `Record()` z obu wątków,
i pełną kopię mapy w `Snapshot()` co klatkę UI).

### TripleBuffer (`Threading/TripleBuffer.h`)
Lock-free, klasyczny algorytm 3-slotowy. Writer (main): `GetWriteBuffer()` → wypełnij → `Publish()`.
Reader (render): `AcquireReadBuffer(bool* outFresh = nullptr)` — nigdy nie blokuje, przy braku
nowych danych oddaje poprzedni bufor; `outFresh` mówi, czy to nowo opublikowany snapshot (render
thread pomija re-render sceny dla stale'a — patrz „Przepływ klatki").
Telemetria: dropped (main wyprzedza render) / stale-reused (render wyprzedza main),
wystawiona przez `RenderingManager::GetSnapshot*/GetImGui*Count()`, reset `ResetTripleBufferTelemetry()`.
Teardown: gdy `T` jest wskaźnikiem owning (np. `ImGuiFrameSnapshot*`), sloty alokowane leniwie trzeba
zwolnić po zjoinowaniu render threadu przez `GetBuffersForTeardown()` — robi to `~RenderingManager()`.

### RenderSnapshot (`Renderer/RenderThreading.h`)
POD-owy stan klatki: `SkeletalMeshRenderObjects` (UUID mesha/materiału + transform + paleta kości;
static meshe idą WYŁĄCZNIE przez batche instancingu poniżej — płaska lista
`StaticMeshRenderObjects` została usunięta po fazie 3), `DirLight`+`HasDirLight`, `SpotLights`,
kamera (projekcja, lokacja, rotacja, **`CameraFOV`** — CSM liczy pod-frustumy per-kaskada, sama
projekcja nie wystarcza), debug geometry fizyki (`DebugLineVerts`/`DebugPointVerts` — płaskie
bufory interleaved pos(3)+color(3)), and the two view-only editor toggles copied on MAIN from
`SceneWorld` fields: `ShowEditorGrid` (drawn on the render thread by `Renderer::RenderEditorGrid`
as an attribute-less fullscreen pass, editor build only) and `ShowShadowCascades` (forwarded into
`ShadowData::DebugVisualizeCascades`). It also carries `SceneHandle` (the `EngineObjectHandle` of
the world it was built from) and the particle spawner states described below.
Physics debug geometry no longer goes through the builder's own Jolt extraction: `PhysicsWorld::OnUpdate`
(main) packs its wireframe/point renderers into the frame buffers behind `SceneWorld::GetDebugDraw()`
(`GetFrameLineBuffer/GetFramePointBuffer`), and the builder appends those buffers to
`DebugLineVerts`/`DebugPointVerts` and clears them (see the debug draw channel below).
Budowany przez `RenderSnapshotBuilder::BuildSnapshotAndPublish` (main); render dostaje same
UUID-y i **rozwiązuje zasoby po swojej stronie** (leniwie, patrz niżej).

**Debug draw channel (main → render).** Every debug draw of a world goes through its
`DebugDrawAdapter` (`SceneWorld::GetDebugDraw()`, `Gameplay/Debug/DebugDrawAdapter.h`), which writes
into the world's two per-frame buffers `mDebugLineVerts`/`mDebugPointVerts` (interleaved
pos(3)+color(3)). They are **main-owned, view-only, not serialized**. Writers, all on main: gameplay
code and Python scripts (`world.GetDebugDraw().DrawDebugSphere(...)`), `PhysicsWorld::OnUpdate`
(bulk-packs its Jolt debug renderers), and the editor tick (`SceneViewport::DrawSelectedSpotLightGizmo`,
`DrawSelectedParticleSpawnerGizmos`, `ParticlesDebugPanel` bounds). `RenderSnapshotBuilder` first calls
`DebugDrawAdapter::EmitPersistentDraws(deltaTime)` — draws with a `Duration` live in the adapter's own
batches and are re-appended to the buffers every frame until they expire — then **drains** both buffers
into `snapshot->DebugLineVerts/DebugPointVerts`. Main-loop ordering makes this safe: the editor's
`OnTick` runs before `BuildSnapshotAndPublish` (`Application.cpp`). Drained, not copied — without
clearing, the buffers would grow by a frame's worth of geometry forever. `DiscardFrame` (probe frames)
clears only the per-frame buffers; persistent batches survive and do not age on such frames.
Editor gizmos draw from **one** place per frame: they live in `SceneViewport`, not in
`SceneViewportPanel`, because the panel is drawn once per hosting window. (This replaced the
editor-only `SceneWorld::EditorDebugLineVerts` channel.)

**Particles are simulated on the render thread** — the one exception to "render only draws". Main
never touches a `ParticleSpawner`. The channel is **state, not events**: `SceneWorld` keeps the live
`ParticleSpawnerComponent`s in `mParticleSpawnerComponents` (removed on component delete and on object
destroy), and `RenderSnapshotBuilder` packs a full `ParticleSpawnerRenderObject` for each of them into
**every** snapshot — UUID, a copy of the `ParticleClass`, world location, launch direction (the
component's forward vector), and the component's cumulative `RequestedParticles` counter plus
`LastBurstSize`. `ParticleSpawnerComponent::SpawnParticles` only bumps that counter.
`Renderer::SyncParticleSpawners` reconciles `mParticleSpawners[SceneHandle]` against the list: creates
missing spawners through `AppObjectManager` (render-owned, `owningThread` = render), pushes class and
transform into existing ones every frame (live edits, moving spawners), destroys the ones no longer
listed, and spawns `RequestedParticles - already synced` particles. One-shot requests were dropped
deliberately: the `TripleBuffer` discards snapshots when render falls behind (a request in one is lost)
and re-renders a stale snapshot after a resize (a request in it runs twice); a counter diff is immune to
both. Particles live in world space — moving the spawner moves only where new ones start.
Remaining consequences: spawners are ticked only when a snapshot is actually rendered (a stale snapshot
is skipped, so particles freeze while main does not publish); gameplay cannot read particle state back;
and spawners of a world that stops publishing snapshots (e.g. the PIE world after PIE ends) are only
reconciled when that world publishes again, so they live until shutdown.

Drawing needs no handoff either, since the simulation already sits next to the GL context: each spawner in
`Renderer::mParticleSpawners` is paired with a `ParticlePointBuffer`. `TickParticleSpawners` ticks and
uploads the spawner's positions (`ParticleSpawner::GetPositions`, already in vertex layout) straight into
it, and `RenderParticles` draws them as points into the main buffer — outside the editor-only block, so
the runtime draws particles too. Particles no longer go through `DebugPointVerts`.

The same buffers feed the shadow maps: `DrawParticleShadowCasters` draws every spawner whose
`ParticleClass::ShadowMode` casts into each cascade and spot slot (`ParticleShadow.vert`, point size from
`ShadowSize` in metres), which is why the tick sits before the shadow passes rather than next to
`RenderParticles`. Receiving is done in `ParticlePoint.vert` from the cascade atlas and the `ShadowData`
block already bound for the main pass — no extra handoff, all of it on the render thread.

The one way back is debug-only: the **Debug Particles** panel calls `RequestParticleDebugStats()` every
frame it is drawn (atomic flag), and `Renderer::RenderSnapshot`, right after the particle tick, consumes
the request and publishes a `ParticleDebugStats` copy (per-spawner `ParticleSpawner::GatherDebugStats`)
under a mutex in `RenderParticleStats.cpp`; the panel reads it with `GetParticleDebugStats()` and joins it
with the components by UUID. Not a gameplay channel — it is a frame behind, published only while
requested, at most every 0.1 s (the gather walks every particle), and only for fresh snapshots.
Asset-driven spawners ride the same publish as `ParticleDebugStats::SystemSpawners` (render-side lifecycle
counters, the parameter block the emitters ran with, per-emitter storage / memory / bounds), gathered by
`Renderer::GatherParticleSystemDebugStats`. Per-op times need a second renew-every-frame flag,
`RequestParticleOpTimings()`, consumed once per particle tick: only that tick is timed.

**Particle systems (asset-driven spawners).** A `ParticleSpawnerComponent` with a `ParticleSystemAsset` takes a
second path through the same channel; without an asset it is the legacy spawner above, untouched.
Everything below is written in English on purpose (repo rule for new sections).

- *Program transport.* Main compiles the asset (`ParticleSystemCompiler::GetCompiled`, cached by uuid +
  `ParticleSystem::CompileRevision`) and hands render the flat `CompiledParticleSystem`, never the asset graph
  (`TOwningPointer`s are owner-thread-checked, and `operator->` asserts on the wrong thread). Copying it per spawner
  per frame is not an option (KB of LUTs x N spawners x 60 Hz), so `RenderSnapshot::ParticleSystems` is a
  **resident cache inside each of the three TripleBuffer slots**: `RenderSnapshotBuilder` does a linear find by
  `SystemUuid`, appends or overwrites an entry only when its `Revision` differs, and drops entries no spawner used
  this frame. `RenderSnapshot::Clear()` deliberately does NOT clear it (and does clear `ParticleParameterValues`,
  which is per-frame data) — both need their comment, or the next person "fixes" one of them.
  **Invariant:** every published snapshot holds a superset of the systems its `ParticleSpawners` name, at the
  revisions they name. It holds because each slot is written only by main and read by render only after
  `Publish()`: a dropped snapshot loses nothing (data stays in the slot) and a replayed old one is self-sufficient.
  Steady-state cost: zero copies. Render (`SyncParticleSystemSpawner`) copies a `CompiledEmitter` into its
  `ParticleEmitterInstance` only when the revision changes and must never keep a pointer into the snapshot array
  across frames (main overwrites the slot). Emitters are matched by `EmitterUuid`, so adding/removing/reordering an
  emitter keeps the survivors' particles.
- *Parameters.* Live values live in main's `ParticleSystemInstance` (`ParticleParameterStore`); the builder writes
  them as one flat float block per spawner into `ParticleParameterValues` (O(parameters) per spawner per frame,
  layout order = `CompiledParticleSystem::ParameterLayout`). Render tolerates a count that disagrees with the
  program for a frame (recompile landed between block write and program adoption): missing indices fall back to
  `ParameterDefaults`.
- *Lifecycle, main -> render (state, never impulses).* `EmissionState` (Stopped/Playing/Paused) plus two
  monotonic counters: `ActivationVersion` (`Play()`: restart) and `ClearVersion` (`Stop()`: clear now).
  `Deactivate`, `Pause` and `Resume` bump neither. Render compares them with what it last synced: an activation
  change resets the emitters and **rebases `SyncedRequested`** onto the incoming counter (a burst requested before
  `Play()` must not fire again), a clear change only drops the particles. Paused ticks with dt = 0; Stopped ticks
  normally without emitting. `SpawnParticles(n)` still works (counter diff) and adds n to every emitter.
- *Feedback, render -> main.* `RenderParticleLiveness.h`: always on, O(spawners), one mutex + one copy (same
  mechanism as `RenderParticleStats`, but no request flag, no throttle, no per-particle gather).
  `TickParticleSpawners` publishes `{alive, emitting emitters, CompletedActivationVersion}` per spawner every tick.
  `CompletedActivationVersion` is monotonic, so a lost or replayed publish cannot make main see a run finish twice.
  Main reads it **once per world per frame** in `SceneWorld::UpdateParticleLiveness()` (called from the builder
  right before packing) and only applies it when `SceneHandle` matches that world. The `mSeenByRenderer` latch is
  mandatory: no entry means "not created yet", never "finished" — without it an `AutoDestroyWhenFinished` effect
  is deleted on its first frame, before render ever creates it. Deletion goes through the deferred
  `DeleteGameObject` queue.
- *Attach to bone/socket* needs nothing new: `GetAttachParentWorldMatrix` resolves the socket and
  `EvaluateSkeletalPosesAndAttachments` invalidates attached children's world matrices before the particle block is
  packed, so `GetWorldLocation()` / `GetWorldRotation()` already report the muzzle's transform of the same frame.
  Caveat: that invalidation is driven by the pose-cache miss, so a socket moved by something other than a pose
  rebuild would need its own invalidation.
- *Drawing.* All of it on the render thread, after the tick: an emitter with a Sprite renderer module is packed
  into `ParticleInstanceGPU`s (`PackParticleSpriteInstances`, which also projects the velocity-stretch direction
  with this frame's camera view) and uploaded to its own `ParticleInstanceBuffer` (SSBO, binding 7); a Ribbon
  emitter likewise (`PackParticleRibbonInstances`), plus its per-particle trail history, which the render-side
  `ParticleBlockStorage` already keeps in the GPU layout, into a `ParticleRibbonHistoryBuffer` (binding 8); an
  emitter without a renderer module is packed as interleaved xyz into a `ParticlePointBuffer` and drawn by
  `ParticlePointProgram`. All buffers are plain copyable handles freed by `DestroyRenderParticleSpawner`.
  The sprite texture is resolved on render with `GetAssetDataNoLoad` + `GetTextureForInfo`; a miss requests the
  load and skips that emitter for the frame, never I/O. Asset-driven emitters neither cast nor receive shadows and
  stay out of the depth prepass; `DrawParticleShadowCasters` and the receive path remain legacy-only.

**Ustawienia cieni światła kierunkowego:** `DirLight` niesie POD `DirectionalLightShadowSettings`
(`CastShadows`, `ShadowDistance`, `CascadeCount`, `SplitLambda`, `Resolution`, `NormalBias`,
`DepthBias`, `PcfRadius`, `CascadeBlend`) skopiowany z `PLU_PROPERTY` na `DirectionalLight`.
Render thread **klampuje je u siebie** (`Renderer::ClampShadowSettings` — liczba kaskad,
rozdzielczość do potęgi dwójki, sensowne biasy), a przy zmianie rozdzielczości/liczby kaskad
przebudowuje zasoby GL (`Renderer::BuildCascadeAtlas` → `RecreateShadowResources`: jeden atlas 2D
o mieszanych rozdzielczościach + jego FBO). Alokacja GL jest tu bezpieczna, bo wątek renderu jest
właścicielem kontekstu, a nic nie samplowało jeszcze atlasu w tej klatce. Reszta parametrów jedzie do shaderów blokiem UBO `ShadowData` (binding 2),
uploadowanym **bezwarunkowo co klatkę** — bez światła po prostu z `CascadeCount = 0`, więc shadery
nigdy nie czytają stanu poprzedniej klatki.

**Światła stożkowe — podział odpowiedzialności.** `SpotLight` jest pierwszym typem światła, którego
może być wiele, więc praca jest rozdzielona między wątki wg tego, kto ma potrzebne dane:

| Wątek | Robi | Dlaczego tam |
|---|---|---|
| MAIN (`RenderSnapshotBuilder::CollectSpotLights`) | culling względem frustum kamery (`ComputeSpotBoundingSphere` + `SphereInFrustum`), przeliczenie cosinusów kątów, **sortowanie malejąco po ważności** (`ShadowPriority`, potem `Intensity / dystans²`, tiebreak po UUID) i przycięcie do `kMaxVisibleSpotLights` | kamera i obiekty sceny są main-only; culling to jedyny test decydujący, czy światło w ogóle trafi na GPU, a dystans do kamery i tak jest tu pod ręką |
| RENDER (`Renderer::PrepareSpotShadowSlots`) | rozdanie `kMaxSpotShadowSlots` slotów atlasu (idzie po gotowej kolejności — **zero sortowania**) i macierze light-space | sloty to zasób GL, a atlas żyje po stronie renderu |
| RENDER (`Renderer::CullShadowCasters`) | culling casterów dla **wszystkich** frustów klatki naraz: najpierw kaskady, potem sloty spotów | frusta cieni istnieją tylko tutaj; jeden sweep = jeden upload SSBO 3 i jeden binding |
| RENDER (`Renderer::RenderSpotShadowPass`) | pass głębi per slot (reużywa `OnlyPositionInstanced`/`OnlyPositionSkeletal` bez zmian w shaderach) | wymaga kontekstu GL |
| RENDER (`Renderer::UpdateSpotLightBuffers`) | UBO 4 + SSBO 5 + SSBO 6, **bezwarunkowo co klatkę** (z `spotLightCount = 0`, gdy nie ma świateł) | ta sama zasada co `ShadowData`: żadna klatka nie czyta listy świateł z poprzedniej |

Konsekwencja podziału: tablica `snapshot->SpotLights` przyjeżdża **już posortowana**, a render thread
tylko rozdaje sloty po kolei. Każdy slot jest odrysowywany od zera co klatkę, więc zamiana kolejności
między klatkami nie daje artefaktów — widoczne jest wyłącznie włączanie/wyłączanie cienia na granicy
budżetu, co jest nieodłączne od stałej puli (`SpotLight::ShadowPriority` pozwala to przypiąć).

**Instancing static meshy:** snapshot niesie też `StaticMeshBatches` (klucz mesh+materiał+CastsShadow,
`InstanceOffset`/`VisibleCount`/`TotalCount`), `StaticInstanceData` (płaska tablica `InstanceGPUData`,
indeksowana `gl_InstanceID` na GPU przez SSBO) i `StaticInstanceBounds` (bounds równoległe, do
cullingu). **Grupowanie (bucketing po hashu klucza, `RenderSnapshotBuilder::mBatchLookup`) i —
docelowo — frustum culling KAMERY to odpowiedzialność wątku MAIN**, w tym samym miejscu co dziś
ekstrakcja komponentów (patrz `BatchStaticMeshes` w `BuildSnapshotAndPublish`) — pass grupujący i tak
przechodzi po wszystkich obiektach, więc culling wpina się tam prawie za darmo. Render thread tylko
uploaduje `StaticInstanceData` do SSBO (`Renderer::mInstanceBuffer`, binding 1, `BindBase` na całą
klatkę, **przed** `RenderShadowPass` — oba passy czytają ten sam upload tej klatki) i rysuje batche.

**Culling casterów cieni jest wyjątkiem — siedzi na RENDER threadzie** (`Renderer::CullShadowCasters`).
Frusta kaskad powstają dopiero tam, więc culling po stronie main oznaczałby zduplikowanie całej
matematyki kaskad. Bounds są już w snapshocie (`StaticInstanceBounds` dla static, `BoundsCenter`/
`BoundsRadius` na `SkeletalMeshRenderObject`), a wynik to jedna skompaktowana tablica indeksów w SSBO
(binding 3) — instancje nie są przepakowywane. Test pomija płaszczyznę **near** (`SphereInFrustumNoNear`):
pass cieni renderuje z `GL_DEPTH_CLAMP`, więc caster przed nią jest spłaszczany na nią i dalej zasłania.

Główny pass (`Renderer::RenderSnapshot`) jest **opt-in per materiał**: `DrawStaticMeshInstanced`
gdy `ShaderProgram::HasInstanceDataBlock()` (niezależnie od `VisibleCount` — instanced-owy
`BasicVertInstanced.vert` celowo nie ma `uniform mat4 model`, więc dla takich programów fallback
per-obiekt renderowałby zły transform), inaczej fallback per-obiekt bajtowo zgodny ze starą ścieżką
(materiały na starych programach nietknięte). Shadow pass (`RenderShadowPass`) jest **silnikowy,
zawsze instanced** dla static meshy — depth-only geometrię rysuje jeden współdzielony
`OnlyPositionInstancedShader` (nie materiał sceny), więc nie ma tu opt-in: każdy batch z
`CastsShadow` idzie jednym `DrawStaticMeshInstanced` po tych instancjach, które przeszły culling
**tej** kaskady (zakres w SSBO indeksów widocznych), a batch niewidoczny w kaskadzie nie generuje
draw calla wcale.

Liczniki `StatDrawCalls`/`StatInstancesDrawn`/`StatCulledCount` na `RenderSnapshot` to tylko roboczy
akumulator klatki na renderze — main thread (panel edytora) czyta je przez mirror `GetStatDrawCalls()`
itp. (`PluUtils.h`), na wzór `GetRenderThreadFPS()` (patrz HELPERS.md), bo żywy `RenderSnapshot` nie
jest bezpieczny do odczytu cross-thread. Tym samym mechanizmem jadą liczniki casterów per kaskada
(`SetShadowCascadeStats`/`GetStatShadowCascadeCasters`, panel Render/GPU).

**Podgląd warstwy kaskady** (panel Render/GPU) idzie przez atomową prośbę:
`RenderingManager::RequestShadowCascadeView(layer)` z main, a render thread kopiuje warstwę tablicy do
zwykłej tekstury 2D (`glCopyImageSubData`) — backend ImGui potrafi bindować tylko `GL_TEXTURE_2D`.
Precedens: `RequestMainFrameBuffer`. Kopia kosztuje, więc prośbę trzeba odnawiać co klatkę; `-1` ją
wyłącza.

**`InstancedStaticMeshComponent` (faza 3):** rejestruje się we własnej mapie `SceneWorld::mInstancedMeshRenderables`
(dziedziczy z `WorldComponent`, nie z `StaticMeshComponent` — patrz komentarz w jego nagłówku o `PhysicsWorld`).
Na wątku MAIN, w tym samym passie grupującym (`BatchStaticMeshes`/`BatchInstancedStaticMeshes` w
`BuildSnapshotAndPublish`), każda pozycja w `Instances` (macierz świata z `GetInstanceWorldMatrices()`,
cache'owana per-komponent, przebudowywana gdy `Instances` albo world matrix komponentu się zmieni) trafia
przez ten sam dodawacz (`AddInstanceToBatch`) i ten sam klucz `(MeshUUID, MaterialUUID, CastsShadow)` co
auto-batching luźnych `StaticMeshComponent` — więc ISMC i luźne komponenty na tym samym meshu+materiale
scalają się w jeden batch/draw call. Render thread nie wie o istnieniu ISMC w ogóle, widzi tylko
`StaticMeshBatches`/`StaticInstanceData`.

### Affinity wskaźników (PluSTL)
`ControlBlock::owningThread` = wątek, który wołał `CreateObject`. `TOwningPointer` operowalny
(copy/deref/Release) tylko na wątku-właścicielu — assert `PLU_PTR_ASSERT_OWNER` (debug;
wyłączany `PLU_DISABLE_PTR_THREAD_CHECKS`). **Move jest thread-neutralny.** `TUsePointer` =
obserwacja z każdego wątku. `EngineObjectManager`: slot-mapa pod `shared_mutex`, Create/Destroy
z dowolnego wątku, readery use-only.

### Concurrent PluSTL containers (`PluSTL/Concurrent/`)

Header-only containers that carry their own synchronization, so a subsystem no longer has to
hand-roll a mutex around a `DynamicArray`/`Queue`/`HashMap`/`HashSet`. Full API reference lives in
`HELPERS.md`; this section is about *when* to reach for each one and what the rules are.

Each one mirrors its single-threaded counterpart name for name (`ConcurrentHashMap` ↔
`HashMap`, `ConcurrentHashSet` ↔ `HashSet`, `ConcurrentArray` ↔ `DynamicArray`,
`ConcurrentQueue` ↔ `Queue` (it *is* a `Queue` behind a mutex),
`ConcurrentString` ↔ `String`), so moving a member over is a type change rather than a rewrite —
`Insert`/`Emplace`/`Contains`/`Remove`/`Reserve`/`Rehash`/`Size`/`Capacity`/`Clear` all mean what
they mean there. Only what rule 1 forbids is spelled differently, and the header says what replaces
it. The striping itself lives once, in `Concurrent/Detail/StripedHashTable.h` (map + set); the
`shared_mutex` plumbing lives once, in `Concurrent/Detail/SharedGuarded.h` (`ConcurrentString`).

Deliberately **not** in `PluSTL_FWD.h` — that header is the precompiled header for both `Engine`
and `PluEditor`, and these pull in `<atomic>`/`<mutex>`/`<shared_mutex>`/`<thread>`. Include the
specific header where you need it, or `Concurrent/Concurrent.h` for all of them (it is also where
the rules below are written down canonically).

| Type | Reach for it when |
|---|---|
| `ConcurrentHashMap<K,V>` | A keyed registry written by several threads, where the hot operation is "update the entry for this key". Striped by key, so unrelated keys never contend. Used by `Profiler`. |
| `ConcurrentHashSet<T>` | Deduplicated work items accumulated by many threads and consumed in one batch. `Insert()`'s `bool` IS the dedupe answer; `DrainToArray()` is the batch consume. Used by `EngineAssetManager::mPendingLoadRequests`. |
| `ConcurrentQueue<T>` | Many producers, one consumer, drained per frame. A `Queue<T>` behind a mutex: `Drain()` swaps the buffer out in O(1), so the lock hold time is constant regardless of queue depth and the consumer iterates with no lock held. Used by `RenderingManager`'s texture queues and `PhysicsWorld::mOverlapQueue` (see below). |
| `ConcurrentRingQueue<T,N>` | A tiny fixed-volume SPSC handoff where a mutex would be all of the cost — e.g. window-lifecycle signalling. Bounded and lock-free; `TryPushBack` can fail. |
| `ConcurrentArray<T>` | Append-mostly storage whose **element addresses must stay stable**. `DynamicArray` reallocates on `Reserve` and its iterators are raw `T*`, so a pointer another thread holds dangles the moment it grows. The slot-map shape — no `Erase`, reuse is a free list's job. Built for `EngineObjectManager`, not yet wired to it. |
| `ConcurrentString` | A genuinely shared, mutable text buffer (editor console / log accumulator). For anything else pass a plain `String` by value — a copy is already thread-safe. |

**Rule 1 — no raw handle ever escapes.** No method returns a pointer, reference or iterator into
the storage. This is not an oversight; it is the reason a `LockPolicy` bolted onto the existing
containers was rejected. `DynamicArray::Iterator` *is* `T*`, `HashMap::Find` returns `TValue*`,
`HashSet::Find` returns an iterator — under a lock every one of those dangles as soon as another
thread rehashes or reallocates. Here you either copy out (`Find`/`Get`/`Snapshot`) or mutate
through a visitor, and `ConcurrentArray::PushBack` gives back an index instead of a `T&`.

**Rule 2 — a `Visit`/`VisitOrInsert`/`ForEach`/`Drain` callback runs under a spinlock.** It must not
block, must not allocate heavily or do I/O, must not take another PluSTL lock, and must not re-enter
the same container. A spinning waiter burns a core the whole time. Copy what you need out and do the
real work after the call returns. (`ConcurrentString::Read`/`Write` use a `shared_mutex`, but the
no-re-entry rule still holds — it is not recursive.)

**Rule 3 — `Size()` and friends are true when read, not when used.** They are relaxed atomic loads:
telemetry and "is there anything to do at all" fast-outs, never control flow that assumes the answer
stays true.

**Rule 4 — the `Drain()` scratch buffer is a local, never a member.** A member would be re-entered
if the processing loop drains again, and would keep the batch alive past the point the consumer
thinks it released it.

**Jolt contact callbacks run on Jolt's job threads**, in parallel, inside `PhysicsSystem::Update`.
`PluContactListener` therefore touches nothing but the two bodies it is handed (Jolt keeps them
locked for the callback), `PhysicsChannelsManager` lookups, and two concurrent members of
`PhysicsWorld`: `mOverlapQueue` (`ConcurrentQueue`, drained by `PhysicsWorld::OnUpdate` on the main
thread right after the step) and `mCurrentOverlaps` (`ConcurrentHashMap` of live sensor contacts,
read by `OnContactRemoved` to skip blocking contacts). Scene objects, components and the plain
`HashMap`s of `PhysicsWorld` are main-thread only — object/component lookup and the
`OnOverlapBegin`/`OnOverlapEnd` calls happen in the drain. `OnContactRemoved` gets no bodies at all
(they may already be destroyed): everything it needs is looked up by contact key in what the begin stored.

Tests: `Tests/PluSTLTests` (configure with the `PluDebugLinux-Tests` preset, or
`PluDebugLinux-Tests-TSan` for the ThreadSanitizer build). Every container has a single-threaded
correctness suite plus a stress suite sized to `hardware_concurrency`. Any TSan report is a
blocker, not a warning.

## Przepisy — „chcę zrobić X przy wątkach"

### GL z maina / panelu edytora (zapis, readback, kompilacja...)
NIE wołaj GL bezpośrednio. Dodaj kolejkę Request* w `RenderingManager` na wzór istniejących:
`RequestTextureFromInfo` / `RequestStaticMeshLoad` / `RequestTextureSave`. Wzorzec:
```
if (!IsOnMainThread()) { /* render — zrób od razu pod lockiem */ }
else { /* main — tylko enqueue (z dedupe), render zdrenuje w Tick() */ }
```
Mapy tekstur/meshy są **własnością renderu** i chodzą pod `mTextureMutex`. Same kolejki pending
(`mPendingTextureRequests`, `mPendingTextureSaves`) to `ConcurrentQueue` — mają własną
synchronizację, więc enqueue **nie** bierze `mTextureMutex`, a dedupe (`PushBackUniqueIf` po UUID)
dzieje się pod lockiem kolejki, nie pod lockiem tekstur. Render drenuje je w `Tick()` przez
`Drain(scratch)` do lokalnego bufora i ładuje poza jakimkolwiek lockiem kolejki; `mTextureMutex`
jest brany krótko, per tekstura.

### Render potrzebuje danych assetu, których nie ma
Na renderze wolno tylko `GetAssetDataNoLoad(uuid)` (bez I/O). Gdy zwróci null:
`RequestAssetDataLoad(uuid)` i spróbuj w następnej klatce — main zrobi I/O w
`ProcessPendingLoads()`. Tak działają shadery silnikowe (OnlyPosition/DebugLine), materiały
i meshe. **Nie hardkoduj pre-warmów** — ten mechanizm jest generyczny.
Kolejka to `ConcurrentHashSet<UInt64>`: `Insert()` dedupuje powtórzone żądania (jego `bool` to
cała odpowiedź), a `ProcessPendingLoads()` woła `DrainToArray()` — drenaż i reset są jedną sekcją
krytyczną, więc żądanie zgłoszone w trakcie I/O maina nie może ani zginąć, ani trafić dwa razy.

### Nowy typ obiektu renderowanego
1. Dodaj POD do `RenderSnapshot` (`RenderThreading.h`) + wyczyść w `Clear()`.
2. Ekstrakcja na main w `RenderSnapshotBuilder::BuildSnapshotAndPublish` (tu wolno dotykać
   sceny/Jolta/ObjectManagera).
3. Rysowanie w `Renderer::RenderSnapshot` (tu wolno GL; zasoby leniwie/z Request*).
Wzór dla danych spoza mesh-pipeline'u: debug geometry fizyki (ekstrakcja → płaski bufor → VBO).

### Nowy zasób GL (FBO, VAO, tekstura specjalna)
Twórz i niszcz na wątku renderu. Stały rozmiar → eager w `Renderer::Initialize`
(jak 4 FBO kaskad); zwalnianie w `Renderer::Shutdown` / `RenderThreadExit` **przed**
`ReleaseGLContext`. Manager main-owned trzymający render-owned obiekty musi je zwolnić na
renderze — wzór: `IShaderManager::ReleaseRenderResources()` wołane z `RenderThreadExit`.

### ImGui
Silnik NIE prowadzi klatki ImGui. Aplikacja (main) buduje ją sama i oddaje wynik. Klatka obejmuje
**wszystkie okna naraz** — inaczej render mógłby złożyć okno A z klatki N i okno B z klatki N-1:

```
BeginImGuiFrameSubmit();
dla każdego okna: SetCurrentContext → NewFrame → UI → Render → SubmitImGuiDrawData(windowID, GetDrawData())
EndImGuiFrameSubmit();          // publikuje ImGuiFrameSnapshot
```

`ImGuiFrameSnapshot` = tablica `ImGuiWindowDrawSnapshot` (windowID + deep-copy: CloneOutput draw
listy + kopia listy tekstur). Wpisy są poolowane w slocie triple buffera, więc stała liczba okien
nie alokuje.

**Kontekst ImGui jest per okno**, wszystkie dzielą jeden font atlas (fonty ładowane raz, jeden
lockstep). Tworzy je `RenderingManager::CreateImGuiContextForWindow` na main (strona platformowa +
DPI; `style.FontScaleDpi` zostaje per okno, więc okno na innym monitorze ma ostry tekst); backend
OpenGL3 żyje w `io.BackendRendererUserData`, czyli **per kontekst**, i jest initowany na renderze
przez kolejkę `mWindowsNeedingGLBackend`.

**Cykl życia okna to handshake z renderem** (`RenderingManager`, trzy kolejki pod jednym mutexem):

| Kierunek | Kolejka | Znaczenie |
|---|---|---|
| main → render | `mWindowsNeedingGLBackend` | kontekst gotowy, postaw jego backend GL |
| main → render | `mWindowsToTearDownGL` | przestań rysować to okno, zwolnij backend |
| render → main | `mWindowsSafeToDestroy` | skończone, okno jest twoje do zniszczenia |

`WindowsManager::ProcessClosingWindows` czeka na ack (`IsImGuiContextTornDown`) i dopiero wtedy
niszczy kontekst i okno. Render pomija okna, których nie ma już w `mImGuiStates` — dzięki temu
stale snapshot wymieniający zamknięte okno jest nieszkodliwy.

**`GImGui` jest thread-local** — patrz overlay port `vcpkg-overlays/imgui/portfile.cmake`, który
wstrzykuje do `imconfig.h` `#define GImGui PluImGuiTLS` (zmienna w `ImGuiRenderState.cpp`). Bez tego
main i render przestawiając kontekst na *różne* okna nadpisywałyby sobie nawzajem bieżący kontekst
(assert `g.WithinFrameScope` w pierwszej klatce z drugim oknem). Przy jednym oknie było to
nieszkodliwe, bo oba wątki ustawiały ten sam wskaźnik.

**VSync przy N oknach.** Swap interval należy do kontekstu GL, a kontekst jest jeden — N swapów
z vsync=1 to N stalli na klatkę (refresh/N). Okna wtórne swapują z interwałem 0
(`IWindow::ApplySwapInterval`), okno 0 swapuje ostatnie i przywraca skonfigurowany interwał.

Okno wtórne dostaje przy tym `mRequestedVSync = false` już w `SDLWindow::Init`. Bez tego
`SwapBuffer` zaraz po każdym jego swapie „naprawiał" interwał z powrotem na 1 (reconcile
`mRequestedVSync != mVSyncEnabled`), pętla renderu w następnej klatce ustawiała go na 0 i tak
w kółko — kilka `SDL_GL_SetSwapInterval` na klatkę walczących ze sobą, objaw: mocny spadek FPS
po złapaniu fokusu na okno główne. **Nie ustawiaj interwału per okno poza tą jedną ścieżką** —
to stan kontekstu, nie okna.

**Przebudowa atlasu fontów** (zmiana rozmiaru czcionki) wymaga lockstepu:
`BeginImGuiLockstep` / `StepImGuiLockstep` / `EndImGuiLockstep` (patrz komentarze w
`RenderingManager.h` i użycie w `EditorApp::OnTick`). Warunek „pending" MUSI obejmować
`WantDestroyNextFrame`, nie tylko `Status != OK` — trzymaj lockstep aż atlas się ustabilizuje.

## Pułapki (sprawdź zanim się zdziwisz)

- **`cond ? *owningPtr : nullptr` kopiuje TOwningPointer** → tymczasowy owner → assert affinity
  na obcym wątku. Rozbij na `if (!p) return nullptr; return *p;`.
- **`shared_mutex` jest nierekurencyjny** — lock tylko wokół mutacji kontenera; konstrukcja
  obiektu PRZED lockiem, destrukcja PO (`condemned = std::move(...)` pod lockiem, dtor poza).
  Ctor/dtor EngineObjectu może rekurencyjnie wołać manager.
- **GL z maina = cichy no-op**, nie błąd. Puste/czarne wyniki bez asercji → sprawdź wątek.
- **Okno główne żyje dłużej niż wątek renderu.** Render swapuje `AppWindow` co klatkę —
  `WindowsManager` NIE niszczy okna 0 (jego zamknięcie kończy `Application::Run`);
  `RenderingManager::Shutdown` joinuje wątek; shutdown renderingu PRZED `OnShutdown()`.
- **Zdarzenia cyklu życia okna obsłuż PRZED `ImGui_ImplSDL3_ProcessEvent`.** Backend zwraca `true`
  dla `CLOSE_REQUESTED` / `MOVED` / `RESIZED` (zapisuje je jako `PlatformRequest*` na swoim
  viewporcie), a `OnEventSDL` na `true` robi wczesny `return` — czyli zjada je w całości. Objaw:
  przycisk zamknięcia menedżera okien i „zamknij wszystkie okna" nie robią nic.
- **Okna wtórne twórz i niszcz tylko przez `WindowsManager`.** `RequestNewWindow` /
  `RequestCloseWindow` są odroczone do `ProcessPendingWindows` / `ProcessClosingWindows`
  (początek i koniec klatki maina). Zniszczenie `SDL_Window` w momencie, w którym poprosił o to
  callback UI, wyrwałoby render threadowi okno spod `MakeGLContextCurrent`/`SwapBuffer`.
- **Okno wtórne nie robi `SDL_GL_MakeCurrent` w `Init()`** — main nie ma kontekstu GL (oddaje go
  w `Application::Run`), a zabranie go render threadowi w locie to natychmiastowy crash. Kontekst
  jest współdzielony (function-local `static` w `SdlWindow.cpp`), więc tekstury/mesze/shadery
  widać we wszystkich oknach bez pracy nad context sharingiem.
- **Liczniki eviction w `Tick()` liczą tiki RENDERU** i muszą być zerowane gdy `uses > 0`
  (gałąź `else`), inaczej churn load/unload + recykling GL id (objaw: podgląd tekstury miga
  atlasem czcionek). Bookkeeping biegnie TYLKO przy świeżym snapshotcie (`Tick(fresh)`) — na
  stale'ach scena się nie renderuje, więc liczniki użyć nie dostają bumpów i liczenie
  bezczynności eksmitowałoby wszystko podczas dłuższego stalla maina (breakpoint, modalny dialog).
- **`DynamicArray::Reserve()` ustawia tylko capacity** (`Size()`=0) — pod `glGetTexImage`/memcpy
  użyj `Resize()`.
- Confinement guardy (`PLU_CORE_ASSERT(IsOnMainThread())`) są no-opami w release — brak
  asercji w release ≠ brak problemu.

## Historia i co dalej

Etapy wdrożenia: **0** atomowy ControlBlock + confinement → **1/1b** lockstep (Runtime, potem
edytor; mechanizm `PrepareFrame`/`RenderOneFrame` + handshake condvar — **usunięty**, zastąpiony
przez pipeline) → **2** pipeline TripleBuffer+RenderSnapshot (obecny stan; cienie CSM, debug
fizyki, ImGui handoff — wszystko zweryfikowane runtime 2026-07) → **3** asset streaming worker
(**nie zaczęty**; założenie: worker bez GL/ObjectManagera/Pythona).

Pod etap 3 podłożone zostały kontenery `PluSTL/Concurrent/` (2026-08) — `Profiler`, kolejki tekstur
`RenderingManagera` i `EngineAssetManager::mPendingLoadRequests` już z nich korzystają. **Nie**
przeniesione (świadomie, jako osobna zmiana): cztery mapy assetów pod `mMutex`, slot-mapa
`EngineObjectManagera`, `EditorShaderManager::shadersToRecompile` oraz niechronione
`mStaticMeshes`/`mSkeletalMeshes`. `ConcurrentArray` istnieje pod slot-mapę, ale nie jest do niej
jeszcze wpięty.

Otwarte tematy: VRAM map cieni (4× 4096² DepthOnly ≈ 256 MB) — do przemyślenia obniżenie
rozdzielczości kaskad.
