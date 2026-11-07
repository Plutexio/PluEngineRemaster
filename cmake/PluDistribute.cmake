# =====================================================================================
#  PluDistribute.cmake — packages a self-contained editor or runtime.
#
#  Invoked by the DistributeEditor / DistributeRuntime targets as `cmake -P` with parameters:
#    DIST_DIR       - destination directory (EditorDist/, ...)
#    SOURCE_DIR     - repo root
#    EXE            - full path to the built executable
#    ENGINE_LIBS    - ;-separated paths to every engine module library (+ glad, spdlog, PluBase)
#    VCPKG_LIB_DIR  - vcpkg_installed/<triplet>/lib (Linux .so files)
#    VCPKG_BIN_DIR  - vcpkg_installed/<triplet>/bin (Windows DLLs)
#    TARGET_WINDOWS - ON when the executable is a Windows one. Not WIN32: in script mode WIN32
#                     describes the host, and a Linux -> Windows cross build packages PE files on a
#                     Linux host.
#  Optional:
#    PYARMOR_EXE    - pyarmor from the build venv; set by the editor, which runs
#                     PythonTools/ShaderCodeParser.py at runtime and ships it obfuscated.
#    PYTHON_RUNTIME_DIR - unpacked python.org embeddable package (cross builds); its interpreter is
#                     shipped next to the executable so the package runs without Python installed.
#    OBJDUMP        - llvm-objdump, to read PE imports when packaging Windows files on a non-Windows
#                     host.
# =====================================================================================

foreach(_var DIST_DIR SOURCE_DIR EXE ENGINE_LIBS VCPKG_LIB_DIR VCPKG_BIN_DIR TARGET_WINDOWS)
    if(NOT DEFINED ${_var})
        message(FATAL_ERROR "PluDistribute.cmake: missing required variable ${_var}")
    endif()
endforeach()

message(STATUS "[dist] Cleaning ${DIST_DIR}")
file(REMOVE_RECURSE "${DIST_DIR}")
file(MAKE_DIRECTORY "${DIST_DIR}")

# --- Binaries --------------------------------------------------------------------------
message(STATUS "[dist] Copying binaries")
file(COPY "${EXE}" DESTINATION "${DIST_DIR}")
# The engine is several shared libraries now, not one, and the executable finds them next to itself
# ($ORIGIN RPATH on Linux, the DLL search order on Windows) — so all of them have to land there.
foreach(_lib IN LISTS ENGINE_LIBS)
    file(COPY "${_lib}" DESTINATION "${DIST_DIR}")
endforeach()

# Linux: the vcpkg dependencies are shared libraries now (x64-linux-dynamic), so they have to
# travel with the executable the same way the engine modules do — a distributed build has no
# vcpkg_installed/ to reach into. Rather than guessing a list, ask the linker's own view: resolve
# what the executable and the engine libraries actually need, transitively.
#
# Only what vcpkg built is shipped. Resolving also turns up libc, libstdc++, libGL, the Wayland and
# X11 clients and so on; those belong to the host system, and bundling them is how a package breaks
# on a machine whose drivers expect the system copies.
if(NOT TARGET_WINDOWS)
    message(STATUS "[dist] Resolving runtime dependencies")
    file(GET_RUNTIME_DEPENDENCIES
            EXECUTABLES "${EXE}"
            LIBRARIES ${ENGINE_LIBS}
            DIRECTORIES "${VCPKG_LIB_DIR}"
            RESOLVED_DEPENDENCIES_VAR _resolved_deps
            UNRESOLVED_DEPENDENCIES_VAR _unresolved_deps
    )
    set(_shipped 0)
    foreach(_dep IN LISTS _resolved_deps)
        if(_dep MATCHES "vcpkg_installed")
            # FOLLOW_SYMLINK_CHAIN: these arrive as libfoo.so.6 -> libfoo.so.6.0.2 and the loader
            # needs the whole chain, not just the name it was linked against.
            file(COPY "${_dep}" DESTINATION "${DIST_DIR}" FOLLOW_SYMLINK_CHAIN)
            math(EXPR _shipped "${_shipped} + 1")
        endif()
    endforeach()
    message(STATUS "[dist] Shipped ${_shipped} vcpkg libraries")

    # Anything the resolver could not find would be missing at startup on the target machine, so it
    # is worth saying out loud rather than discovering when the package does not run.
    if(_unresolved_deps)
        message(WARNING "[dist] Unresolved runtime dependencies:\n  ${_unresolved_deps}")
    endif()
