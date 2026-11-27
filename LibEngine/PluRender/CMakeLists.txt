# L4 — everything that touches the GPU, and the render thread's resources.
plu_add_engine_module(PluRender DEPENDS PluCore PluAssetCore PluPlatform PluAssetTypes PluGameplay PluEffects)

target_link_libraries(PluRender PUBLIC glm imgui::imgui ImGuizmo stb::stb)

# Vulkan + VMA are PUBLIC: render headers expose vk:: / Vma* types to the modules above.
target_link_libraries(PluRender PUBLIC Vulkan::Vulkan GPUOpen::VulkanMemoryAllocator)
# PUBLIC as well: vulkan.hpp must be configured the same way in every translation unit that
# includes it, or the dispatcher and the structs differ between libraries.
target_compile_definitions(PluRender PUBLIC
        VULKAN_HPP_DISPATCH_LOADER_DYNAMIC=1
        VULKAN_HPP_NO_STRUCT_CONSTRUCTORS=1
)

# Shader compilation stays inside the module.
target_link_libraries(PluRender PRIVATE slang::slang)
