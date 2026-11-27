# L2 — the asset data types themselves, plus the node graph and animation data.
plu_add_engine_module(PluAssetTypes DEPENDS PluCore PluAssetCore)

target_link_libraries(PluAssetTypes PUBLIC glm)

# Image decoding (ImageInfo) stays inside the module.
target_link_libraries(PluAssetTypes PRIVATE ImageMagick::Magick++ ImageMagick::MagickCore)
