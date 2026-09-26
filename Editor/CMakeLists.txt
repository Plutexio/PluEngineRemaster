project(PluEditor)

# Bootstrap reflection sources on the first configure so the GLOB below has files to
# collect. On later builds the PRE_BUILD command keeps them regenerated, so we skip the
# configure-time run to avoid generating twice whenever CMake reconfigures.
if(NOT EXISTS "${CMAKE_SOURCE_DIR}/ReflectionCache/Editor")
    execute_process(
            COMMAND ${VENV_EXECUTABLE} "${CMAKE_SOURCE_DIR}/PythonTools/ReflectionGeneratorRegex.py" -bp Editor
            WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    )
endif()

file(GLOB_RECURSE SOURCES_REFLECTION CONFIGURE_DEPENDS "../ReflectionCache/Editor/*.cpp")
#file(GLOB_RECURSE HEADERS CONFIGURE_DEPENDS "./*.h")
#file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS "./*.cpp")

set(PLU_THIRDPARTY_DIR "${CMAKE_SOURCE_DIR}/ThirdParty")

add_executable(PluEditor WIN32 ${SOURCES_REFLECTION}
        ./DefinedPanels/EditorSettingsPanel.cpp
        ./DefinedPanels/EngineClassTreePanel.cpp
        ./DefinedPanels/EngineStatsPanel.cpp
        ./DefinedPanels/InputViewerPanel.cpp
        ./DefinedPanels/LoadedShadersPanel.cpp
        ./DefinedPanels/LoadedAssetsPanel.cpp
        ./DefinedPanels/LoadedObjectsPanel.cpp
        ./DefinedPanels/RenderGpuStatsPanel.cpp
        ./DefinedPanels/ParticlesDebugPanel.cpp
        ./DefinedPanels/Project/AssetBrowserPanel/AssetBrowserPanel.cpp
        ./DefinedPanels/ProfilerPanel.cpp
        ./DefinedPanels/ProjectLauncherPanel.cpp
        ./DefinedPanels/Style/EditorStylePanel.cpp
        ./DefinedViewports/Animation/AnimationPreviewPanel.cpp
        ./DefinedViewports/Animation/AnimationTimelinePanel.cpp
        ./DefinedViewports/Animation/AnimationViewport.cpp
        ./DefinedViewports/AnimationGraph/AnimationGraphDetailsPanel.cpp
        ./DefinedViewports/AnimationGraph/AnimationGraphVariablesPanel.cpp
        ./DefinedViewports/AnimationGraph/AnimationGraphViewport.cpp
        ./DefinedViewports/AnimationGraph/AnimationGraphViewportPanel.cpp
        ./NodeGraph/NodeGraphEditor.cpp
        ./DefinedViewports/ParticleSystem/ParticleDetailsPanel.cpp
        ./DefinedViewports/ParticleSystem/ParticleEmittersPanel.cpp
        ./DefinedViewports/ParticleSystem/ParticleGraphPanel.cpp
        ./DefinedViewports/ParticleSystem/ParticleParametersPanel.cpp
        ./DefinedViewports/ParticleSystem/ParticlePreviewPanel.cpp
        ./DefinedViewports/ParticleSystem/ParticleSystemViewport.cpp
        ./DefinedViewports/Material/MaterialDetailsPanel.cpp
        ./DefinedViewports/Material/MaterialViewport.cpp
        ./DefinedViewports/Scene/SceneObjectDetailsPanel.cpp
        ./DefinedViewports/Scene/SceneStructurePanel.cpp
        ./DefinedViewports/Scene/SceneViewport.cpp
        ./DefinedViewports/Scene/SceneViewportPanel.cpp
        ./DefinedViewports/Scene/SceneWorldSettings.cpp
        ./DefinedViewports/SkeletalMesh/SkeletalMeshDetailsPanel.cpp
        ./DefinedViewports/SkeletalMesh/SkeletalMeshViewport.cpp
        ./DefinedViewports/SkeletalMesh/SkeletalMeshViewportPanel.cpp
        ./DefinedViewports/Skeleton/SkeletonHierarchyPanel.cpp
        ./DefinedViewports/Skeleton/SkeletonViewport.cpp
        ./DefinedViewports/Skeleton/SkeletonViewportPanel.cpp
        ./DefinedViewports/StaticMesh/StaticMeshDetailsPanel.cpp
        ./DefinedViewports/StaticMesh/StaticMeshViewport.cpp
        ./DefinedViewports/StaticMesh/StaticMeshViewportPanel.cpp
        ./DefinedViewports/Texture/TextureDisplayPanel.cpp
        ./DefinedViewports/Texture/TextureViewport.cpp
        ./EditorApp.cpp
        ./EditorSettings/EditorSettingsManager.cpp
        ./EditorViewports/EditorViewportManager.cpp
        ./EditorViewports/IEditorPanel.cpp
        ./EditorViewports/IEditorViewport.cpp
        ./EditorWindows/EditorWindowsManager.cpp
        ./Mains/LinuxMain.cpp
        ./Mains/WindowsMain.cpp
        ./Managers/Assets/EditorAssetManager.cpp
        ./Managers/Project/EditorProjectManager.cpp
        ./Managers/Python/EditorPythonManager.cpp
        ./Managers/Scene/EditorCamera.cpp
        ./Managers/Shaders/EditorShaderCode.cpp
        ./Managers/Shaders/EditorShaderManager.cpp
        ./Panels/EditorPanel.cpp
        ./Panels/EditorPanelManager.cpp

        ./DefinedPanels/EditorSettingsPanel.h
        ./DefinedPanels/EngineClassTreePanel.h
        ./DefinedPanels/EngineStatsPanel.h
        ./DefinedPanels/InputViewerPanel.h
        ./DefinedPanels/Project/AssetBrowserPanel/AssetBrowserPanel.h
        ./DefinedPanels/ProjectLauncherPanel.h
        ./DefinedPanels/Style/EditorStylePanel.h
        ./DefinedViewports/Animation/AnimationTimelinePanel.h
        ./DefinedViewports/Animation/AnimationViewport.h
        ./DefinedViewports/Material/MaterialDetailsPanel.h
        ./DefinedViewports/Material/MaterialViewport.h
        ./DefinedViewports/Scene/SceneObjectDetailsPanel.h
        ./DefinedViewports/Scene/SceneStructurePanel.h
        ./DefinedViewports/Scene/SceneViewport.h
        ./DefinedViewports/Scene/SceneViewportPanel.h
        ./DefinedViewports/Scene/SceneWorldSettings.h
        ./DefinedViewports/SkeletalMesh/SkeletalMeshDetailsPanel.h
        ./DefinedViewports/SkeletalMesh/SkeletalMeshViewport.h
        ./DefinedViewports/SkeletalMesh/SkeletalMeshViewportPanel.h
        ./DefinedViewports/StaticMesh/StaticMeshDetailsPanel.h
        ./DefinedViewports/StaticMesh/StaticMeshViewport.h
        ./DefinedViewports/StaticMesh/StaticMeshViewportPanel.h
        ./DefinedViewports/Texture/TextureDisplayPanel.h
        ./DefinedViewports/Texture/TextureViewport.h
        ./DefinedViewports/AnimationGraph/AnimationGraphDetailsPanel.h
        ./DefinedViewports/AnimationGraph/AnimationGraphViewport.h
        ./DefinedViewports/AnimationGraph/AnimationGraphViewportPanel.h
        ./DefinedViewports/ParticleSystem/ParticleDetailsPanel.h
        ./DefinedViewports/ParticleSystem/ParticleEmittersPanel.h
        ./DefinedViewports/ParticleSystem/ParticleGraphPanel.h
        ./DefinedViewports/ParticleSystem/ParticleParametersPanel.h
        ./DefinedViewports/ParticleSystem/ParticlePreviewPanel.h
        ./DefinedViewports/ParticleSystem/ParticleSystemViewport.h
        ./DefinedViewports/ParticleSystem/ParticleModuleNodeView.h
        ./DefinedViewports/ParticleSystem/ParticleCurveParamNodeView.h
        ./EditorApp.h
        ./EditorAppContext.h
        ./EditorInterface.h
        ./EditorSettings/EditorSettings.h
        ./EditorSettings/EditorSettingsManager.h
        ./EditorViewports/EditorViewportManager.h
        ./EditorViewports/IEditorPanel.h
        ./EditorViewports/IEditorViewport.h
        ./EditorWindows/EditorWindowsManager.h
        ./Managers/Assets/EditorAssetImporter.h
        ./Managers/Assets/EditorAssetManager.h
        ./Managers/Project/EditorProjectManager.h
        ./Managers/Project/PythonBuildEnvironment.h
        ./Managers/Python/EditorPythonManager.h
        ./Managers/Scene/EditorCamera.h
        ./Managers/Shaders/EditorShaderCode.h
        ./Managers/Shaders/EditorShaderManager.h
        ./Panels/EditorPanel.h
        ./Panels/EditorPanelManager.h
        ./Utils/RGBTransformDragger.h
        DefinedPanels/TextureViewerPanel.h
        DefinedPanels/TextureViewerPanel.cpp
        Managers/Assets/EditorAssetCreator.cpp
        Managers/Assets/EditorAssetCreator.h
        Managers/Assets/EditorAssetImporter.cpp
        Managers/Assets/EditorAssetImporter.h
        DefinedPanels/Project/ProjectSettings/ProjectSettingsPanel.h
        DefinedPanels/Project/ProjectSettings/ProjectSettingsPanel.cpp
        DefinedViewports/ShaderProgram/ShaderProgramInfoViewport.cpp
        DefinedViewports/ShaderProgram/ShaderProgramInfoViewport.h
        DefinedViewports/ShaderProgram/ShaderProgramInfoPanel.cpp
        DefinedViewports/ShaderProgram/ShaderProgramInfoPanel.h
        Utils/GizmoUtils.h
        Utils/RandomTransformUtils.h
        Utils/RandomTransformUtils.cpp
)

