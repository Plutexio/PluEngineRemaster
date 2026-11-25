# L5 — GameObject, components, scenes and the snapshot builder.
plu_add_engine_module(PluGameplay
        DEPENDS PluCore PluAssetCore PluPlatform PluAssetTypes PluEffects)

target_link_libraries(PluGameplay PUBLIC glm json)
