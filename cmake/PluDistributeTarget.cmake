# plu_add_distribute_target(<name> <exe target> <dist dir> [extra -D arguments...])
#
# Adds a target that packages <exe target> into <dist dir> through cmake/PluDistribute.cmake: the
# executable, every engine module and engine-side shared library (PLU_MODULES +
# PLU_THIRDPARTY_SHARED_LIBS), the vcpkg libraries they need, engine resources, and — for a Linux ->
# Windows cross build — the embeddable Python. Extra arguments are passed to the script as-is
# (e.g. -DPYARMOR_EXE=... for the editor).
function(plu_add_distribute_target NAME EXE_TARGET DIST_DIR)
    # Built from PLU_MODULES rather than spelled out, so a new engine module is packaged without
    # anyone remembering this list (a hand-written one silently left PluEffects out).
    set(_engine_lib_files "")
    foreach(_module IN LISTS PLU_MODULES PLU_THIRDPARTY_SHARED_LIBS)
        list(APPEND _engine_lib_files "$<TARGET_FILE:${_module}>")
    endforeach()

    set(_extra ${ARGN})
    if(WIN32)
        set(_target_windows ON)
        if(CMAKE_CROSSCOMPILING)
            list(APPEND _extra
                    -DPYTHON_RUNTIME_DIR=${PLU_WINDOWS_PYTHON_RUNTIME_DIR}
                    -DOBJDUMP=${PLU_LLVM_OBJDUMP}
            )
        endif()
    else()
        set(_target_windows OFF)
    endif()

    set(_vcpkg_dir "${CMAKE_BINARY_DIR}/vcpkg_installed/${VCPKG_TARGET_TRIPLET}")
    add_custom_target(${NAME}
            COMMAND ${CMAKE_COMMAND}
            -DDIST_DIR=${DIST_DIR}
            -DSOURCE_DIR=${CMAKE_SOURCE_DIR}
            -DEXE=$<TARGET_FILE:${EXE_TARGET}>
            # Every engine module, semicolon-separated. It was one $<TARGET_FILE:Engine> while the
            # engine was a single library; Engine is an INTERFACE target now and has no file.
            "-DENGINE_LIBS=$<JOIN:${_engine_lib_files},;>"
            -DVCPKG_LIB_DIR=${_vcpkg_dir}/lib
            -DVCPKG_BIN_DIR=${_vcpkg_dir}/bin
            -DTARGET_WINDOWS=${_target_windows}
            ${_extra}
            -P "${CMAKE_SOURCE_DIR}/cmake/PluDistribute.cmake"
            WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
            VERBATIM
            COMMENT "Packaging ${EXE_TARGET} into ${DIST_DIR}"
    )
    add_dependencies(${NAME} ${EXE_TARGET})
endfunction()
