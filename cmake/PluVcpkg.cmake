# vcpkg bootstrap, included from the root CMakeLists.txt before project().
#
# The engine builds against its own vcpkg checkout in <source>/vcpkg, pinned to PLU_VCPKG_COMMIT.
# Every configure makes that checkout match the pin: it clones vcpkg if it is missing, fetches and
# checks out the pinned commit if HEAD differs, and re-runs the bootstrap when the vcpkg executable
# does not match scripts/vcpkg-tool-metadata.txt of that commit.
#
# Why a pin and not "git pull": vcpkg.json's builtin-baseline already fixes the *port versions*,
# but the vcpkg tool and its scripts/ (vcpkg_cmake_configure, ports.cmake, ...) come from whatever
# is checked out, and they feed into every package's ABI hash. Pulling master therefore changes the
# hashes of the whole dependency tree and rebuilds it (and the overlay ports in vcpkg-overlays/ can
# break against newer scripts). With the pin, vcpkg only moves when the pin is changed in a commit,
# and a `git pull` of this repository brings every machine to the same vcpkg on the next configure.
#
# Updating vcpkg (deliberate, one commit):
#   git -C vcpkg fetch origin && git -C vcpkg rev-parse origin/master
#   -> put that hash into PLU_VCPKG_COMMIT below and reconfigure.
#   Bump "builtin-baseline" in vcpkg.json as well if the ports should move to newer versions, and
#   check whether the overlay ports in vcpkg-overlays/ are still needed / still apply.
#
# Options:
#   PLU_VCPKG_USE_ENV_ROOT  use $ENV{VCPKG_ROOT} as-is instead of the pinned checkout (no git
#                           operations). OFF by default: Visual Studio's developer shells set
#                           VCPKG_ROOT to their bundled vcpkg, which must not be picked up silently.
#   PLU_VCPKG_SYNC          OFF leaves the local checkout alone (e.g. offline, or while testing
#                           another vcpkg commit by hand). Configure still warns about a mismatch.

set(PLU_VCPKG_COMMIT "b8b8df2201ad8509b81a830fe0957bcb98e06c27")
set(PLU_VCPKG_REPO_URL "https://github.com/microsoft/vcpkg.git")

option(PLU_VCPKG_USE_ENV_ROOT "Use \$ENV{VCPKG_ROOT} instead of the pinned vcpkg checkout" OFF)
option(PLU_VCPKG_SYNC "Keep <source>/vcpkg checked out at PLU_VCPKG_COMMIT" ON)

function(plu_vcpkg_git OUT_RESULT OUT_OUTPUT)
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" -C "${PLU_VCPKG_DIR}" ${ARGN}
        RESULT_VARIABLE _result
        OUTPUT_VARIABLE _output
        ERROR_VARIABLE _error
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_STRIP_TRAILING_WHITESPACE
    )
    set(${OUT_RESULT} "${_result}" PARENT_SCOPE)
    if(_result EQUAL 0)
        set(${OUT_OUTPUT} "${_output}" PARENT_SCOPE)
    else()
        set(${OUT_OUTPUT} "${_error}" PARENT_SCOPE)
    endif()
endfunction()

# Makes sure a commit exists in the local vcpkg clone, fetching from origin if it does not.
function(plu_vcpkg_ensure_commit COMMIT WHAT)
    plu_vcpkg_git(_result _output cat-file -e "${COMMIT}^{commit}")
    if(_result EQUAL 0)
        return()
    endif()

    message(STATUS "vcpkg: ${WHAT} ${COMMIT} not in the local clone, fetching...")
    plu_vcpkg_git(_result _output fetch --quiet origin)
    plu_vcpkg_git(_result _output cat-file -e "${COMMIT}^{commit}")
    if(NOT _result EQUAL 0)
        # Not reachable from origin's branches (e.g. a commit from a PR) - ask for it directly.
        plu_vcpkg_git(_result _output fetch --quiet origin "${COMMIT}")
    endif()
    if(NOT _result EQUAL 0)
        message(FATAL_ERROR "vcpkg: could not fetch ${WHAT} ${COMMIT} from origin:\n${_output}\n"
                            "Check the network connection, or configure with -DPLU_VCPKG_SYNC=OFF "
                            "to build against the current checkout.")
    endif()
endfunction()

