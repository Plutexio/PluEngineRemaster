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

### Windows .exe from Linux (cross build)

`PluDistWindowsCross-Editor` / `PluDistWindowsCross-Runtime` build a Windows distribution on Linux and package it into `EditorDistWindows/` / `RuntimeDistWindows/` (build presets run `DistributeEditor` / `DistributeRuntime`). Release only.

- Toolchain: clang-cl + lld-link + llvm-rc (via `cmake/Toolchains/llvm-rc-codepage`) against an MSVC CRT + Windows SDK splatted by **xwin** — same MSVC ABI as a native Visual Studio build, so the code takes its normal `_WIN32` paths. One-time setup and the sysroot location (`$PLU_XWIN_DIR`, default `~/.local/share/xwin`) are in `cmake/Toolchains/WindowsCrossClangCL.cmake`.
- vcpkg builds the ports with the overlay triplet `vcpkg-triplets/x64-windows-xwin.cmake` (release-only), whose toolchain is `WindowsCrossClangCL.vcpkg.cmake` = the cross toolchain + vcpkg's own `windows.cmake`.
- Python: `cmake/PluWindowsCrossPython.cmake` downloads the official NuGet package (headers + `python3xx.lib`) and the embeddable package (shipped next to the exe) into the build dir, pinned by version + SHA256. Keep its minor version equal to the host Python's.
- ImageMagick: `cmake/PluWindowsCrossImageMagick.cmake` downloads the official MSVC build (the `-Q16-HDRI-x64-dll` installer from the ImageMagick GitHub release, pinned by version + SHA256) and unpacks it with **innoextract** (one-time host setup) into the build dir; it defines the same `ImageMagick::Magick++` / `ImageMagick::MagickCore` targets FindImageMagick does on Linux. It is an "installed" build: at runtime it finds its coders and config only through `MAGICK_CODER_MODULE_PATH` / `MAGICK_FILTER_MODULE_PATH` / `MAGICK_CONFIGURE_PATH`, which must point into `ImageMagick/` next to the exe. On Linux ImageMagick is a system requirement and is not shipped.
- Packaging is `cmake/PluDistribute.cmake` (shared by editor and runtime, native and cross); on a Linux host it resolves PE imports with `llvm-objdump`. The MSVC C++ runtime (`msvcp140.dll`) is not shipped — the target machine needs the VC++ Redistributable, same as a native build.
- Anything run on the build machine (venv, PyArmor) must test `CMAKE_HOST_WIN32`, not `WIN32` — in a cross build `WIN32` is true on a Linux host.

### Build speed setup

Measured choices — keep them unless you re-measure:

- Linux presets compile with **Clang** (`base-linux` in `CMakePresets.json`): a full Debug editor build is ~2.3× faster than with GCC 16 (31 s vs 71 s wall, 440 s vs 1030 s CPU, measured 2026-10-19). Switching the compiler of an existing build dir makes CMake wipe its cache once.
- `CMAKE_CXX_SCAN_FOR_MODULES OFF` (root `CMakeLists.txt`). The engine has no C++20 modules; scanning made ccache refuse every compile and silently disabled unity builds.
- ccache runs with PCH-friendly `sloppiness`; the linker is mold (lld fallback) via `CMAKE_LINKER_TYPE`.
- spdlog is compiled once as a **shared** library (`SPDLOG_COMPILED_LIB`), not header-only: header-only it was over half of every object file. Shared so there is one logger registry per process. It is in `PLU_THIRDPARTY_SHARED_LIBS` (together with `stb`, `PluBase` and `PluNetwork`) and gets copied next to the executable.
- Engine modules unity-build only the generated `*.generated.cpp` (batch `PLU_REFLECTION_UNITY_BATCH_SIZE`); hand-written sources are `SKIP_UNITY_BUILD_INCLUSION` so missing includes still show up.
- The engine PCH holds `Core.h`, `PluBase/Log.h`, `Timer.h`, `stdpch.h` and the heavy third-party headers (`PLU_THIRDPARTY_PCH_HEADERS`). Do not force-include anything in front of `cmake_pch.hxx` — GCC then ignores the `.gch`.

### vcpkg