elseif(CMAKE_HOST_WIN32)
    # Native Windows build: copy over every DLL sitting next to the executable. vcpkg (applocal
    # deployment) already resolved the full, transitive runtime dependencies there based on the
    # actual PE imports — including ones $<TARGET_RUNTIME_DLLS:...> can't see because they're
    # linked PRIVATE by an intermediate dependency (e.g. assimp → kubazip/minizip/poly2tri/pugixml/zlib).
    get_filename_component(_exe_dir "${EXE}" DIRECTORY)
    message(STATUS "[dist] Copying dependency DLLs from ${_exe_dir}")
    file(GLOB _dep_dlls "${_exe_dir}/*.dll")
    file(COPY ${_dep_dlls} DESTINATION "${DIST_DIR}")
else()
    # Windows files packaged on a Linux host (cross build). vcpkg's applocal step does run here, but
    # it copies each library's dependencies next to that library, so the executable's directory is
    # not complete. Resolve the PE imports instead, the same way as on Linux, with llvm-objdump
    # reading them.
    if(NOT OBJDUMP)
        message(FATAL_ERROR "[dist] Packaging Windows binaries on a non-Windows host needs OBJDUMP (llvm-objdump).")
    endif()
    set(CMAKE_GET_RUNTIME_DEPENDENCIES_PLATFORM "windows+pe")
    set(CMAKE_GET_RUNTIME_DEPENDENCIES_TOOL "objdump")
    set(CMAKE_GET_RUNTIME_DEPENDENCIES_COMMAND "${OBJDUMP}")
    message(STATUS "[dist] Resolving runtime dependencies (PE)")
    file(GET_RUNTIME_DEPENDENCIES
            EXECUTABLES "${EXE}"
            LIBRARIES ${ENGINE_LIBS}
            DIRECTORIES "${VCPKG_BIN_DIR}"
            # The API sets are virtual DLLs of the OS loader, never files.
            PRE_EXCLUDE_REGEXES "^api-ms-" "^ext-ms-"
            RESOLVED_DEPENDENCIES_VAR _resolved_deps
            UNRESOLVED_DEPENDENCIES_VAR _unresolved_deps
            # The PE rules search each library's own directory first, where applocal left identical
            # copies of e.g. imgui.dll — a "conflict" only by path.
            CONFLICTING_DEPENDENCIES_PREFIX _conflicting
    )
    # Ship by name, from vcpkg's own bin/: whichever copy the resolver found, that is the original.
    # Matched case-insensitively — the PE rules compare lower-cased names, which on a case-sensitive
    # file system leaves e.g. Jolt.dll and SDL3.dll "unresolved".
    file(GLOB _vcpkg_dlls "${VCPKG_BIN_DIR}/*.dll")
    foreach(_dll IN LISTS _vcpkg_dlls)
        get_filename_component(_name "${_dll}" NAME)
        string(TOLOWER "${_name}" _key)
        set("_vcpkg_dll_${_key}" "${_dll}")
    endforeach()
    set(_engine_names "")
    foreach(_lib IN LISTS EXE ENGINE_LIBS)
        get_filename_component(_name "${_lib}" NAME)
        string(TOLOWER "${_name}" _key)
        list(APPEND _engine_names "${_key}")
    endforeach()

    set(_dep_names ${_unresolved_deps} ${_conflicting_FILENAMES})
    foreach(_dep IN LISTS _resolved_deps)
        get_filename_component(_name "${_dep}" NAME)
        list(APPEND _dep_names "${_name}")
    endforeach()
    list(TRANSFORM _dep_names TOLOWER)
    list(REMOVE_DUPLICATES _dep_names)

    set(_shipped 0)
    set(_from_system "")
    foreach(_key IN LISTS _dep_names)
        if(DEFINED "_vcpkg_dll_${_key}")
            file(COPY "${_vcpkg_dll_${_key}}" DESTINATION "${DIST_DIR}")
            math(EXPR _shipped "${_shipped} + 1")
        elseif(NOT _key IN_LIST _engine_names)
            list(APPEND _from_system "${_key}")
        endif()
    endforeach()
    message(STATUS "[dist] Shipped ${_shipped} vcpkg libraries")
    # What is left is the OS (kernel32, user32, opengl32, ...), the MSVC runtime and python3xx.dll
    # (shipped below). Listed for reference, not as a warning: no Windows system DLL exists on this
    # host to resolve against.
    if(_from_system)
        list(JOIN _from_system ", " _from_system_text)
        message(STATUS "[dist] Expected from Windows / shipped separately: ${_from_system_text}")
    endif()
