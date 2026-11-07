# L5 — GameObject, components, scenes and the snapshot builder.
plu_add_engine_module(PluGameplay
        DEPENDS PluCore PluAssetCore PluPlatform PluAssetTypes PluRender PluEffects)

target_link_libraries(PluGameplay PUBLIC glm json glad Jolt::Jolt)
target_compile_definitions(PluGameplay PUBLIC JPH_DEBUG_RENDERER)
