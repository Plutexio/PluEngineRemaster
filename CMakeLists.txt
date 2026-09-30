cmake_minimum_required(VERSION 4.0.2)
# vcpkg: pinned checkout in vcpkg/, synced and bootstrapped on every configure. Must run before
# project(), which loads the toolchain file it sets.
include("${CMAKE_SOURCE_DIR}/cmake/PluVcpkg.cmake")

project(PluEngine)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Multi-config generators (Visual Studio) only. Distribution builds are Release + PLU_DISTRIBUTION,
# not a configuration of their own.
set(CMAKE_CONFIGURATION_TYPES
        Debug
        Release
        RelWithDebInfo
        CACHE STRING "" FORCE
)

# Reconfigure when a `git pull` changes what vcpkg installs: vcpkg.json is tracked by the vcpkg
# toolchain itself and cmake/PluVcpkg.cmake (the pin) by include(), but overlay ports are not.
file(GLOB_RECURSE PLU_VCPKG_OVERLAY_FILES "${CMAKE_SOURCE_DIR}/vcpkg-overlays/*")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${PLU_VCPKG_OVERLAY_FILES})


option(BUILD_SHARED_LIBS "Build shared libraries" ON)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

message(STATUS "C compiler: ${CMAKE_C_COMPILER}")
message(STATUS "CXX compiler: ${CMAKE_CXX_COMPILER}")

# --- 2. KONFIGURACJA PYTHON VENV ---
find_package(Python3 REQUIRED COMPONENTS Interpreter)

if(WIN32)
    set(VENV_NAME "venv-windows")
    set(VENV_BIN "Scripts")
    set(PYTHON_EXE "python.exe")
else()
    set(VENV_NAME "venv-linux")
    set(VENV_BIN "bin")
    set(PYTHON_EXE "python")
endif()

set(VENV_PATH "${CMAKE_SOURCE_DIR}/Python/${VENV_NAME}")
set(REQ_FILE "${CMAKE_SOURCE_DIR}/Python/requirements.txt")
set(STAMP_FILE "${VENV_PATH}/requirements.timestamp")
set(VENV_EXECUTABLE "${VENV_PATH}/${VENV_BIN}/${PYTHON_EXE}")

# Tworzenie venv
if(NOT EXISTS "${VENV_PATH}")
    execute_process(COMMAND ${Python3_EXECUTABLE} -m venv ${VENV_PATH})
endif()

# Inteligentna instalacja requirements
if(EXISTS "${REQ_FILE}")
    if("${REQ_FILE}" IS_NEWER_THAN "${STAMP_FILE}")
        message(STATUS "Aktualizacja pakietów Python...")
        execute_process(COMMAND "${VENV_PATH}/${VENV_BIN}/pip" install -r "${REQ_FILE}"
                        RESULT_VARIABLE PIP_RESULT)
        # Stamp only on success, so a failed install is retried on the next configure.
        if(PIP_RESULT EQUAL 0)
            file(WRITE "${STAMP_FILE}" "")
        else()
            message(WARNING "pip install -r ${REQ_FILE} failed (exit code ${PIP_RESULT}).")
        endif()
    endif()
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${REQ_FILE}")
endif()


if (WIN32 AND CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    add_compile_definitions(
            _CRT_SECURE_NO_WARNINGS
            _CRT_NONSTDC_NO_WARNINGS
    )
endif()

if(MSVC)
    # Wymusza standardy i rygorystyczne sprawdzanie kodu (podobnie jak Clang)
    message("PluMSVC")
    add_compile_options(/permissive- /W4 /utf-8 /MP)
    # Wyłącz optymalizacje dla wersji Debug
    add_compile_options($<$<CONFIG:Debug>:/Od>)
    # Linker musi stworzyć plik .pdb
    set(CMAKE_EXE_LINKER_FLAGS_DEBUG "${CMAKE_EXE_LINKER_FLAGS_DEBUG} /DEBUG")
endif()

find_program(CCACHE_PROGRAM ccache)
if(CCACHE_PROGRAM)
    set(CMAKE_C_COMPILER_LAUNCHER ${CCACHE_PROGRAM})
    set(CMAKE_CXX_COMPILER_LAUNCHER ${CCACHE_PROGRAM})
endif()

option(PLU_BUILD_EDITOR "Build with editor support" OFF)
option(PLU_DISTRIBUTION "Build a standalone distributable (resources resolved next to the exe)" OFF)
option(PLU_BUILD_TESTS "Build PluSTL tests" OFF)
# Tests-only configure: skips LibEngine/Editor/Runtime entirely, so the container tests can be
# built and run without a full engine build. Implies PLU_BUILD_TESTS.
option(PLU_TESTS_ONLY "Configure only the test targets (no engine)" OFF)

if(PLU_TESTS_ONLY)
    set(PLU_BUILD_TESTS ON)
endif()

if(NOT PLU_TESTS_ONLY)
    add_subdirectory(LibEngine)
    if(PLU_BUILD_EDITOR)
        add_subdirectory(Editor)
    else()
        add_subdirectory(Runtime)
    endif ()
endif()
add_subdirectory(PluSTL)

if(PLU_BUILD_TESTS)
    add_subdirectory(Tests)
endif()

if(PLU_BUILD_EDITOR AND TARGET Engine)
    # On every module, not on the Engine interface target: the modules compile their own
    # editor-only code behind this (asset picker widgets, viewport class lookups), so an INTERFACE
    # definition — all an interface target can carry — would reach consumers but not the code that
    # actually tests it.
    foreach(PLU_MODULE IN LISTS PLU_MODULES)
        target_compile_definitions(${PLU_MODULE} PUBLIC PLU_ENGINE_EDITOR_BUILD)
    endforeach()
endif()