`vcpkg/` is a gitignored clone **pinned** to `PLU_VCPKG_COMMIT` in `cmake/PluVcpkg.cmake`. Every configure clones it if missing, fetches + checks out the pin if HEAD differs (refuses if the clone has local changes), and re-runs the bootstrap when the executable doesn't match the checkout. Never `git pull` inside `vcpkg/` (or use CLion's "Update vcpkg"): the tool and `scripts/` feed every package's ABI hash, so moving them rebuilds the whole dependency tree — and the next configure moves it back to the pin anyway. To update vcpkg, change the pin (and optionally `builtin-baseline` in `vcpkg.json`) in a commit. Patch ports only through overlays in `vcpkg-overlays/`; overlays ignore `builtin-baseline` and `overrides`, so their version is whatever the overlay says. `-DPLU_VCPKG_SYNC=OFF` leaves the checkout alone; `-DPLU_VCPKG_USE_ENV_ROOT=ON` uses `$VCPKG_ROOT` unpinned.

## Reflection Code Generation

**Critical**: any time you add or change `PLU_CLASS`, `PLU_STRUCT`, `PLU_ENUM`, `PLU_PROPERTY`, or `PLU_FUNCTION` annotations in a header, the reflection generator must be re-run before building. It scans header files with regex (no libclang) and writes `*.generated.{h,cpp}` plus `PluEngineBindings.cpp` / `PluEngine.pyi` into `ReflectionCache/`. The pybind11 bindings themselves are chunked into `ReflectionCache/Bindings/PluEngineBindings_<Module>_<N>.cpp` (per module, ceil(generated size / `BindingsChunkTargetChars`) files, packed bottom-up) so they compile in parallel; `PluEngineBindings.cpp` only holds `PYBIND11_EMBEDDED_MODULE` and calls the chunks in layer order. All of them compile into `PluApp`. The chunk list is `ReflectionCache/BindingsLayout.cmake`, include()d by `LibEngine/CMakeLists.txt`: when the wanted layout changes, the generator still fills the layout CMake already knows for that build and rewrites the file, so CMake reconfigures on the next build. The build (CMake) runs it automatically as a pre-build step; run it manually only when iterating outside a build. Note: `ReflectionCache/` is gitignored — it is regenerated on every build.

The generator takes `-p`/`--project <name>` (required), `-b`/`--bindings`, `-F`/`--force`, `-q`/`--quiet`.

Reflected headers under `Platforms/Windows/` and `Platforms/Linux/` are scanned on every host; their `.generated.cpp` and their lines in `Init<Module>Reflection.cpp` are wrapped in `#ifdef PLU_PLATFORM_WINDOWS` / `PLU_PLATFORM_LINUX` (`PlatformGuardMacros` in the generator), so one `ReflectionCache/` serves the Linux build and the Linux → Windows cross build alike. Not covered: pybind11 bindings and `PLU_ENUM`s in those folders.

**Gotcha**: the generator never prunes (except `ReflectionCache/Bindings/`, which the bindings step owns and cleans). Moving a reflected header between modules leaves the old `ReflectionCache/<OldModule>/*.generated.*` behind, and CMake's glob keeps compiling it — you get a "No such file or directory" on the header's old absolute path. `git mv` also preserves mtime, so the cache thinks nothing changed. After moving reflected files, `rm -rf ReflectionCache`. There is **no** single-file mode. `<name>` is a subdirectory to scan (e.g. `Editor`) or `ALL` to scan the whole repo. CMake invokes it as `-bp Engine` for `LibEngine` and `-bp Editor` for the editor.

```bash
# Run from project root with the build venv. -b also regenerates pybind11 bindings + .pyi stubs.
Python/venv-linux/bin/python PythonTools/ReflectionGeneratorRegex.py -bp ALL
# Force-reprocess everything (ignore ProcessedList.txt):
Python/venv-linux/bin/python PythonTools/ReflectionGeneratorRegex.py -bFp ALL
```

The Python venv used by the build system lives at `Python/venv-linux` (Linux) or `Python/venv-windows` (Windows). Requirements are auto-installed by CMake if `Python/requirements.txt` changes.

## Helper functions

`HELPERS.md` lists all reusable helper/util functions and macros available in the engine (math/transform, paths, string utils, Jolt↔GLM conversions, render utils, physics helpers — bounding box, raycast, mesh collision, reflection/serialization helpers — `TypeRegistry`/`TypeInfo`/`TypeSerializer<T>`, PluSTL `String` static helpers, logging and assert macros, editor widgets). **Check it before writing a new utility** — there is probably one already.

