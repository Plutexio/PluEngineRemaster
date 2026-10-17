# Windows builds imgui as a DLL (see the GImGui patch below for why); everywhere else it stays a
# static archive, as upstream ships it.
if (NOT VCPKG_TARGET_IS_WINDOWS)
    vcpkg_check_linkage(ONLY_STATIC_LIBRARY)
endif()

if ("docking-experimental" IN_LIST FEATURES)
    vcpkg_from_github(
        OUT_SOURCE_PATH SOURCE_PATH
        REPO ocornut/imgui
        REF "v${VERSION}-docking"
        SHA512 927ecf72f00a228e0899d5b8008575b44748c49b083b9425b5f2a6b4490a9900eae111afad23f2bf0a1c9c62cf1fea80c903eb3076d7e7ea901a5625f09df78e
        HEAD_REF docking
    )
else()
    vcpkg_from_github(
        OUT_SOURCE_PATH SOURCE_PATH
        REPO ocornut/imgui
        REF "v${VERSION}"
        SHA512 60eb4f8478ae998ae68efa33b2e3c9f331f5e373a1272472f93befd9fd6cab4ed73935bb540e728b5abb154469fbc6c0fd69f7aaf54cd3187eefede6cb145a10
        HEAD_REF master
    )
endif()

file(COPY "${CMAKE_CURRENT_LIST_DIR}/imgui-config.cmake.in" DESTINATION "${SOURCE_PATH}")
file(COPY "${CMAKE_CURRENT_LIST_DIR}/CMakeLists.txt" DESTINATION "${SOURCE_PATH}")

# Local overlay only: make ImGui's "current context" global thread-local.
#
# PluEngine runs one ImGui context per OS window, and two threads use them at the same time: Main
# builds each window's frame while the render thread submits the previous frame's draw data (see
# MULTITHREADING.md). With the stock single global they overwrite each other's current context —
# whichever thread called SetCurrentContext last wins, and the other one asserts or corrupts state.
# This is the mechanism ImGui documents for exactly this case (see the thread_local note in
# imgui.cpp). PluImGuiTLS is defined once, in LibEngine/PluRender/src/ImGuiRenderState.cpp.
#
# Patching the source (not just the installed header) matters: imgui's own translation units have
# to see the same definition of GImGui as the engine does.
#
# Windows needs a different shape. The engine is several DLLs, and a static imgui linked into each
# of them gives every DLL its own private copy of imgui (PE has no symbol interposition to merge
# them, unlike ELF), so the copies would not even agree on the current context. imgui is therefore
# one shared DLL there, exporting IMGUI_API. A thread_local variable cannot be dllimport-ed, so the
# per-thread context lives inside the imgui DLL behind an exported accessor, and GImGui expands to
# a call to it. PLU_IMGUI_TLS_IN_DLL tells the engine not to define PluImGuiTLS itself.
if (VCPKG_TARGET_IS_WINDOWS AND VCPKG_LIBRARY_LINKAGE STREQUAL "dynamic")
    vcpkg_replace_string("${SOURCE_PATH}/imconfig.h" "#pragma once"
        "#pragma once\n\n// PluEngine: imgui is a DLL, with a per-thread current context (see the overlay portfile).\n#ifdef PLU_IMGUI_EXPORTS\n#define IMGUI_API __declspec(dllexport)\n#else\n#define IMGUI_API __declspec(dllimport)\n#endif\nstruct ImGuiContext;\nIMGUI_API ImGuiContext*& PluImGuiCurrentContext();\n#define GImGui PluImGuiCurrentContext()\n#define PLU_IMGUI_TLS_IN_DLL 1")
    file(APPEND "${SOURCE_PATH}/imgui.cpp"
        "\n// PluEngine: storage for the per-thread current context (see imconfig.h).\nImGuiContext*& PluImGuiCurrentContext()\n{\n    static thread_local ImGuiContext* context = NULL;\n    return context;\n}\n")
    vcpkg_replace_string("${SOURCE_PATH}/CMakeLists.txt" "target_compile_features(\${PROJECT_NAME} PUBLIC cxx_std_11)"
        "target_compile_features(\${PROJECT_NAME} PUBLIC cxx_std_11)\ntarget_compile_definitions(\${PROJECT_NAME} PRIVATE PLU_IMGUI_EXPORTS)")
else()
    vcpkg_replace_string("${SOURCE_PATH}/imconfig.h" "#pragma once"
        "#pragma once\n\n// PluEngine: per-thread current ImGui context (see the overlay portfile).\nstruct ImGuiContext;\nextern thread_local ImGuiContext* PluImGuiTLS;\n#define GImGui PluImGuiTLS")
endif()

