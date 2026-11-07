//
// Created by Plutex on 31.12.2025.
//

#include "PluEngine/Core.h"

#ifdef PLU_PLATFORM_WINDOWS

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <ShellScalingApi.h>

#include "../RuntimeApp.h"
#include "PluEngine/Application.h"
#include "PluBase/Log.h"

// Hybrid-GPU laptops (NVIDIA Optimus, AMD PowerXpress) run every process on the integrated GPU
// unless the executable asks otherwise. Old iGPUs stop below OpenGL 4.5, so without this the engine
// gets no GL context there. The drivers only read these from the .exe, not from a DLL.
extern "C"
{
    __declspec(dllexport) DWORD NvOptimusEnablement = 0x00000001;
    __declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
}



int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    Plu::Application* application = new Plu::RuntimeApp();
    application->Run();
    delete application;
    return 67;
}

int main(int argc, char** argv)
{
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    Plu::Application* application = new Plu::RuntimeApp();
    application->Run();
    delete application;
    return 67;
}

#endif