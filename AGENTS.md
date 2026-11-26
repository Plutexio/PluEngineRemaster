# AGENTS.md

This file provides guidance for coding agents working in PluEngine. PluEngine is a C++20 3D game engine with a Vulkan renderer under development, Jolt physics, an ImGui editor, and embedded Python via pybind11.

## Language and scope

- Communicate with the user in Polish. Write repository content in English, including identifiers, comments, docs, UI strings, logs, assertions, commit messages, and PR text. Keep existing Polish text unless the task calls for editing it.
- Preserve work already in progress. If compilation fails in unrelated code, report the failing target and error and keep fixes within the requested scope.
- One world unit equals one metre. The transform helpers in `PluUtils.h` take Euler angles in degrees with pitch on X, yaw on Y, and roll on Z. Check the helper's units when converting to physics or render types.

## Build and test commands

Run these commands from the repository root. The root build requires CMake 4.0.2 or newer and C++20. Linux presets use Clang and Ninja. Initialize the three top-level submodules once with `git submodule update --init`. The engine supplies its own PluSTL and PluBase targets to PluNetwork.

```bash
cmake --list-presets=all

# Editor (PLU_BUILD_EDITOR=ON)
cmake --preset PluDebugLinux-Editor
cmake --build --preset PluDebugLinux-Editor

# Runtime (PLU_BUILD_EDITOR=OFF)
cmake --preset PluDebugLinux-Runtime
cmake --build --preset PluDebugLinux-Runtime

# PluSTL and engine-side tests without compiling the engine
cmake --preset PluDebugLinux-Tests -DVCPKG_MANIFEST_INSTALL=OFF
cmake --build --preset PluDebugLinux-Tests
ctest --test-dir cmake-build-debug-tests --output-on-failure

# Check module includes after changes in LibEngine/
python3 PythonTools/LayeringCheck.py
```

- The executables are `PluEditor` and `PluRuntime`. Editor-specific code in engine modules is guarded by `PLU_ENGINE_EDITOR_BUILD`. For changes shared by both applications, check both configurations when possible.
- `PluSTLTests` lives in `PluSTL/tests/`. `PluEngineTests` lives in `Tests/`. The tests-only preset can skip installing the engine's vcpkg manifest dependencies. For concurrent container changes, `PluDebugLinux-Tests-TSan` builds the same suites with ThreadSanitizer in `cmake-build-debug-tests-tsan/`.
- [CMakePresets.json](CMakePresets.json) lists Debug, Release, and RelWithDebInfo configurations for Linux and Windows. The release-only `PluDistWindowsCross-Editor` and `PluDistWindowsCross-Runtime` presets build and package Windows binaries from Linux. See [WindowsCrossClangCL.cmake](cmake/Toolchains/WindowsCrossClangCL.cmake) for xwin and `PLU_XWIN_DIR`. Select the host OS with `CMAKE_HOST_WIN32` for build-time tools and the target OS with `WIN32` for target code and libraries.
- Configure manages the pinned `vcpkg/` checkout through [PluVcpkg.cmake](cmake/PluVcpkg.cmake). Update the pin there and patch ports in `vcpkg-overlays/`. Avoid pulling or patching the local clone. CMake also provisions the host venv under `Python/venv-linux` or `Python/venv-windows` from `Python/requirements.txt`.

## Module boundaries

`LibEngine/<Module>/{include,src}` contains twelve shared libraries. Public headers use `PluEngine/...` include paths and the owning module's export macro, such as `PLUCORE_API`. The `Engine` interface target links the modules for the applications. Keep direct dependencies in the owning module's `CMakeLists.txt`.

| Layer | Modules |
|---|---|
| 0 | `PluCore` |
| 1 | `PluPlatform`, `PluAssetCore` |
| 2 | `PluAssetTypes` |
| 3 | `PluEffects`, `PluScripting` |
| 4 | `PluAssetPipeline` |
| 5 | `PluGameplay` |
| 6 | `PluRender`, `PluPhysics`, `PluNetworking` |
| 7 | `PluApp` |

- Dependencies between engine modules point strictly to lower layers. Keep the zero-violation baseline in [LayeringCheck.py](PythonTools/LayeringCheck.py). Gameplay and physics communicate through scene and object events. `PluNetworking` is the engine's integration point for `PluNetwork`.
- `Application::Run()` in `PluApp` drives the main loop. `ApplicationInfo` exposes the app subsystems. `Editor/` and `Runtime/` supply the concrete applications. `EngineAssets/` contains built-in assets and `PythonTools/` contains build tools such as the reflection generator and engine asset indexer.
- `PluSTL/`, `PluBase/`, and `PluNetwork/` are separate Git repositories. Keep them independent of engine types. For an intentional submodule change, commit it in the submodule and update the parent repository's recorded revision when committing the integration.

