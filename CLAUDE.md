# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Language

**Everything that lands in the repository is written in English** — no exceptions:

- Identifiers: class/function/variable/enum names, file names.
- Code comments and doc comments.
- **UI strings**: panel titles, labels, buttons, tooltips, menu entries, error/status text shown to the user.
- Log and assert messages, exception text.
- Commit messages, PR titles/descriptions.
- Markdown docs in the repo (`HELPERS.md`, `MULTITHREADING.md`, `REFLECTION.md`, …). Existing Polish text in these files is legacy — write new sections in English, and do not translate old ones unless asked.

**Chat with the user is in Polish.** Explanations, summaries and questions in the conversation — Polish. The moment text is destined for a file, a commit, or the screen, it switches to English.

Some older code still has Polish comments. Match the *style* of surrounding code, never its language — new and edited lines are English even in a file that is otherwise Polish.

## Engine scale & units

**1 world unit = 1 metre.** This is the canonical engine scale — everything assumes it: Jolt physics (gravity 9.81 m/s², stable body sizes 0.1–10 m), camera clip planes (`kCameraNearClip` = 0.1 in `RenderUtils.h`), shadow range (`DirectionalLight::ShadowDistance` = 150 m) and CSM cascade splits, and the editor grid (minor lines every 1 m, major every 10 m). Do not introduce code that assumes a different unit without converting.

## WIP Build Errors

This is an actively developed engine. Build errors in files not directly related to the current task are **intentional WIP** — do not fix them without being explicitly asked. If a build fails due to a pre-existing error in unrelated files, report it and ask the user how to proceed.

## Build Commands

Two build targets exist: **Editor** (PLU_BUILD_EDITOR=ON) and **Runtime** (PLU_BUILD_EDITOR=OFF). Use CMake presets:

```bash
# Configure
cmake --preset PluDebugLinux-Editor     # Editor debug build → cmake-build-debug-editor/
cmake --preset PluDebugLinux-Runtime    # Runtime debug build → cmake-build-debug-runtime/

# Build
cmake --build --preset PluDebugLinux-Editor
cmake --build --preset PluDebugLinux-Runtime
```

Other presets follow the pattern `Plu{Debug|Release|RelDbg}{Linux|Windows}-{Editor|Runtime}`.

### vcpkg

`vcpkg/` is a gitignored clone **pinned** to `PLU_VCPKG_COMMIT` in `cmake/PluVcpkg.cmake`. Every configure clones it if missing, fetches + checks out the pin if HEAD differs (refuses if the clone has local changes), and re-runs the bootstrap when the executable doesn't match the checkout. Never `git pull` inside `vcpkg/` (or use CLion's "Update vcpkg"): the tool and `scripts/` feed every package's ABI hash, so moving them rebuilds the whole dependency tree — and the next configure moves it back to the pin anyway. To update vcpkg, change the pin (and optionally `builtin-baseline` in `vcpkg.json`) in a commit. Patch ports only through overlays in `vcpkg-overlays/`; overlays ignore `builtin-baseline` and `overrides`, so their version is whatever the overlay says. `-DPLU_VCPKG_SYNC=OFF` leaves the checkout alone; `-DPLU_VCPKG_USE_ENV_ROOT=ON` uses `$VCPKG_ROOT` unpinned.

## Reflection Code Generation

**Critical**: any time you add or change `PLU_CLASS`, `PLU_STRUCT`, `PLU_ENUM`, `PLU_PROPERTY`, or `PLU_FUNCTION` annotations in a header, the reflection generator must be re-run before building. It scans header files with regex (no libclang) and writes `*.generated.{h,cpp}` plus `PluEngineBindings.cpp` / `PluEngine.pyi` into `ReflectionCache/`. The build (CMake) runs it automatically as a pre-build step; run it manually only when iterating outside a build. Note: `ReflectionCache/` is gitignored — it is regenerated on every build.

The generator takes `-p`/`--project <name>` (required), `-b`/`--bindings`, `-F`/`--force`, `-q`/`--quiet`.

**Gotcha**: the generator never prunes. Moving a reflected header between modules leaves the old `ReflectionCache/<OldModule>/*.generated.*` behind, and CMake's glob keeps compiling it — you get a "No such file or directory" on the header's old absolute path. `git mv` also preserves mtime, so the cache thinks nothing changed. After moving reflected files, `rm -rf ReflectionCache`. There is **no** single-file mode. `<name>` is a subdirectory to scan (e.g. `Editor`) or `ALL` to scan the whole repo. CMake invokes it as `-bp Engine` for `LibEngine` and `-bp Editor` for the editor.

