project(Engine)

# The engine is eleven layered shared libraries. Each one lives in LibEngine/<Module>/{include,src}
# with its own include root, its own export macro and its own reflection cache directory, and may
# link only the modules below it — the layering is stated once, in each module's CMakeLists, and
# from here on the linker is what enforces it. PythonTools/LayeringCheck.py checks the same rule at
# the source level, which catches a violation before it becomes a link error.
#
# Listed bottom-up; add_subdirectory follows this order so a module's dependencies already exist.
set(PLU_MODULES
        PluCore
        PluPlatform PluAssetCore
        PluAssetTypes
        PluEffects PluScripting
        PluRender PluAssetPipeline
        PluGameplay
        PluPhysics
        PluApp
        CACHE INTERNAL "Engine modules, bottom-up. Consumers copy these next to their executable."
)

set(PLU_THIRDPARTY_DIR "${CMAKE_SOURCE_DIR}/ThirdParty")

# Generated reflection sources merged into one unity translation unit (see plu_add_engine_module).
set(PLU_REFLECTION_UNITY_BATCH_SIZE 8 CACHE STRING "Generated reflection sources per unity batch")

# Added to every engine module's precompiled header (and the editor's).
set(PLU_THIRDPARTY_PCH_HEADERS
        <pybind11/pybind11.h>
        <nlohmann/json.hpp>
        <imgui.h>
        <glm/glm.hpp>
        <glm/gtc/quaternion.hpp>
        CACHE INTERNAL "Third-party headers in the engine precompiled headers"
)

# ─────────────────────────────────────────────────────────────────────────────
#  Third-party
# ─────────────────────────────────────────────────────────────────────────────
# spdlog compiled once instead of header-only. Header-only, every translation unit compiled
# spdlog's and the bundled fmt's implementation again: 57% of the code in a trivial object file, and
# 40–47% of its compile time. SHARED for the same reason as glad below: the logger registry is
# global state, and a static archive would give every engine library a registry of its own.
set(PLU_SPDLOG_DIR "${PLU_THIRDPARTY_DIR}/spdlog")
add_library(spdlog SHARED
        "${PLU_SPDLOG_DIR}/src/spdlog.cpp"
        "${PLU_SPDLOG_DIR}/src/stdout_sinks.cpp"
        "${PLU_SPDLOG_DIR}/src/color_sinks.cpp"
        "${PLU_SPDLOG_DIR}/src/file_sinks.cpp"
        "${PLU_SPDLOG_DIR}/src/async.cpp"
        "${PLU_SPDLOG_DIR}/src/cfg.cpp"
        "${PLU_SPDLOG_DIR}/src/bundled_fmtlib_format.cpp"
)
target_include_directories(spdlog PUBLIC "${PLU_SPDLOG_DIR}/include")
# What spdlog's own CMakeLists sets for a shared build. spdlog_EXPORTS, which picks dllexport over
# dllimport on Windows, is defined by CMake for this target.
target_compile_definitions(spdlog
        PUBLIC SPDLOG_COMPILED_LIB SPDLOG_SHARED_LIB FMT_SHARED
        PRIVATE FMT_LIB_EXPORT
)
target_compile_options(spdlog PUBLIC $<$<CXX_COMPILER_ID:MSVC>:/wd4251 /wd4275>)
find_package(Threads REQUIRED)
target_link_libraries(spdlog PUBLIC Threads::Threads)

add_library(json INTERFACE)
target_include_directories(json INTERFACE "${PLU_THIRDPARTY_DIR}/nlohmann")

add_library(stb_image INTERFACE)
target_include_directories(stb_image INTERFACE "${PLU_THIRDPARTY_DIR}/stb_image")

add_library(pybind11 INTERFACE)
target_include_directories(pybind11 INTERFACE "${PLU_THIRDPARTY_DIR}/pybind11/include")

add_library(glm INTERFACE)
target_include_directories(glm INTERFACE "${PLU_THIRDPARTY_DIR}/glm")

find_package(imgui CONFIG REQUIRED)