endif()

# --- Python runtime -----------------------------------------------------------------------
# The embeddable package: python3xx.dll, the zipped stdlib, the extension modules (.pyd) and the
# MSVC runtime DLLs it was built against. Py_Initialize finds the stdlib through python3xx.zip
# next to the DLL. python3xx._pth is left out on purpose: it would put the interpreter into isolated
# mode (no site, PYTHONPATH ignored), which is not how the engine runs against a regular installation
# on a native Windows build. The python(w).exe launchers are not needed either.
if(PYTHON_RUNTIME_DIR)
    message(STATUS "[dist] Copying Python runtime from ${PYTHON_RUNTIME_DIR}")
    file(GLOB _python_files "${PYTHON_RUNTIME_DIR}/*")
    foreach(_file IN LISTS _python_files)
        get_filename_component(_name "${_file}" NAME)
        if(_name MATCHES "\\._pth$" OR _name MATCHES "^pythonw?\\.exe$" OR _name STREQUAL "python.cat" OR _name STREQUAL ".complete")
            continue()
        endif()
        if(_name STREQUAL "LICENSE.txt")
            file(COPY_FILE "${_file}" "${DIST_DIR}/Python-LICENSE.txt")
        else()
            file(COPY "${_file}" DESTINATION "${DIST_DIR}")
        endif()
    endforeach()
endif()

# --- Engine resources --------------------------------------------------------------------
message(STATUS "[dist] Copying EngineAssets")
file(COPY "${SOURCE_DIR}/EngineAssets" DESTINATION "${DIST_DIR}")

# Fonts + icons (GetEngineResourcesDir()/ThirdParty/UI/Fonts/...)
message(STATUS "[dist] Copying ThirdParty/UI")
file(MAKE_DIRECTORY "${DIST_DIR}/ThirdParty")
file(COPY "${SOURCE_DIR}/ThirdParty/UI" DESTINATION "${DIST_DIR}/ThirdParty")

# Type stub for projects' Python environment
if(EXISTS "${SOURCE_DIR}/ReflectionCache/PluEngine.pyi")
    file(COPY "${SOURCE_DIR}/ReflectionCache/PluEngine.pyi" DESTINATION "${DIST_DIR}")
endif()

# --- Obfuscate ShaderCodeParser.py (editor) ---------------------------------------------
# The only Python tool run at editor runtime (EditorShaderManager
# → RunScript(GetEngineResourcesDir()/PythonTools/ShaderCodeParser.py)). PyArmor gen
# creates a pyarmor_runtime_* package next to the obfuscated script, which must be
# copied alongside it.
#
# PyArmor runs on the host, and by default builds its runtime package (pyarmor_runtime.so/.pyd) for
# the host. A cross build asks for the target's one explicitly.
if(PYARMOR_EXE)
    set(_pytools_dir "${DIST_DIR}/PythonTools")
    file(MAKE_DIRECTORY "${_pytools_dir}")

    set(_pyarmor_platform "")
    if(TARGET_WINDOWS AND NOT CMAKE_HOST_WIN32)
        set(_pyarmor_platform --platform windows.x86_64)
    endif()

    message(STATUS "[dist] Obfuscating ShaderCodeParser.py (PyArmor)")
    execute_process(
            COMMAND "${PYARMOR_EXE}" gen ${_pyarmor_platform} --output "${_pytools_dir}"
                    "${SOURCE_DIR}/PythonTools/ShaderCodeParser.py"
            RESULT_VARIABLE _pyarmor_res
            OUTPUT_VARIABLE _pyarmor_out
            ERROR_VARIABLE  _pyarmor_err
    )
    if(NOT _pyarmor_res EQUAL 0)
        message(FATAL_ERROR "[dist] PyArmor failed (${_pyarmor_res}):\n${_pyarmor_out}\n${_pyarmor_err}")
    endif()

    if(NOT EXISTS "${_pytools_dir}/ShaderCodeParser.py")
        message(FATAL_ERROR "[dist] PyArmor did not generate ShaderCodeParser.py in ${_pytools_dir}")
    endif()
endif()

message(STATUS "[dist] Done: ${DIST_DIR}")
