# PluBase

The foundation layer of PluEngine, usable on its own: fixed-width integer aliases, platform
detection and DLL visibility macros, asserts, spdlog-based logging, glm math aliases with PluSTL
hashers, a 64-bit random `PluUUID`, and main-thread / thread-name helpers.

| Header | Contents |
|---|---|
| `PluBase/Base.h` | `UInt8..UInt64`, `Int8..Int64`, `PLU_PLATFORM_*`, `PLU_EXPORT` / `PLU_IMPORT`, `PLU_DEBUGBREAK()` |
| `PluBase/Assert.h` | `PLU_ASSERT`, `PLU_CORE_ASSERT` (active with `PLU_ENABLE_ASSERTS`, implied by `PLU_DEBUG`) |
| `PluBase/Log.h` | `Plu::Log::Init(coreName, clientName)`, `PLU_CORE_TRACE..CRITICAL`, `PLU_TRACE..CRITICAL` |
| `PluBase/Types.h` | `Vec2/3/4`, `IVec2/3/4`, `Matrix4`, `Quaternion` (global namespace) |
| `PluBase/GlmHash.h` | `Plu::DefaultHash` for glm vectors, quaternions and matrices |
| `PluBase/PluUUID.h` | `Plu::PluUUID` — random non-zero 64-bit id, 0 = invalid |
| `PluBase/ThreadAffinity.h` | `RegisterMainThread`, `IsOnMainThread`, `RegisterThreadName`, `GetCurrentThreadName` |

## Using it

```cmake
add_subdirectory(PluBase)          # needs the PluSTL::PluSTL, spdlog::spdlog and glm::glm targets,
target_link_libraries(MyApp PRIVATE PluBase::PluBase)   # or finds them with find_package
```

```cpp
#include "PluBase/Assert.h"
#include "PluBase/ThreadAffinity.h"

int main()
{
    Plu::Log::Init("NET", "APP");
    Plu::RegisterMainThread();
    PLU_CORE_ASSERT(Plu::IsOnMainThread(), "not on the main thread");
    PLU_INFO("Hello from {0}", Plu::GetCurrentThreadName().CStr());
}
```

PluBase is a **shared** library on purpose: the loggers and the main-thread id are process-wide
state, and a static copy linked into several shared libraries would split them. spdlog has to be
shared for the same reason. Ship `libPluBase` and `libspdlog` next to the executable.
