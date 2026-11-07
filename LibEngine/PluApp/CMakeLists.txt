# L7 — Application, and the one place that links every module: the generated pybind11 bindings
# name types from all of them.
plu_add_engine_module(PluApp
        DEPENDS PluCore PluAssetCore PluPlatform PluAssetTypes PluEffects PluScripting
                PluRender PluAssetPipeline PluGameplay PluPhysics)

# The bindings chunks (PLU_BINDINGS_SOURCES, from ReflectionCache/BindingsLayout.cmake – see
# LibEngine/CMakeLists.txt) plus the PYBIND11_EMBEDDED_MODULE that calls them; the chunks compile in
# parallel.
target_sources(PluApp PRIVATE ${PLU_BINDINGS_MAIN_SOURCE} ${PLU_BINDINGS_SOURCES})
# Out of the reflection unity batches: the chunks are split precisely so they compile in parallel.
set_source_files_properties(${PLU_BINDINGS_MAIN_SOURCE} ${PLU_BINDINGS_SOURCES} PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON)
target_link_libraries(PluApp PUBLIC argparse::argparse)