**Critical**: `HELPERS.md` is maintained manually (not generated). Whenever you add, change, or remove a helper function/macro, update `HELPERS.md` in the same change.

## Multithreading

The OpenGL renderer and its render thread are removed; the Vulkan renderer that replaces them does not exist yet, so right now everything runs on the main thread (input, logic, scene, physics, ImGui frame building) and nothing is drawn. `MULTITHREADING.md` still describes the old two-thread architecture (frame flow, the `TripleBuffer`/`RenderSnapshot` handoff, pitfalls) — historical, but worth reading before designing the new render thread.

**Critical**: `MULTITHREADING.md` is maintained manually. Whenever you change the threading architecture (handoff structures, thread ownership of a subsystem, new cross-thread queue/pattern), update it in the same change.

## Profiling / debugging timers

**Try to apply these timers often.** Whenever you add or touch non-trivial logic — hot paths, loops, init/load steps, anything that could be slow — wrap it in a profiling timer by default rather than waiting for a performance problem. They are cheap and feed the editor **Profiler** panel (menu View → Profiler) instead of spamming the console, so leaving them in place is fine and useful. Prefer instrumenting code over guessing where time goes.

- `PLU_PROFILE_SCOPE("Name")` — drop into any scope/function you want to measure; records silently to the `Profiler` registry (last/avg/min/max + 120-sample history per name).
- `PLU_PROFILE_SCOPE_LOG("Name")` — same, but also logs each sample to the console.
- `PLU_TIMER_START("Name")` / `PLU_TIMER_END("Name")` — manual start/stop across non-scoped regions; pass `true` as the 2nd arg to `PLU_TIMER_START` to also log.

Defined in `PluEngine/Timer.h`; registry is `PluEngine/Profiler.h`. See `HELPERS.md` for the full table.

## Architecture Overview

### Module structure

