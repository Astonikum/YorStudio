#include "yorstudio/studio_application.hpp"

#include "../platform/win32_window.hpp"
#include "../ui/imgui_ui_port.hpp"

#include <shellapi.h>

#include <filesystem>

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    int argumentCount = 0;
    LPWSTR* arguments = CommandLineToArgvW(GetCommandLineW(), &argumentCount);
    const std::filesystem::path initialProject =
        argumentCount > 1 ? std::filesystem::path(arguments[1]) : std::filesystem::path{};
    if (arguments) LocalFree(arguments);

    if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))) return 1;

    yorstudio::Win32Window window;
    if (!window.create(instance, L"YorStudio", 1280, 800)) {
        CoUninitialize();
        return 1;
    }

    yorstudio::StudioApplication application(window.recentProjectsPath());
    if (!initialProject.empty()) application.openProject(initialProject);
    yorstudio::ImGuiUiPort ui(window);

    while (window.pumpMessages() && application.running()) {
        ui.beginFrame();
        const auto action = ui.draw(application.frame());
        if (action.command == yorstudio::StudioUiCommand::chooseProject) {
            application.handle(action, window.browseForProject());
        } else if (action.command == yorstudio::StudioUiCommand::newProject) {
            application.handle(action, window.browseForDirectory(L"Select a parent folder for the new YOR project"));
        } else {
            application.handle(action);
        }
        ui.endFrame();
    }

    CoUninitialize();
    return 0;
}
