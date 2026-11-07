# Cross-compiles for x64 Windows (MSVC ABI) from a Linux host: clang-cl + lld-link against the MSVC CRT
# and Windows SDK downloaded by xwin (https://github.com/Jake-Shadle/xwin). The result is the same kind
# of binary a native Visual Studio build produces — same CRT, same C++ ABI, same python3xx.dll — so the
# code paths are the ones the native Windows build already takes (_WIN32, WinAPI backend, no SDL).
#
# Used by the PluDistWindowsCross-* presets as VCPKG_CHAINLOAD_TOOLCHAIN_FILE, and included by
# WindowsCrossClangCL.vcpkg.cmake, the toolchain vcpkg builds the ports of the x64-windows-xwin triplet
# with.
#
# One-time setup (needs cargo):
#   cargo install --locked xwin
#   xwin --accept-license --arch x86_64 splat --use-winsysroot-style --preserve-ms-arch-notation \
#        --output ~/.local/share/xwin
# Keep xwin's --cache-dir on the same filesystem as --output, or pass `splat --copy`: splat moves files
# out of the cache and a move across filesystems (e.g. a cache on tmpfs) fails.
# Release only: the MSVC debug CRT (msvcrtd/ucrtd) is not redistributable and xwin leaves it out
# unless asked with --include-debug-libs.
#
# The sysroot is $ENV{PLU_XWIN_DIR}, or ~/.local/share/xwin when that is not set.

include_guard(GLOBAL)

set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR AMD64)
set(CMAKE_SYSTEM_VERSION 10.0)

if(DEFINED ENV{PLU_XWIN_DIR} AND NOT "$ENV{PLU_XWIN_DIR}" STREQUAL "")
    file(TO_CMAKE_PATH "$ENV{PLU_XWIN_DIR}" PLU_XWIN_DIR)
else()
    set(PLU_XWIN_DIR "$ENV{HOME}/.local/share/xwin")
endif()

file(GLOB PLU_XWIN_MSVC_DIRS LIST_DIRECTORIES true "${PLU_XWIN_DIR}/VC/Tools/MSVC/*")
file(GLOB PLU_XWIN_SDK_INCLUDE_DIRS LIST_DIRECTORIES true "${PLU_XWIN_DIR}/Windows Kits/10/Include/*")
if(NOT PLU_XWIN_MSVC_DIRS OR NOT PLU_XWIN_SDK_INCLUDE_DIRS)
    message(FATAL_ERROR
            "Windows cross toolchain: no MSVC CRT / Windows SDK in ${PLU_XWIN_DIR}.\n"
            "Download them with xwin (one time, ~600 MB, accepts the Microsoft license):\n"
            "  cargo install --locked xwin\n"
            "  xwin --accept-license --arch x86_64 splat --use-winsysroot-style "
            "--preserve-ms-arch-notation --output ~/.local/share/xwin\n"
            "or point PLU_XWIN_DIR at an existing splat.")
endif()
# Newest version wins if several were splatted over each other.
list(SORT PLU_XWIN_MSVC_DIRS COMPARE NATURAL ORDER DESCENDING)
list(GET PLU_XWIN_MSVC_DIRS 0 _plu_xwin_msvc)
list(SORT PLU_XWIN_SDK_INCLUDE_DIRS COMPARE NATURAL ORDER DESCENDING)
list(GET PLU_XWIN_SDK_INCLUDE_DIRS 0 _plu_xwin_sdk_include)

foreach(_tool clang-cl lld-link llvm-lib llvm-mt)
    string(TOUPPER "${_tool}" _var)
    string(REPLACE "-" "_" _var "PLU_${_var}")
    find_program(${_var} NAMES ${_tool} REQUIRED)
endforeach()

set(CMAKE_C_COMPILER "${PLU_CLANG_CL}")
set(CMAKE_CXX_COMPILER "${PLU_CLANG_CL}")
set(CMAKE_LINKER "${PLU_LLD_LINK}")
set(CMAKE_AR "${PLU_LLVM_LIB}")
# A wrapper that adds the code page (UTF-8, falling back to Windows-1252); see the script for why it
# cannot go into CMAKE_RC_FLAGS.
set(CMAKE_RC_COMPILER "${CMAKE_CURRENT_LIST_DIR}/llvm-rc-codepage")
set(CMAKE_MT "${PLU_LLVM_MT}")

# clang-cl on a Linux host would otherwise target the host triple. /winsysroot finds the CRT and SDK
# headers in the xwin layout, /winsysroot: the libraries.
set(PLU_XWIN_COMPILE_FLAGS "--target=x86_64-pc-windows-msvc /winsysroot ${PLU_XWIN_DIR}")
set(PLU_XWIN_LINK_FLAGS "/winsysroot:${PLU_XWIN_DIR}")
# llvm-rc knows nothing about /winsysroot; resource scripts include <windows.h> and friends. The SDK
# path has a space ("Windows Kits"), hence the quotes.
set(PLU_XWIN_RC_FLAGS " -I \"${_plu_xwin_msvc}/include\"")
foreach(_sub um shared ucrt winrt)
    string(APPEND PLU_XWIN_RC_FLAGS " -I \"${_plu_xwin_sdk_include}/${_sub}\"")
endforeach()

# try_compile (compiler checks, check_cxx_source_compiles, ...) builds in Debug unless told otherwise,
# which links the debug CRT (msvcrtd.lib) — not in the splat.
set(CMAKE_TRY_COMPILE_CONFIGURATION Release)

string(APPEND CMAKE_C_FLAGS_INIT " ${PLU_XWIN_COMPILE_FLAGS}")
string(APPEND CMAKE_CXX_FLAGS_INIT " ${PLU_XWIN_COMPILE_FLAGS}")
string(APPEND CMAKE_EXE_LINKER_FLAGS_INIT " ${PLU_XWIN_LINK_FLAGS}")
string(APPEND CMAKE_SHARED_LINKER_FLAGS_INIT " ${PLU_XWIN_LINK_FLAGS}")
string(APPEND CMAKE_MODULE_LINKER_FLAGS_INIT " ${PLU_XWIN_LINK_FLAGS}")
string(APPEND CMAKE_RC_FLAGS_INIT " ${PLU_XWIN_RC_FLAGS}")

# Host programs (python, git, ninja) come from the host; libraries and headers from the targets
# (vcpkg_installed is added to the search roots by the vcpkg toolchain).
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)

# PE dependency resolution (file(GET_RUNTIME_DEPENDENCIES) in the distribution scripts) from a Linux
# host: parse the imports with llvm-objdump instead of the host's ELF tools.
find_program(PLU_LLVM_OBJDUMP NAMES llvm-objdump)
