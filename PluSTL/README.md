# PluSTL

Header-only C++20 containers, strings, hashers and smart pointers. PluSTL started as the
in-house replacement for `std::` containers in [PluEngine](https://github.com/Plutexio/PluEngineRemaster)
and is now a standalone library with no dependencies beyond the C++ standard library.

## What is inside

| Header (`PluSTL/...`) | Types |
|---|---|
| `Array/Array.h` | `DynamicArray<T>` — contiguous growable array (`std::vector`) |
| `Queue/Queue.h` | `Queue<T>` — ring-buffer deque (`std::deque` / `std::queue`) |
| `HashMap/HashMap.h` | `HashMap<K, V>` — separate chaining, stable value addresses (`std::unordered_map`) |
| `HashSet/HashSet.h` | `HashSet<T>` — open addressing (`std::unordered_set`) |
| `String/String.h` | `String` / `StringW` (`BasicString<CharT>`) — SSO string with split, case, number and format helpers |
| `Path/Path.h` | `Path` / `PathW` (`BasicPath<CharT>`) — path manipulation on top of `BasicString` |
| `Hashers/Default.h`, `Hashers/String.h` | `DefaultHash<T>`, `HashCombine` — the hasher every container uses by default |
| `Allocators/Default.h` | `DefaultAllocator<T>` and the `Rebind` machinery the containers allocate through |
| `Pointers/TOwningPointer.h`, `Pointers/TUsePointer.h`, `Pointers/Casts.h` | `TOwningPointer<T>` (strong, `std::shared_ptr`-like) and `TUsePointer<T>` (weak observer), `DynamicCast` / `StaticCast` |
| `Random/Random.h` | `PluRandom` — thread-local random number helpers |
| `Concurrent/*.h` | `ConcurrentHashMap`, `ConcurrentHashSet`, `ConcurrentQueue`, `ConcurrentRingQueue`, `ConcurrentArray`, `ConcurrentString`, `SpinLock` |
| `PluSTL_FWD.h` | Everything above except `Concurrent/` — a convenient precompiled-header candidate |

Everything lives in namespace `Plu`; `DynamicArray` and `DefaultAllocator` are also exported
to the global namespace.

`TOwningPointer` checks thread affinity in debug builds: an owning pointer may only be copied,
dereferenced or released on the thread that created the object (observe it from other threads
through `TUsePointer`). Define `PLU_DISABLE_PTR_THREAD_CHECKS` to turn the check off.

## Using it

Requires a C++20 compiler (GCC, Clang or MSVC) and CMake 3.21+.

### As a subdirectory, git submodule or FetchContent

```cmake
include(FetchContent)
FetchContent_Declare(PluSTL
        GIT_REPOSITORY https://github.com/Plutexio/PluSTL.git
        GIT_TAG        main)   # pin a commit for reproducible builds
FetchContent_MakeAvailable(PluSTL)

target_link_libraries(MyApp PRIVATE PluSTL::PluSTL)
```

`add_subdirectory(PluSTL)` on a submodule checkout works the same way.

### Installed

```bash
cmake -S . -B build -DCMAKE_INSTALL_PREFIX=/your/prefix
cmake --install build
```

```cmake
find_package(PluSTL REQUIRED)
target_link_libraries(MyApp PRIVATE PluSTL::PluSTL)
```

### Example

```cpp
#include "PluSTL/PluSTL_FWD.h"

#include <cstdio>

int main()
{
    Plu::DynamicArray<Plu::String> names;
    names.PushBack("alpha");
    names.PushBack("beta");

    Plu::HashMap<Plu::String, int> lengths;
    for (const Plu::String& name : names)
        lengths.Insert(name, static_cast<int>(name.Length()));

    if (const int* length = lengths.Find("beta"))
        std::printf("beta has %d characters\n", *length);
}
```

## Tests

The tests use a tiny bundled runner (`tests/TestFramework.h`), no external framework.

```bash
cmake -S . -B build          # tests are on by default when PluSTL is the top-level project
cmake --build build
ctest --test-dir build --output-on-failure
```

`-DPLUSTL_TESTS_TSAN=ON` builds them with ThreadSanitizer; the concurrent stress suites are
where that pays off. When PluSTL is consumed as a subproject, tests and install rules are off
unless `PLUSTL_BUILD_TESTS` / `PLUSTL_INSTALL` are set.

## Debugger support

`debug/gdb/` holds GDB pretty printers for the containers, strings and smart pointers — see
`debug/gdb/README.md`.

## License

MIT — see [LICENSE](LICENSE).