```bash
# Run from project root with the build venv. -b also regenerates pybind11 bindings + .pyi stubs.
Python/venv-linux/bin/python PythonTools/ReflectionGeneratorRegex.py -bp ALL
# Force-reprocess everything (ignore ProcessedList.txt):
Python/venv-linux/bin/python PythonTools/ReflectionGeneratorRegex.py -bFp ALL
```

The Python venv used by the build system lives at `Python/venv-linux` (Linux) or `Python/venv-windows` (Windows). Requirements are auto-installed by CMake if `Python/requirements.txt` changes.

## Helper functions

`HELPERS.md` lists all reusable helper/util functions and macros available in the engine (math/transform, paths, string utils, Jolt↔GLM conversions, renderer/shadow helpers, physics helpers — bounding box, raycast, mesh collision, reflection/serialization helpers — `TypeRegistry`/`TypeInfo`/`TypeSerializer<T>`, PluSTL `String` static helpers, logging and assert macros, editor widgets). **Check it before writing a new utility** — there is probably one already.

**Critical**: `HELPERS.md` is maintained manually (not generated). Whenever you add, change, or remove a helper function/macro, update `HELPERS.md` in the same change.

## Multithreading

The engine runs a main thread (input, logic, scene, physics, ImGui frame building) and a render thread (all GL). `MULTITHREADING.md` is the reference for this architecture: frame flow, the `TripleBuffer`/`RenderSnapshot` handoff, recipes ("how do I do X across threads") and known pitfalls. **Read it before touching anything thread-related** — renderer, panels doing GL, asset loading from the render path, ImGui plumbing. Key rule: the main thread has no GL context; GL calls from it are silent no-ops.

**Critical**: `MULTITHREADING.md` is maintained manually. Whenever you change the threading architecture (handoff structures, thread ownership of a subsystem, new cross-thread queue/pattern), update it in the same change.

## Profiling / debugging timers

**Try to apply these timers often.** Whenever you add or touch non-trivial logic — hot paths, loops, init/load steps, anything that could be slow — wrap it in a profiling timer by default rather than waiting for a performance problem. They are cheap and feed the editor **Profiler** panel (menu View → Profiler) instead of spamming the console, so leaving them in place is fine and useful. Prefer instrumenting code over guessing where time goes.

- `PLU_PROFILE_SCOPE("Name")` — drop into any scope/function you want to measure; records silently to the `Profiler` registry (last/avg/min/max + 120-sample history per name).
- `PLU_PROFILE_SCOPE_LOG("Name")` — same, but also logs each sample to the console.
- `PLU_TIMER_START("Name")` / `PLU_TIMER_END("Name")` — manual start/stop across non-scoped regions; pass `true` as the 2nd arg to `PLU_TIMER_START` to also log.

Defined in `PluEngine/Timer.h`; registry is `PluEngine/Profiler.h`. See `HELPERS.md` for the full table.

## Architecture Overview

### Module structure

- **`LibEngine/`** — the engine shared library, split into eleven layered modules: `PluCore`, `PluPlatform`, `PluAssetCore`, `PluAssetTypes`, `PluEffects`, `PluRender`, `PluPhysics`, `PluScripting`, `PluAssetPipeline`, `PluGameplay`, `PluApp`. Each is `LibEngine/{Module}/{include,src}` with its own include root, and the first path segment of a header names its module (`PluEngine/Render/Renderer.h`). Root headers (`PluEngine/Core.h`, `PluTypes.h`, `Log.h`, `Timer.h`, `Profiler.h`) stayed put. Every module is its own shared library (`plu_add_engine_module()` in `LibEngine/CMakeLists.txt`, export macro `PLU<MODULE>_API` from `Core.h`); `Engine` is an INTERFACE target linking all of them. A module may only include from the layers below it. Layers: `PluCore` → `PluPlatform`, `PluAssetCore` → `PluAssetTypes` → `PluEffects`, `PluScripting` → `PluRender`, `PluAssetPipeline` → `PluGameplay` → `PluPhysics` → `PluApp`. `PluPhysics` sits above gameplay on purpose: it builds bodies from gameplay components and listens to scene events, while gameplay never names physics.
- **`Editor/`** — editor application; only compiled when `PLU_BUILD_EDITOR=ON`. Defines panels, viewports (scene, material, static mesh, shader, texture), and editor-specific managers.
- **`Runtime/`** — standalone runtime app; compiled when `PLU_BUILD_EDITOR=OFF`.
- **`PluSTL/`** — custom containers and smart pointers used everywhere instead of `std::`.
- **`PythonTools/`** — offline tooling: `ReflectionGeneratorRegex.py`, `ClassCreator.py`, `EngineAssetsIndexer.py`, `ShaderCodeParser.py`, `LayeringCheck.py`.
  `LayeringCheck.py` enforces the module layering described above: a module may include only from layers below it. Its `Baseline` dict is a ratchet — a module at 0 is closed and any new upward include fails the check. Run it after touching includes in `LibEngine/`; lower a baseline when you cut includes, never raise one.