target_compile_definitions(PluEditor PUBLIC PLU_EDITOR)

add_custom_command(TARGET PluEditor
        PRE_BUILD
        COMMENT "Generating Reflection Data"
        COMMAND ${VENV_EXECUTABLE} "${CMAKE_SOURCE_DIR}/PythonTools/ReflectionGeneratorRegex.py" -bp Editor
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
)

set_target_properties(PluEditor PROPERTIES UNITY_BUILD ON)

find_package(argparse CONFIG REQUIRED)

target_include_directories(PluEditor PUBLIC ../ReflectionCache/Editor)
target_include_directories(PluEditor PUBLIC ../Editor)

target_link_libraries(PluEditor PRIVATE Engine argparse::argparse)
find_package(Jolt CONFIG REQUIRED)
target_link_libraries(PluEditor PRIVATE Jolt::Jolt)
target_compile_definitions(PluEditor PRIVATE JPH_DEBUG_RENDERER)

target_precompile_headers(PluEditor PUBLIC "${CMAKE_SOURCE_DIR}/PluSTL/PluSTL_FWD.h")
target_precompile_headers(PluEditor PUBLIC "${CMAKE_SOURCE_DIR}/LibEngine/PluCore/include/PluEngine/Core.h")
target_precompile_headers(PluEditor PUBLIC "${CMAKE_SOURCE_DIR}/LibEngine/PluCore/include/PluEngine/Log.h")
target_precompile_headers(PluEditor PUBLIC "${CMAKE_SOURCE_DIR}/LibEngine/PluCore/include/PluEngine/Timer.h")
target_precompile_headers(PluEditor PUBLIC "${CMAKE_SOURCE_DIR}/LibEngine/PluCore/include/stdpch.h")

