# L3 — particle data and the particle spawner (simulated on the render thread).
plu_add_engine_module(PluEffects
        DEPENDS PluCore PluAssetTypes)

target_link_libraries(PluEffects PUBLIC glm json glad Jolt::Jolt)
target_compile_definitions(PluEffects PUBLIC JPH_DEBUG_RENDERER)