function(plu_vcpkg_sync_checkout)
    find_package(Git QUIET)
    if(NOT GIT_FOUND)
        message(FATAL_ERROR "vcpkg: git is required to fetch and pin the vcpkg checkout.")
    endif()

    if(NOT EXISTS "${PLU_VCPKG_DIR}/.git")
        file(GLOB _existing "${PLU_VCPKG_DIR}/*")
        if(_existing)
            message(FATAL_ERROR "vcpkg: ${PLU_VCPKG_DIR} exists but is not a git clone. "
                                "Remove it (or move it away) and configure again.")
        endif()
        message(STATUS "vcpkg: cloning ${PLU_VCPKG_REPO_URL} into ${PLU_VCPKG_DIR}...")
        execute_process(
            COMMAND "${GIT_EXECUTABLE}" clone "${PLU_VCPKG_REPO_URL}" "${PLU_VCPKG_DIR}"
            RESULT_VARIABLE _result
        )
        if(NOT _result EQUAL 0)
            file(REMOVE_RECURSE "${PLU_VCPKG_DIR}")
            message(FATAL_ERROR "vcpkg: git clone failed (exit code ${_result}).")
        endif()
    endif()

    plu_vcpkg_git(_result _head rev-parse HEAD)
    if(NOT _result EQUAL 0)
        message(FATAL_ERROR "vcpkg: cannot read HEAD of ${PLU_VCPKG_DIR}:\n${_head}")
    endif()

    if(NOT _head STREQUAL PLU_VCPKG_COMMIT)
        if(NOT PLU_VCPKG_SYNC)
            message(WARNING "vcpkg: checkout is at ${_head}, the pin is ${PLU_VCPKG_COMMIT} "
                            "(PLU_VCPKG_SYNC=OFF, leaving it alone).")
        else()
            # Local edits in vcpkg/ports do nothing in manifest mode anyway (use vcpkg-overlays/),
            # but never throw away someone's changes silently.
            plu_vcpkg_git(_result _dirty status --porcelain --untracked-files=no)
            if(NOT _dirty STREQUAL "")
                message(FATAL_ERROR "vcpkg: ${PLU_VCPKG_DIR} has local changes, refusing to move it "
                                    "to ${PLU_VCPKG_COMMIT}:\n${_dirty}\n"
                                    "Discard them (git -C vcpkg checkout -- .) or configure with "
                                    "-DPLU_VCPKG_SYNC=OFF.")
            endif()

            plu_vcpkg_ensure_commit("${PLU_VCPKG_COMMIT}" "pinned commit")
            message(STATUS "vcpkg: moving checkout ${_head} -> ${PLU_VCPKG_COMMIT}")
            plu_vcpkg_git(_result _output -c advice.detachedHead=false checkout --quiet --detach "${PLU_VCPKG_COMMIT}")
            if(NOT _result EQUAL 0)
                message(FATAL_ERROR "vcpkg: checkout of ${PLU_VCPKG_COMMIT} failed:\n${_output}")
            endif()
        endif()
    endif()

    # Manifest mode resolves port versions from the builtin-baseline commit's git history.
    file(READ "${CMAKE_SOURCE_DIR}/vcpkg.json" _manifest)
    string(JSON _baseline ERROR_VARIABLE _json_error GET "${_manifest}" builtin-baseline)
    if(_baseline AND NOT _json_error)
        plu_vcpkg_ensure_commit("${_baseline}" "builtin-baseline")
    endif()
endfunction()

# Re-runs bootstrap-vcpkg when the executable is missing or older/newer than the checkout expects.
function(plu_vcpkg_bootstrap_if_needed)
    if(CMAKE_HOST_WIN32)
        set(_exe "${PLU_VCPKG_DIR}/vcpkg.exe")
        set(_bootstrap "${PLU_VCPKG_DIR}/bootstrap-vcpkg.bat")
    else()
        set(_exe "${PLU_VCPKG_DIR}/vcpkg")
        set(_bootstrap "${PLU_VCPKG_DIR}/bootstrap-vcpkg.sh")
    endif()

    file(STRINGS "${PLU_VCPKG_DIR}/scripts/vcpkg-tool-metadata.txt" _tag_line REGEX "^VCPKG_TOOL_RELEASE_TAG=")
    string(REPLACE "VCPKG_TOOL_RELEASE_TAG=" "" _wanted "${_tag_line}")

    set(_have "")
    if(EXISTS "${_exe}")
        execute_process(
            COMMAND "${_exe}" version
            OUTPUT_VARIABLE _version_output
            ERROR_QUIET
        )
        string(REGEX MATCH "[0-9][0-9][0-9][0-9]-[0-9][0-9]-[0-9][0-9]" _have "${_version_output}")
    endif()

    if(_have STREQUAL _wanted AND NOT _wanted STREQUAL "")
        return()
    endif()

    if(_have STREQUAL "")
        message(STATUS "vcpkg: no usable executable, bootstrapping (${_wanted})...")
    else()
        message(STATUS "vcpkg: executable is ${_have}, checkout expects ${_wanted}, bootstrapping...")
    endif()
    execute_process(
        COMMAND "${_bootstrap}" -disableMetrics
        WORKING_DIRECTORY "${PLU_VCPKG_DIR}"
        RESULT_VARIABLE _result
    )
    if(NOT _result EQUAL 0 OR NOT EXISTS "${_exe}")
        message(FATAL_ERROR "vcpkg: bootstrap failed (exit code ${_result}).")
    endif()
endfunction()

if(PLU_VCPKG_USE_ENV_ROOT)
    if(NOT DEFINED ENV{VCPKG_ROOT} OR NOT EXISTS "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake")
        message(FATAL_ERROR "vcpkg: PLU_VCPKG_USE_ENV_ROOT is ON but VCPKG_ROOT does not point to a vcpkg root.")
    endif()
    file(TO_CMAKE_PATH "$ENV{VCPKG_ROOT}" PLU_VCPKG_DIR)
    message(STATUS "vcpkg: using VCPKG_ROOT as-is: ${PLU_VCPKG_DIR} (not pinned)")
else()
    set(PLU_VCPKG_DIR "${CMAKE_SOURCE_DIR}/vcpkg")
    plu_vcpkg_sync_checkout()
    plu_vcpkg_bootstrap_if_needed()
    message(STATUS "vcpkg: ${PLU_VCPKG_DIR} @ ${PLU_VCPKG_COMMIT}")
endif()

set(VCPKG_OVERLAY_PORTS "${CMAKE_SOURCE_DIR}/vcpkg-overlays" CACHE STRING "")
# FORCE: CLion's vcpkg integration injects its own toolchain path; the engine must build against
# the vcpkg chosen above.
set(CMAKE_TOOLCHAIN_FILE "${PLU_VCPKG_DIR}/scripts/buildsystems/vcpkg.cmake" CACHE FILEPATH "" FORCE)
