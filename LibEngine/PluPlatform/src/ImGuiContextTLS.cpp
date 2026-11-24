//
// Created by Plutex on 11/24/26.
//

#include "imgui.h"

// The "current ImGui context" global, made thread-local by our imgui overlay port
// (vcpkg-overlays/imgui/portfile.cmake defines GImGui to this symbol), so a thread that renders
// ImGui can keep a current context of its own next to Main's. Defined in PluPlatform, the lowest
// module that links imgui. On Windows imgui is a DLL and keeps this storage itself
// (PLU_IMGUI_TLS_IN_DLL).
#ifndef PLU_IMGUI_TLS_IN_DLL
thread_local ImGuiContext* PluImGuiTLS = nullptr;
#endif