## Engine conventions

- Use the PluSTL counterparts for containers, strings, and paths, including `DynamicArray`, `StaticArray`, `Queue`, `HashMap`, `HashSet`, `String`, and `Path`. Math aliases such as `Vec3`, `Quaternion`, and `Matrix4` are in the global namespace. Engine classes and smart pointers are in `Plu`.
- Use `EngineObjectManager` to create managed objects. Represent ownership with `TOwningPointer<T>` and observation with `TUsePointer<T>` or an `EngineObjectHandle` (index and generation). Avoid storing raw engine object pointers or wrapping them in `std::shared_ptr`. Keep owning-pointer operations on the creating thread. A cross-thread observer needs independent lifetime and synchronization guarantees before accessing object data.
- Read [HELPERS.md](HELPERS.md) before writing a utility. Update its catalog in the same change when adding, removing, or changing a helper or macro. Use `PLU_PROFILE_SCOPE` for non-trivial work such as hot loops, initialization, and loading. Use `PLU_PROFILE_SCOPE_LOG` when console output is needed.

## Reflection and Python

- Edit the source headers and regenerate after changing `PLU_CLASS`, `PLU_STRUCT`, `PLU_INTERFACE`, `PLU_ENUM`, `PLU_PROPERTY`, or `PLU_FUNCTION`. Use the generated `REFLECTION_BODY_<TYPE>()` macro where required. Give reflected enums a `PyNamespace`, usually `Plu`. See [REFLECTION.md](REFLECTION.md) for specifiers and validation rules.
- CMake runs the regex-based `ReflectionGeneratorRegex.py` automatically, using `-bp LibEngine` for the engine and `-bp Editor` or `-bp Runtime` for the application. Use the commands below for manual regeneration from the repository root. The generator requires a project and supports directory-wide scans.

```bash
Python/venv-linux/bin/python PythonTools/ReflectionGeneratorRegex.py -bp ALL
Python/venv-linux/bin/python PythonTools/ReflectionGeneratorRegex.py -bFp ALL
```

- `-b` regenerates pybind11 bindings and type stubs. `-F` bypasses the modification cache. Treat `ReflectionCache/` as generated and ignored. If a reflected header moves or is removed, clear stale generated outputs before regenerating. A forced scan can still leave obsolete module outputs. When the binding chunk layout changes, let CMake reconfigure on the next build.
- `TypeRegistry` holds reflection metadata. `TypeSerializer<T>` supplies serialization and editor controls. Generated Python bindings compile into `PluApp`. The build tools' host venv is separate from the editor-managed venv for project scripts.

## Rendering and threading

- Start with `Application.cpp`, `PluPlatform`, and `PluRender` for window or graphics initialization. The Vulkan controller currently creates the instance. Device setup and frame presentation are still under development. SDL3 handles Linux windows and Win32 handles Windows windows.
- The old OpenGL render thread and its `RenderSnapshot` and `TripleBuffer` handoffs are historical. [MULTITHREADING.md](MULTITHREADING.md) records their design and also covers surviving APIs such as the worker pool and pointer affinity. Verify subsystem ownership and scheduling in the current source and update the document in the same change when altering either.
- Keep scene and asset-registry mutations and their events on the main thread. For independent worker jobs, use `ApplicationInfo::AppThreadPool` and `ParallelFor` where appropriate. Collect plain results in worker tasks and apply them on the owning thread. Follow the thread-safety contract of the specific PluSTL concurrent container.

## Editor and assets

- Read [Editor/CLAUDE.md](Editor/CLAUDE.md) before changing editor panels, viewports, scenes, or windows. Panel and viewport mutations of persistent asset data call `PanelChangedAsset()` or `ViewportChangedAsset()`. Save through the existing Ctrl+S and Save All flow. Binary asset viewports with a saver opt in through `SetCanBeSaved(true)`.
- The Structure panel caches the object list and listens for `"GameObjectsChanged"` on the current world. Keep subscriptions and dirty marking correct across scene switches and reloads. Use `SceneStructurePrepareClone` when duplicating through scene JSON so the clone gets its own UUID and name and keeps its original world transform after detaching.
- Build each window's ImGui UI in its own context and keep docking records scoped to that window. For visible UI that changes on its own, use the redraw APIs in `PluEngine/FrameDemand.h` at the appropriate cadence. Check the main loop before relying on power-saving or probe-frame behavior during the Vulkan migration.
- Keep the existing JSON and binary asset paths compatible. `EngineAssetManager` indexes binary `PLUA` descriptors and checks their version against `kMaxKnownAssetVersion`. [BINARYASSETS.md](BINARYASSETS.md) describes the proposed generic runtime cooking path. Check current loaders and `PrepareAssetsForDistribution()` for implemented behavior.