- **`LibEngine/`** — the engine shared library, split into twelve layered modules: `PluCore`, `PluPlatform`, `PluAssetCore`, `PluAssetTypes`, `PluEffects`, `PluRender`, `PluPhysics`, `PluNetworking`, `PluScripting`, `PluAssetPipeline`, `PluGameplay`, `PluApp`. Each is `LibEngine/{Module}/{include,src}` with its own include root, and the first path segment of a header names its module (`PluEngine/Render/RenderUtils.h`). Root headers (`PluEngine/Core.h`, `PluTypes.h`, `Timer.h`, `Profiler.h`) stayed put. Every module is its own shared library (`plu_add_engine_module()` in `LibEngine/CMakeLists.txt`, export macro `PLU<MODULE>_API` from `Core.h`); `Engine` is an INTERFACE target linking all of them. A module may only include from the layers below it. Layers: `PluCore` → `PluPlatform`, `PluAssetCore` → `PluAssetTypes` → `PluEffects`, `PluScripting` → `PluRender`, `PluAssetPipeline` → `PluGameplay` → `PluPhysics`, `PluNetworking` → `PluApp`. `PluPhysics` sits above gameplay on purpose: it builds bodies from gameplay components and listens to scene events, while gameplay never names physics. `PluNetworking` sits next to it for the same reason (replication of gameplay objects); it is the engine's only user of `PluNetwork`.
- **`Editor/`** — editor application; only compiled when `PLU_BUILD_EDITOR=ON`. Defines panels, viewports (scene, material, static mesh, skeletal mesh, texture, …), and editor-specific managers.
- **`Runtime/`** — standalone runtime app; compiled when `PLU_BUILD_EDITOR=OFF`.
- **`PluSTL/`** — custom containers and smart pointers used everywhere instead of `std::`. A **git submodule** of its own public repo, [Plutexio/PluSTL](https://github.com/Plutexio/PluSTL) (header-only, MIT, no engine dependencies — keep it that way: nothing in it may name engine types, pybind11 or glm). Headers are under `PluSTL/include/` and are included with the prefix: `#include "PluSTL/Array/Array.h"`. Engine modules get it by linking `PluSTL::PluSTL`. Its tests are `PluSTL/tests` (target `PluSTLTests`); `Tests/` holds only engine-side tests (`PluEngineTests`). A change to PluSTL is a commit in the submodule, pushed to its repo, followed by a commit in the engine that bumps the submodule pointer. After cloning: `git submodule update --init` (fetches PluSTL, PluBase and PluNetwork; no `--recursive` — PluNetwork's own nested PluSTL/PluBase submodules are not used inside the engine).
- **`PluBase/`** — the layer under the engine, meant for reuse in other projects: integer/platform/export macros (`PluBase/Base.h`), asserts (`Assert.h`), logging (`Log.h`), glm aliases + their hashes (`Types.h`, `GlmHash.h`), `PluUUID` and `ThreadAffinity`. Its own **shared** library (`PluBase::PluBase`, export macro `PLUBASE_API`; shared because the loggers and the main-thread id are process-wide state), added from `LibEngine/CMakeLists.txt` and linked PUBLIC by `PluCore`; it depends only on PluSTL, spdlog and glm. Nothing in it may name engine types — standalone it gets its dependencies from `find_package`. A **git submodule** of its own public repo, [Plutexio/PluBase](https://github.com/Plutexio/PluBase) (MIT); changes follow the PluSTL workflow — commit in the submodule, push, bump the pointer in the engine. The engine's `PluEngine/Core.h` includes `Base.h` + `Assert.h` and adds the module export macros and the reflection macros; `PluEngine/PluTypes.h` includes `Types.h` and adds `JSON`.
- **`PluNetwork/`** — networking on raw OS sockets (TCP/UDP sockets, `PacketWriter`/`PacketReader`, `PacketDispatcher`), meant for reuse outside the engine. Its own **shared** library (`PluNetwork::PluNetwork`, export macro `PLUNETWORK_API`, namespace `PluNetwork`), added from `LibEngine/CMakeLists.txt` right after PluBase and listed in `PLU_THIRDPARTY_SHARED_LIBS`; depends only on PluBase (and through it PluSTL, spdlog, glm), which it takes from the parent's targets when they exist. Linked only by the `PluNetworking` engine module, which wraps it (`Plu::Networking::Init/Shutdown`, called from `Application`). A **git submodule** of [Plutexio/PluNetwork](https://github.com/Plutexio/PluNetwork); changes follow the PluSTL workflow. Work in progress: the Windows socket path is still `#error` stubs, so the Windows cross build fails until it is filled in.
- **`PythonTools/`** — offline tooling: `ReflectionGeneratorRegex.py`, `ClassCreator.py`, `EngineAssetsIndexer.py`, `LayeringCheck.py`.
  `LayeringCheck.py` enforces the module layering described above: a module may include only from layers below it. Its `Baseline` dict is a ratchet — a module at 0 is closed and any new upward include fails the check. Run it after touching includes in `LibEngine/`; lower a baseline when you cut includes, never raise one.
- **`ThirdParty/`** — vendored: ImGuizmo, glm, nlohmann/json, pybind11, spdlog, stb_image (compiled once as the shared `stb` target). ImGui comes from vcpkg. Everything else comes from vcpkg.

### Application lifecycle

`Application` (abstract, `LibEngine/PluApp/include/PluEngine/Application.h`) is subclassed by `EditorApp` and `RuntimeApp`. It owns an `EngineObjectManager` and an `ApplicationInfo` struct that holds `TUsePointer<>` references to every major subsystem (SceneManager, AssetsManager, InputManager, PythonManager, GameClient, …). `Application::Run()` drives the main loop.

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

- `SceneWorld` owns a `HashMap<UInt64, TOwningPointer<GameObject>>`. It is responsible for spawning/destroying objects and ticking. It does not know about physics — see below.
- `GameObject` holds transform (location/rotation/scale), a UUID, and two component lists: `GameObjectComponent` (non-spatial logic) and `WorldComponent` (spatial, forms a transform hierarchy).
- `GameMode` (a `GameObject` subclass) sets the active `Controller` and `Puppet` classes. One `GameMode` lives per `SceneWorld`.
- `Controller → Puppet` is the player input chain.

### Physics

Jolt, wrapped in `PluPhysics`. Every `SceneWorld` gets its own `PhysicsWorld`, created and destroyed by the physics module on `SceneManager` events (`"NewWorldBeforeLoad"` / `"UnloadWorld"`) and looked up with `JoltPhysics::GetPhysicsWorldBySceneHandle(sceneHandle)`. Gameplay and physics talk **only through events**: `SceneWorld` dispatches `"PhysicsTick"`, `"NewComponent"` and `"DestroyComponent"`; components and objects dispatch their transform/shape changes; `PhysicsBodyComponent`'s velocity/force API and `SceneWorld::ShootRaycast` (`"Raycast"`) dispatch events the physics world answers. A body is built per `GameObject` from its `PhysicsBodyComponent` plus every collider (`PhysicsColliderComponent` subclasses and `StaticMeshComponent`s whose mesh has a collision type). The physics tick runs with the scene tick, so in the editor it only simulates in PIE. Details and current gaps (raycasts filter only by ignored objects, collision channels not wired) are in `HELPERS.md`, section Physics.

### Particles

`PluEffects` holds `ParticleClass` / `ParticleSpawner` and the particle system asset/compiler; `ParticleSpawnerComponent` lives in `PluGameplay`. Spawners used to be created and simulated on the GL render thread, driven by requests packed into the `RenderSnapshot`; with that thread gone nothing simulates or draws them until the Vulkan renderer does.

### Renderer

Being rewritten on Vulkan. `PluRender` links Vulkan (system SDK/loader), VMA and Slang (vcpkg) plus Magick++ for image loading; the OpenGL renderer, its render thread (`RenderingManager`, `RenderSnapshotBuilder`, the `TripleBuffer` handoffs), the GLSL shader assets and both shader managers are deleted. What is left in `PluRender` is API-agnostic (`RenderUtils.h`, `RenderingInterfaces.h`). `MaterialInfo` is an empty asset until the new material model exists; `StaticMesh`/`SkeletalMesh::IsLoaded` means GPU-resident and stays false. Windows have no graphics context of their own any more (SDL windows are created without `SDL_WINDOW_OPENGL`, and not yet with `SDL_WINDOW_VULKAN`). ImGui contexts are not created by anything right now (the old `ImGuiRenderState` did it), and `PluImGuiTLS` — the thread-local current context the imgui overlay port needs — is defined in `PluPlatform/src/ImGuiContextTLS.cpp`.

### PluSTL containers

Always use PluSTL types, not `std::` equivalents:

| PluSTL | Replaces |
|---|---|
| `DynamicArray<T>` | `std::vector<T>` |
| `StaticArray<T, N>` | `std::array<T, N>` |
| `Queue<T>` | `std::queue<T>` / `std::deque<T>` |
| `HashMap<K,V>` | `std::unordered_map<K,V>` |
| `HashSet<T>` | `std::unordered_set<T>` |
| `String` | `std::string` |
| `Path` | `std::filesystem::path` |

All types are forward-declared in `PluSTL/PluSTL_FWD.h`.

### Type aliases (PluTypes.h)

`Vec2/3/4`, `IVec2/3/4`, `Matrix4`, `Quaternion` — all glm wrappers (from `PluBase/Types.h`). `JSON` = `nlohmann::json`.
Integer aliases: `UInt8/16/32/64`, `Int8/16/32/64` (from `PluBase/Base.h`, reached through `Core.h`).

**Namespace uwaga**: `Vec2`, `Vec3`, `Vec4`, `IVec2/3/4`, `Matrix4`, `Quaternion` są w **global namespace**, nie w `Plu::`. Podobnie `String`, `Path`, `PathW`, `HashMap` itp. z PluSTL. `DynamicArray` i `DefaultAllocator` mieszkają już w `Plu::`, ale `PluSTL/Array/Array.h` i `PluSTL/Allocators/Default.h` eksportują je z powrotem do global namespace przez `using`, więc obie pisownie działają. Tylko klasy silnika (`EngineObject`, `ShaderProgram`, `IShaderCode`, …) oraz smart pointery (`TOwningPointer`, `TUsePointer`) żyją w `Plu::`. W kodzie poza blokiem `namespace Plu { }` (np. w `.cpp` używającym stylu `Plu::ClassName::Method`) typy matematyczne i STL używa się bez prefixu, a typy silnika z prefixem `Plu::`.

### Python scripting

Python runs inside the same process via pybind11. Types annotated `PyExport` / `PyDerive` are registered with `TypeRegistry` and exposed to Python. The editor manages a separate Python venv for project scripts (`~/.local/share/PluEngine/PythonEnv` on Linux). PyArmor is used to obfuscate scripts in distribution builds.

### Editor structure

Editor panels live in `Editor/Panels/` (base infrastructure) and `Editor/DefinedPanels/` (concrete panels). Viewports (render-to-texture windows) live in `Editor/DefinedViewports/`. Editor-specific managers override or extend engine managers and are in `Editor/Managers/`.
