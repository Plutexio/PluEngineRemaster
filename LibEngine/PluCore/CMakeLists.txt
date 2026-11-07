# L0 — the bottom of the stack. Depends on no other engine module, and must stay that way.
plu_add_engine_module(PluCore)

target_link_libraries(PluCore PUBLIC
        PluBase::PluBase      # types, logging, asserts, UUIDs, thread affinity
        glm
        json
        spdlog
        pybind11
        imgui::imgui          # ReflectionBase.h declares editor control hooks in ImGui terms
        Python3::Python       # pybind11::type lives in TypeInfo
)