vcpkg_check_features(OUT_FEATURE_OPTIONS FEATURE_OPTIONS
    FEATURES 
    allegro5-binding            IMGUI_BUILD_ALLEGRO5_BINDING
    android-binding             IMGUI_BUILD_ANDROID_BINDING
    dx9-binding                 IMGUI_BUILD_DX9_BINDING
    dx10-binding                IMGUI_BUILD_DX10_BINDING
    dx11-binding                IMGUI_BUILD_DX11_BINDING
    dx12-binding                IMGUI_BUILD_DX12_BINDING
    glfw-binding                IMGUI_BUILD_GLFW_BINDING
    glut-binding                IMGUI_BUILD_GLUT_BINDING
    metal-binding               IMGUI_BUILD_METAL_BINDING
    opengl2-binding             IMGUI_BUILD_OPENGL2_BINDING
    opengl3-binding             IMGUI_BUILD_OPENGL3_BINDING
    osx-binding                 IMGUI_BUILD_OSX_BINDING
    sdl3-binding                IMGUI_BUILD_SDL3_BINDING
    sdlgpu3-binding             IMGUI_BUILD_SDLGPU3_BINDING
    sdl3-renderer-binding       IMGUI_BUILD_SDL3_RENDERER_BINDING
    vulkan-binding              IMGUI_BUILD_VULKAN_BINDING
    win32-binding               IMGUI_BUILD_WIN32_BINDING
    webgpu-binding              IMGUI_BUILD_WEBGPU_BINDING
    freetype                    IMGUI_FREETYPE
    freetype-svg                IMGUI_FREETYPE_SVG
    wchar32                     IMGUI_USE_WCHAR32
    test-engine                 IMGUI_TEST_ENGINE
)

if ("libigl-imgui" IN_LIST FEATURES)
    vcpkg_download_distfile(
        IMGUI_FONTS_DROID_SANS_H
        URLS
            https://raw.githubusercontent.com/libigl/libigl-imgui/c3efb9b62780f55f9bba34561f79a3087e057fc0/imgui_fonts_droid_sans.h
        FILENAME "imgui_fonts_droid_sans.h"
        SHA512
            abe9250c9a5989e0a3f2285bbcc83696ff8e38c1f5657c358e6fe616ff792d3c6e5ff2fa23c2eeae7d7b307392e0dc798a95d14f6d10f8e9bfbd7768d36d8b31
    )

    file(INSTALL "${IMGUI_FONTS_DROID_SANS_H}" DESTINATION "${CURRENT_PACKAGES_DIR}/include")
endif()

if ("test-engine" IN_LIST FEATURES)
    vcpkg_from_github(
        OUT_SOURCE_PATH TEST_ENGINE_SOURCE_PATH
        REPO ocornut/imgui_test_engine
        REF "v${VERSION}"
        SHA512 3a31bd3e1f86679ee60fc765971e96f76e05e530b3c7920aea284818604fd8a3cdf1fc7189e7e825a52a75446217ed6f1b2d7470d317799d381fb9019f14333b
        HEAD_REF master
    )

    file(REMOVE_RECURSE "${SOURCE_PATH}/test-engine")
    file(COPY "${TEST_ENGINE_SOURCE_PATH}/imgui_test_engine/" DESTINATION "${SOURCE_PATH}/test-engine")
    file(REMOVE_RECURSE "${SOURCE_PATH}/test-engine/thirdparty/stb")
    vcpkg_replace_string("${SOURCE_PATH}/test-engine/imgui_capture_tool.cpp" "//#define IMGUI_STB_IMAGE_WRITE_FILENAME \"my_folder/stb_image_write.h\"" "#define IMGUI_STB_IMAGE_WRITE_FILENAME <stb_image_write.h>\n#define STB_IMAGE_WRITE_STATIC")
    vcpkg_replace_string("${SOURCE_PATH}/imconfig.h" "#pragma once" "#pragma  once\n\n#include \"imgui_te_imconfig.h\"")
    vcpkg_replace_string("${SOURCE_PATH}/test-engine/imgui_te_imconfig.h" "#define IMGUI_TEST_ENGINE_ENABLE_COROUTINE_STDTHREAD_IMPL 0" "#define IMGUI_TEST_ENGINE_ENABLE_COROUTINE_STDTHREAD_IMPL 1")
endif()

vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS
        ${FEATURE_OPTIONS}
    OPTIONS_DEBUG
        -DIMGUI_SKIP_HEADERS=ON
)

vcpkg_cmake_install()

if ("freetype" IN_LIST FEATURES)
    vcpkg_replace_string("${CURRENT_PACKAGES_DIR}/include/imconfig.h" "//#define IMGUI_ENABLE_FREETYPE\n" "#define IMGUI_ENABLE_FREETYPE\n")
endif()
if ("freetype-svg" IN_LIST FEATURES)
    vcpkg_replace_string("${CURRENT_PACKAGES_DIR}/include/imconfig.h" "//#define IMGUI_ENABLE_FREETYPE_PLUTOSVG" "#define IMGUI_ENABLE_FREETYPE_PLUTOSVG")
endif()
if ("wchar32" IN_LIST FEATURES)
    vcpkg_replace_string("${CURRENT_PACKAGES_DIR}/include/imconfig.h" "//#define IMGUI_USE_WCHAR32" "#define IMGUI_USE_WCHAR32")
endif()

vcpkg_copy_pdbs()
vcpkg_cmake_config_fixup()

if ("test-engine" IN_LIST FEATURES)
    vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE.txt" "${SOURCE_PATH}/test-engine/LICENSE.txt")
else()
    vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE.txt")
endif()