add_library(ImGuizmo STATIC "${PLU_THIRDPARTY_DIR}/ImGuizmo/ImGuizmo.cpp")
target_include_directories(ImGuizmo PUBLIC "${PLU_THIRDPARTY_DIR}/ImGuizmo")
set_target_properties(ImGuizmo PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_link_libraries(ImGuizmo PUBLIC imgui::imgui)

# glad is SHARED, and has to be: it is nothing but global function pointers, filled in once by
# gladLoadGL() from the platform module. As a static archive every library that links it would get
# its own copy of that table and only the platform module's would be initialised — the first GL
# call from anywhere else would dereference a null pointer.
if (WIN32)
    add_library(glad SHARED "${PLU_THIRDPARTY_DIR}/glad/glad.c" "${PLU_THIRDPARTY_DIR}/glad/glad_wgl.c")
else ()
    add_library(glad SHARED "${PLU_THIRDPARTY_DIR}/glad/glad.c")
endif ()
target_include_directories(glad PUBLIC "${PLU_THIRDPARTY_DIR}/glad")
set_target_properties(glad PROPERTIES POSITION_INDEPENDENT_CODE ON)
if (WIN32)
    # glad's own export macro; without it nothing is visible outside the DLL.
    target_compile_definitions(glad PRIVATE GLAD_GLAPI_EXPORT_BUILD)
    target_compile_definitions(glad PUBLIC GLAD_GLAPI_EXPORT)
endif ()

# Third-party shared libraries that, like the engine modules, have to sit next to the executable.
set(PLU_THIRDPARTY_SHARED_LIBS glad spdlog
        CACHE INTERNAL "Third-party shared libraries consumers copy next to their executable."
)

find_package(OpenGL REQUIRED)
find_package(Python3 COMPONENTS Interpreter Development)
find_package(Jolt CONFIG REQUIRED)
find_package(assimp CONFIG REQUIRED)
find_package(argparse CONFIG REQUIRED)
if (NOT WIN32)
    find_package(SDL3 CONFIG REQUIRED)
endif ()

# ─────────────────────────────────────────────────────────────────────────────
#  Generated code
# ─────────────────────────────────────────────────────────────────────────────
# Bootstrap on the first configure so the per-module globs have files to collect. Afterwards the
# EngineReflection target below keeps them regenerated on every build.
if (NOT EXISTS "${CMAKE_SOURCE_DIR}/ReflectionCache/PluCore" OR NOT EXISTS "${CMAKE_SOURCE_DIR}/ReflectionCache/BindingsLayout.cmake")
    execute_process(
            COMMAND ${VENV_EXECUTABLE} "${CMAKE_SOURCE_DIR}/PythonTools/ReflectionGeneratorRegex.py" -bp LibEngine
            WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    )
endif ()

# PLU_BINDINGS_SOURCES: the pybind11 bindings chunks PluApp compiles. The generator sizes the chunk
# count per module and rewrites this file when it changes; include() makes that reconfigure CMake on
# the next build. The chunks and PluEngineBindings.cpp are BYPRODUCTS below: a chunk the new layout
# adds is then something Ninja knows how to make before the generator has written it, and Ninja
# re-checks these files after the generator ran instead of trusting what it saw at startup (the
# main file changes along with the layout, and a stale object of it fails the link).
include("${CMAKE_SOURCE_DIR}/ReflectionCache/BindingsLayout.cmake")
set(PLU_BINDINGS_MAIN_SOURCE "${CMAKE_SOURCE_DIR}/ReflectionCache/PluEngineBindings.cpp")
set_source_files_properties(${PLU_BINDINGS_SOURCES} ${PLU_BINDINGS_MAIN_SOURCE} PROPERTIES GENERATED TRUE)

# One run for the whole engine, before any module compiles. It was a PRE_BUILD command on the
# single Engine target; with ten of them it has to be a target they all depend on, or it would run
# ten times per build.
add_custom_target(EngineReflection
        COMMENT "Generating Reflection Data"
        COMMAND ${VENV_EXECUTABLE} "${CMAKE_SOURCE_DIR}/PythonTools/ReflectionGeneratorRegex.py" -bp LibEngine
        BYPRODUCTS ${PLU_BINDINGS_SOURCES} ${PLU_BINDINGS_MAIN_SOURCE}
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
)

# Engine asset UUID index -> ReflectionCache/EngineAssets.h (Plu::EngineAssets::*). Built from the
# .pluasset files by EngineAssetsIndexer.py, a different tool from the reflection generator above
# and so in need of its own hook.
#
# A custom TARGET rather than a PRE_BUILD command: PRE_BUILD is only honoured by the Visual Studio
# generators, and everywhere else CMake degrades it to PRE_LINK — which runs AFTER the compilation
# that needs this header. DEPENDS on the asset files means it re-indexes exactly when a .pluasset
# is added, removed or edited, and does nothing on every other build.
file(GLOB_RECURSE PLU_ENGINE_ASSET_FILES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/EngineAssets/*.pluasset")

add_custom_command(
        OUTPUT "${CMAKE_SOURCE_DIR}/ReflectionCache/EngineAssets.h"
        COMMENT "Indexing engine assets (EngineAssets.h)"
        COMMAND ${VENV_EXECUTABLE} "${CMAKE_SOURCE_DIR}/PythonTools/EngineAssetsIndexer.py"
        DEPENDS ${PLU_ENGINE_ASSET_FILES} "${CMAKE_SOURCE_DIR}/PythonTools/EngineAssetsIndexer.py"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        VERBATIM
)
add_custom_target(EngineAssetsIndex DEPENDS "${CMAKE_SOURCE_DIR}/ReflectionCache/EngineAssets.h")

# ─────────────────────────────────────────────────────────────────────────────
#  Module helper
# ─────────────────────────────────────────────────────────────────────────────
# Everything every engine module shares: sources, generated sources, include roots, its own export
# flag, the precompiled header and the RPATH. A module's own CMakeLists adds only what is specific
# to it — which modules it depends on, and which third-party libraries it uses.
function(plu_add_engine_module NAME)
    cmake_parse_arguments(PLU_MOD "" "" "DEPENDS" ${ARGN})

    file(GLOB_RECURSE MOD_SOURCES CONFIGURE_DEPENDS "src/*.cpp")
    file(GLOB_RECURSE MOD_HEADERS CONFIGURE_DEPENDS "include/*.h" "include/*.inl" "src/*.h")
    file(GLOB_RECURSE MOD_GENERATED CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/ReflectionCache/${NAME}/*.cpp")

    add_library(${NAME} SHARED ${MOD_SOURCES} ${MOD_HEADERS} ${MOD_GENERATED})
    add_dependencies(${NAME} EngineReflection EngineAssetsIndex)

    # Only this module's flag: its own symbols export, everything it imports from the modules below
    # resolves to an import. That is the whole reason each module has a macro of its own.
    string(TOUPPER ${NAME} MOD_UPPER)
    target_compile_definitions(${NAME} PRIVATE PLU_BUILD_${MOD_UPPER})

    target_include_directories(${NAME}
            PUBLIC
            "${CMAKE_CURRENT_SOURCE_DIR}/include"
            "${CMAKE_SOURCE_DIR}/ReflectionCache/${NAME}"
            "${CMAKE_SOURCE_DIR}/ReflectionCache"     # EngineAssets.h
            "${CMAKE_SOURCE_DIR}/ThirdParty"
            PRIVATE
            # Private headers next to the sources (Platforms/<os>/*.h) are reached with a path
            # relative to the module's src root.
            "${CMAKE_CURRENT_SOURCE_DIR}/src"
    )

    target_link_libraries(${NAME} PUBLIC ${PLU_MOD_DEPENDS} PluSTL::PluSTL)

    target_compile_definitions(${NAME} PUBLIC $<$<CONFIG:Debug>:PLU_DEBUG>)
    if (NOT PLU_DISTRIBUTION)
        # A distribution build leaves this undefined on purpose, so GetEngineResourcesDir() returns
        # the directory next to the executable instead of the source tree.
        target_compile_definitions(${NAME} PRIVATE PLU_PROJECT_ROOT="${CMAKE_SOURCE_DIR}")
    endif ()

    target_precompile_headers(${NAME} PRIVATE
            "${CMAKE_SOURCE_DIR}/PluSTL/include/PluSTL/PluSTL_FWD.h"
            "${CMAKE_SOURCE_DIR}/LibEngine/PluCore/include/PluEngine/Core.h"
            "${CMAKE_SOURCE_DIR}/LibEngine/PluCore/include/PluEngine/Log.h"
            "${CMAKE_SOURCE_DIR}/LibEngine/PluCore/include/PluEngine/Timer.h"
            "${CMAKE_SOURCE_DIR}/LibEngine/PluCore/include/stdpch.h"
            # Third-party headers nearly every TU reaches through X.generated.h → ReflectionBase.h
            # and PluTypes.h.
            ${PLU_THIRDPARTY_PCH_HEADERS}
    )
    # No `-include stdpch.h` here: cmake_pch.hxx already includes it, and GCC only uses the .gch
    # when it is the first include — a forced include in front of it silently disables the PCH.
    target_compile_options(${NAME} PRIVATE
            # The generated pybind11 bindings exceed the COFF per-object section limit on MSVC.
            $<$<CXX_COMPILER_ID:MSVC>:/bigobj>
    )

    # Unity build for the generated reflection sources only. Each is a few KB of its own code that
    # paid the full header cost (~5–7 s) as a translation unit of its own. Hand-written sources stay
    # out: a unity batch hides a header that forgets an include as long as a neighbour supplies it.
    # Sources added to the target later (PluApp's bindings) are not in this list and must opt out
    # themselves.
    set_source_files_properties(${MOD_SOURCES} PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON)
    set_target_properties(${NAME} PROPERTIES
            UNITY_BUILD ON
            UNITY_BUILD_BATCH_SIZE ${PLU_REFLECTION_UNITY_BATCH_SIZE}
            # Every engine library sits next to the executable that loads it.
            INSTALL_RPATH "$ORIGIN"
            BUILD_RPATH "$ORIGIN"
    )
endfunction()

foreach (PLU_MODULE IN LISTS PLU_MODULES)
    add_subdirectory(${PLU_MODULE})
endforeach ()

# ─────────────────────────────────────────────────────────────────────────────
#  Compatibility target
# ─────────────────────────────────────────────────────────────────────────────
# Consumers still say `target_link_libraries(... Engine)` and get the whole engine. PluApp does not
# depend on PluAssetPipeline in the include graph — the asset loaders register themselves through
# reflection rather than being called by name — so listing every module here is also what keeps the
# pipeline in the link.
add_library(Engine INTERFACE)
target_link_libraries(Engine INTERFACE ${PLU_MODULES})