target_compile_options(PluEditor PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang>:-include ${CMAKE_SOURCE_DIR}/LibEngine/PluCore/include/stdpch.h>
        # Generated reflection/binding translation units can exceed the COFF
        # per-object section limit (65279) on MSVC → need /bigobj.
        $<$<CXX_COMPILER_ID:MSVC>:/bigobj>
)

find_package(efsw CONFIG REQUIRED)
target_link_libraries(PluEditor PRIVATE efsw::efsw)

find_package(nfd CONFIG REQUIRED)
target_link_libraries(PluEditor PRIVATE nfd::nfd)

# imgui-node-editor (thedmd) — canvas dla AnimationGraph viewportu. Edytor-only,
# statyczna lib z vcpkg (patrz vcpkg.json). Nagłówki: <imgui-node-editor/imgui_node_editor.h>.
find_package(unofficial-imgui-node-editor CONFIG REQUIRED)
target_link_libraries(PluEditor PRIVATE unofficial::imgui-node-editor::imgui-node-editor)

set_target_properties(PluEditor PROPERTIES
        # Szukaj .so w tym samym folderze co executable
        # Distributed layout: every runtime library sits next to the executable.
        INSTALL_RPATH "$ORIGIN"
        # But not while building. The vcpkg dependencies are dynamic now (x64-linux-dynamic), so a
        # binary in the build tree has to reach into vcpkg_installed/ for libJolt.so and friends --
        # which is exactly what CMake's automatic build RPATH does. Forcing the install RPATH here
        # instead would leave the loader with nothing but $ORIGIN and the app would not start.
        BUILD_WITH_INSTALL_RPATH FALSE
        # Keeps libEngine.so, which the POST_BUILD step copies next to the executable, findable too.
        BUILD_RPATH "$ORIGIN"
)


