# x64 Windows (MSVC ABI, dynamic CRT and libraries — the same as the stock x64-windows triplet), built
# on a Linux host with clang-cl + lld-link against an xwin sysroot. See
# cmake/Toolchains/WindowsCrossClangCL.cmake for the one-time setup.
#
# Release only: the cross presets are distribution builds, and the MSVC debug CRT is not part of an
# xwin splat by default anyway.

set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE dynamic)
set(VCPKG_BUILD_TYPE release)

set(VCPKG_CHAINLOAD_TOOLCHAIN_FILE "${CMAKE_CURRENT_LIST_DIR}/../cmake/Toolchains/WindowsCrossClangCL.vcpkg.cmake")
set(VCPKG_ENV_PASSTHROUGH PLU_XWIN_DIR)
