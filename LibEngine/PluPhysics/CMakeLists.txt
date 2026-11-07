# L6 — Jolt and the per-scene physics world. Sits above gameplay: it builds bodies from gameplay
# components and reacts to scene events, and gameplay never names it.
plu_add_engine_module(PluPhysics DEPENDS PluCore PluAssetTypes PluGameplay)

target_link_libraries(PluPhysics PUBLIC glm Jolt::Jolt)
target_compile_definitions(PluPhysics PUBLIC JPH_DEBUG_RENDERER)