- **`ThirdParty/`** — vendored: ImGui, glad, glm, nlohmann/json, pybind11, spdlog, stb_image. Everything else comes from vcpkg.

### Application lifecycle

`Application` (abstract, `LibEngine/PluApp/include/PluEngine/Application.h`) is subclassed by `EditorApp` and `RuntimeApp`. It owns an `EngineObjectManager` and an `ApplicationInfo` struct that holds `TUsePointer<>` references to every major subsystem (Renderer, SceneManager, ShadersManager, AssetsManager, InputManager, PythonManager, GameClient, …). `Application::Run()` drives the main loop.

### Object model

Every engine object inherits from `EngineObject`. Lifetime is managed by `EngineObjectManager`, a slot-map that issues `EngineObjectHandle` (index + generation) values. Objects are never accessed by raw pointer — always via the handle or through one of the two smart pointers:

- `TOwningPointer<T>` — reference-counted owning pointer (analogous to `shared_ptr`). When owning count drops to zero the object is deleted.
- `TUsePointer<T>` — non-owning observer (analogous to `weak_ptr`). Does not extend lifetime; becomes null-like when the owner is gone.

Both are backed by a shared `ControlBlock`. **Never use raw pointers or `std::shared_ptr` for engine objects.**

### Reflection system

Classes annotate themselves with macros defined in `LibEngine/PluCore/include/PluEngine/Core.h`:

| Macro | Purpose |
|---|---|
| `PLU_CLASS(...)` | Mark a class for reflection |
| `PLU_STRUCT(...)` | Mark a struct |
| `PLU_ENUM(PyNamespace=Plu)` | Mark an enum — `PyNamespace` is required, omitting it causes compile errors |
| `PLU_PROPERTY(...)` | Expose a field (serialisation, editor UI) |
| `PLU_FUNCTION(...)` | Expose a method |

Optional specifiers inside the parentheses: `Abstract`, `PyExport` (expose to Python), `PyDerive` (allow Python subclassing), `PyOverride` (on functions — can be overridden from Python), `PyNotCallable`.

`ReflectionGeneratorRegex.py` parses these with libclang and generates `ClassName.generated.h` (included inside the class definition via `REFLECTION_BODY_CLASSNAME()`). `TypeInfo` / `PropertyInfo` structs are registered globally in `TypeRegistry`.

Serialisation and editor widgets are driven by specialisations of `TypeSerializer<T>` (see `ReflectionBase.h`).

See `REFLECTION.md` for the full macro/specifier reference.

### Scene & gameplay

- `SceneWorld` owns a `HashMap<UInt64, TOwningPointer<GameObject>>`. It is responsible for spawning/destroying objects, ticking, and coordinating with the `Renderer`. It does not know about physics — see below.
- `GameObject` holds transform (location/rotation/scale), a UUID, and two component lists: `GameObjectComponent` (non-spatial logic) and `WorldComponent` (spatial, forms a transform hierarchy).
- `GameMode` (a `GameObject` subclass) sets the active `Controller` and `Puppet` classes. One `GameMode` lives per `SceneWorld`.
- `Controller → Puppet` is the player input chain.

### Physics

