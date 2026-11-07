# The toolchain vcpkg builds the ports of the x64-windows-xwin triplet (vcpkg-triplets/) with.
#
# It is WindowsCrossClangCL.cmake (clang-cl, lld-link and the xwin sysroot) plus vcpkg's own
# scripts/toolchains/windows.cmake, so the ports get exactly the flags a native x64-windows build
# gives them (/MD, /utf-8, /EHsc, /O2 /Z7, ...). windows.cmake writes CMAKE_<LANG>_FLAGS into the cache
# itself, which makes CMake ignore the *_FLAGS_INIT values the base toolchain sets — so the sysroot
# flags travel through the VCPKG_*_FLAGS variables windows.cmake appends instead.

include("${CMAKE_CURRENT_LIST_DIR}/WindowsCrossClangCL.cmake")

# This file runs again for every try_compile, which forwards the already-extended VCPKG_* values.
if(NOT VCPKG_C_FLAGS MATCHES "winsysroot")
    string(APPEND VCPKG_C_FLAGS " ${PLU_XWIN_COMPILE_FLAGS}")
endif()
if(NOT VCPKG_CXX_FLAGS MATCHES "winsysroot")
    string(APPEND VCPKG_CXX_FLAGS " ${PLU_XWIN_COMPILE_FLAGS}")
endif()
if(NOT VCPKG_LINKER_FLAGS MATCHES "winsysroot")
    string(APPEND VCPKG_LINKER_FLAGS " ${PLU_XWIN_LINK_FLAGS}")
endif()
# windows.cmake sets this one without FORCE, so a value already in the cache wins. Its "/c65001"
# (UTF-8 code page) moves into the llvm-rc-codepage wrapper: CMake feeds the RC flags to the clang-cl
# preprocessor as well, and on a Linux host clang-cl takes an unknown /option for a file path.
set(CMAKE_RC_FLAGS "-DWIN32 ${PLU_XWIN_RC_FLAGS}" CACHE STRING "")

# vcpkg includes this file from scripts/buildsystems/vcpkg.cmake before it has worked out its own root,
# and inside try_compile even CMAKE_TOOLCHAIN_FILE is empty at that point — but the including file is
# always vcpkg.cmake, so find windows.cmake next to it.
get_filename_component(_plu_vcpkg_scripts "${CMAKE_PARENT_LIST_FILE}" DIRECTORY)
get_filename_component(_plu_vcpkg_scripts "${_plu_vcpkg_scripts}" DIRECTORY)
if(NOT EXISTS "${_plu_vcpkg_scripts}/toolchains/windows.cmake")
    message(FATAL_ERROR "WindowsCrossClangCL.vcpkg.cmake is meant for vcpkg port builds only "
                        "(included from ${CMAKE_PARENT_LIST_FILE}).")
endif()
include("${_plu_vcpkg_scripts}/toolchains/windows.cmake")
