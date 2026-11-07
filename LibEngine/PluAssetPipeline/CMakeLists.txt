# L4 — importers and asset loaders. Reached through reflection, not by name, which is why the
# Engine interface target lists it explicitly.
plu_add_engine_module(PluAssetPipeline DEPENDS PluCore PluAssetCore PluAssetTypes PluEffects)

target_link_libraries(PluAssetPipeline PUBLIC glm stb_image assimp::assimp)