# Engine is an INTERFACE target now, so there is no single $<TARGET_FILE:Engine> to copy: every
# module is its own shared library and all of them have to sit next to the executable, which is
# what the $ORIGIN RPATH expects to find. glad is shared for the same reason (one function-pointer
# table for the whole process) and travels with them.
foreach (PLU_MODULE IN LISTS PLU_MODULES)
    add_custom_command(TARGET PluEditor POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:${PLU_MODULE}>
            $<TARGET_FILE_DIR:PluEditor>
    )
endforeach ()
add_custom_command(TARGET PluEditor POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:glad>
        $<TARGET_FILE_DIR:PluEditor>
)

add_custom_command(TARGET PluEditor POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_SOURCE_DIR}/ReflectionCache/PluEngine.pyi
        $<TARGET_FILE_DIR:${PROJECT_NAME}>
)


# =====================================================================================
#  Editor distribution (target: DistributeEditor)
#
#  Only enabled when configured with PLU_DISTRIBUTION=ON (see the PluDist*-Editor presets).
#  Assembles a self-contained editor into ${CMAKE_SOURCE_DIR}/EditorDist:
#    - PluEditor(.exe) + every engine module library and glad (+ vcpkg runtime libraries)
#    - EngineAssets/, ThirdParty/UI/ (fonts + icons), PluEngine.pyi
#    - obfuscated (PyArmor) ShaderCodeParser.py in PythonTools/ — the only Python
#      tool run at editor runtime (via the embedded interpreter).
#
#  Runtime resource paths come from GetEngineResourcesDir() == the directory next to the
#  exe, because in this configuration Engine is built without PLU_PROJECT_ROOT.
# =====================================================================================
if(PLU_DISTRIBUTION)
    set(PLU_EDITOR_DIST_DIR "${CMAKE_SOURCE_DIR}/EditorDist")

    if(WIN32)
        set(PLU_PYARMOR_EXE "${VENV_PATH}/${VENV_BIN}/pyarmor.exe")
    else()
        set(PLU_PYARMOR_EXE "${VENV_PATH}/${VENV_BIN}/pyarmor")
    endif()

    # Built from PLU_MODULES rather than spelled out, so a new engine module is packaged without
    # anyone remembering this list (a hand-written one silently left PluEffects out).
    set(PLU_ENGINE_LIB_FILES "")
    foreach (PLU_MODULE IN LISTS PLU_MODULES)
        list(APPEND PLU_ENGINE_LIB_FILES "$<TARGET_FILE:${PLU_MODULE}>")
    endforeach ()
    list(APPEND PLU_ENGINE_LIB_FILES "$<TARGET_FILE:glad>")

    add_custom_target(DistributeEditor
            COMMAND ${CMAKE_COMMAND}
            -DDIST_DIR=${PLU_EDITOR_DIST_DIR}
            -DSOURCE_DIR=${CMAKE_SOURCE_DIR}
            -DEDITOR_EXE=$<TARGET_FILE:PluEditor>
            # Every engine module, semicolon-separated. It was one $<TARGET_FILE:Engine> while the
            # engine was a single library; Engine is an INTERFACE target now and has no file.
            "-DENGINE_LIBS=$<JOIN:${PLU_ENGINE_LIB_FILES},;>"
            -DVCPKG_LIB_DIR=${CMAKE_BINARY_DIR}/vcpkg_installed/${VCPKG_TARGET_TRIPLET}/lib
            -DPYARMOR_EXE=${PLU_PYARMOR_EXE}
            -P "${CMAKE_CURRENT_SOURCE_DIR}/DistributeEditor.cmake"
            WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
            VERBATIM
            COMMENT "Packaging standalone editor into ${PLU_EDITOR_DIST_DIR}"
    )
    add_dependencies(DistributeEditor PluEditor)
endif()

#set(CPACK_PACKAGE_NAME "PluEditor")
#set(CPACK_PACKAGE_VERSION "1.0.0")
#set(CPACK_PACKAGE_VENDOR "Plutex")
#set(CPACK_GENERATOR "ZIP")
#set(CPACK_STRIP_FILES TRUE)          # usuwa debug symbols → mniejszy plik
#set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY FALSE)  # brak zbędnego folderu root w ZIP
#
#include(CPack)