Jolt, wrapped in `PluPhysics`. Every `SceneWorld` gets its own `PhysicsWorld`, created and destroyed by the physics module on `SceneManager` events (`"NewWorldBeforeLoad"` / `"UnloadWorld"`) and looked up with `JoltPhysics::GetPhysicsWorldBySceneHandle(sceneHandle)`. Gameplay and physics talk **only through events**: `SceneWorld` dispatches `"PhysicsTick"`, `"NewComponent"` and `"DestroyComponent"`; components and objects dispatch their transform/shape changes; `PhysicsBodyComponent`'s velocity/force API and `SceneWorld::ShootRaycast` (`"Raycast"`) dispatch events the physics world answers. A body is built per `GameObject` from its `PhysicsBodyComponent` plus every collider (`PhysicsColliderComponent` subclasses and `StaticMeshComponent`s whose mesh has a collision type). The physics tick runs with the scene tick, so in the editor it only simulates in PIE. Details and current gaps (raycasts filter only by ignored objects, collision channels not wired) are in `HELPERS.md`, section Physics.

### Particles

`PluEffects` holds `ParticleClass` / `ParticleSpawner`; `ParticleSpawnerComponent` lives in `PluGameplay`. Spawners are created and **simulated on the render thread**, driven by requests packed into the `RenderSnapshot` — see `MULTITHREADING.md`. Each spawner uploads its positions into its own `ParticlePointBuffer` on the render thread. Work in progress: particles render only as points.

### Renderer

Rendering is split across two threads (see `MULTITHREADING.md`). `RenderingManager` owns the render thread; `Renderer` (`LibEngine/PluRender/include/PluEngine/Render/Renderer.h`) is a plain class (not an `EngineObject`) living entirely on that thread — it consumes POD `RenderSnapshot`s (built each frame on the main thread by `RenderSnapshotBuilder`, handed over via a lock-free `TripleBuffer`) and owns the GL resources: main `FrameBuffer`, CSM cascade framebuffers, debug-geometry buffers. ImGui draw data reaches the render thread through a second `TripleBuffer` (`RenderingManager::SubmitImGuiDrawData`).

### PluSTL containers

Always use PluSTL types, not `std::` equivalents:

| PluSTL | Replaces |
|---|---|
| `DynamicArray<T>` | `std::vector<T>` |
| `Queue<T>` | `std::queue<T>` / `std::deque<T>` |
| `HashMap<K,V>` | `std::unordered_map<K,V>` |
| `HashSet<T>` | `std::unordered_set<T>` |
| `String` | `std::string` |
| `Path` | `std::filesystem::path` |

All types are forward-declared in `PluSTL/PluSTL_FWD.h`.

### Type aliases (PluTypes.h)

`Vec2/3/4`, `IVec2/3/4`, `Matrix4`, `Quaternion` — all glm wrappers. `JSON` = `nlohmann::json`.
Integer aliases: `UInt8/16/32/64`, `Int8/16/32/64` (from `Core.h`).

**Namespace uwaga**: `Vec2`, `Vec3`, `Vec4`, `IVec2/3/4`, `Matrix4`, `Quaternion` są w **global namespace**, nie w `Plu::`. Podobnie `String`, `Path`, `PathW`, `HashMap` itp. z PluSTL. `DynamicArray` i `DefaultAllocator` mieszkają już w `Plu::`, ale `PluSTL/Array/Array.h` i `PluSTL/Allocators/Default.h` eksportują je z powrotem do global namespace przez `using`, więc obie pisownie działają. Tylko klasy silnika (`EngineObject`, `ShaderProgram`, `IShaderCode`, …) oraz smart pointery (`TOwningPointer`, `TUsePointer`) żyją w `Plu::`. W kodzie poza blokiem `namespace Plu { }` (np. w `.cpp` używającym stylu `Plu::ClassName::Method`) typy matematyczne i STL używa się bez prefixu, a typy silnika z prefixem `Plu::`.

### Python scripting

Python runs inside the same process via pybind11. Types annotated `PyExport` / `PyDerive` are registered with `TypeRegistry` and exposed to Python. The editor manages a separate Python venv for project scripts (`~/.local/share/PluEngine/PythonEnv` on Linux). PyArmor is used to obfuscate scripts in distribution builds.

### Editor structure

Editor panels live in `Editor/Panels/` (base infrastructure) and `Editor/DefinedPanels/` (concrete panels). Viewports (render-to-texture windows) live in `Editor/DefinedViewports/`. Editor-specific managers override or extend engine managers and are in `Editor/Managers/`.
