//
// Created by Plutex on 31.12.2025.
//

#include "PluEngine/Core.h"

#ifdef PLU_PLATFORM_WINDOWS

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <ShellScalingApi.h>

#include "../EditorApp.h"
#include "PluEngine/Application.h"
#include "PluBase/Log.h"




int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    Plu::Application* application = new Plu::PluEditor();
    application->InjectArguments(nullptr);
    application->Run();
    delete application;
    return 0;
}

int main(int argc, char** argv)
{
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    argparse::ArgumentParser program("PluEngine");
    program.add_argument("--project", "-p").help("Path to project (.pluproject file or its directory) to launch at startup");
    Plu::Application::AddEngineArguments(program);
    try {
        program.parse_args(argc, argv);
    } catch (const std::exception& err) {
        std::cerr << err.what() << std::endl;
        std::cerr << program;
        return 1;
    }
    Plu::Application* application = new Plu::PluEditor();
    application->InjectArguments(&program);
    application->Run();
    delete application;
    return 0;
}

#endif