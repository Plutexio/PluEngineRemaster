# L4 — everything that touches GL, and the render thread's resources.
plu_add_engine_module(PluRender DEPENDS PluCore PluAssetCore PluPlatform PluAssetTypes PluEffects)

target_link_libraries(PluRender PUBLIC glm glad imgui::imgui ImGuizmo stb_image)
target_link_libraries(PluRender PRIVATE OpenGL::GL)
