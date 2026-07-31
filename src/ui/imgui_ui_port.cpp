#include "imgui_ui_port.hpp"

#include "../platform/win32_window.hpp"

#include "backends/imgui_impl_dx11.h"
#include "backends/imgui_impl_win32.h"
#include "imgui.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace yorstudio {

ImGuiUiPort::ImGuiUiPort(Win32Window& window) : window_(window) {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.IniFilename = nullptr;
    ImGui::StyleColorsDark();
    ImGui_ImplWin32_Init(window_.handle());
    ImGui_ImplDX11_Init(window_.device(), window_.context());
    window_.setMessageHandler(&ImGui_ImplWin32_WndProcHandler);
}

ImGuiUiPort::~ImGuiUiPort() {
    window_.setMessageHandler(nullptr);
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
}

void ImGuiUiPort::beginFrame() {
    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
}

StudioUiCommand ImGuiUiPort::draw(const StudioUiFrame& frame) {
    StudioUiCommand command = StudioUiCommand::none;
    ImGui::DockSpaceOverViewport();

    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("Open Project...")) command = StudioUiCommand::chooseProject;
            if (frame.projectOpen && ImGui::MenuItem("Close Project")) command = StudioUiCommand::closeProject;
            ImGui::Separator();
            if (ImGui::MenuItem("Quit")) command = StudioUiCommand::quit;
            ImGui::EndMenu();
        }
        ImGui::EndMainMenuBar();
    }

    ImGui::Begin("YorStudio");
    ImGui::TextUnformatted("YOR project launcher");
    ImGui::Separator();
    if (frame.projectOpen) {
        ImGui::Text("Project: %s", frame.projectName.c_str());
        ImGui::TextWrapped("Root: %s", frame.projectRoot.c_str());
        if (frame.readOnly) ImGui::TextUnformatted("Access: read-only");
        if (ImGui::Button("Close Project")) command = StudioUiCommand::closeProject;
    } else {
        ImGui::TextUnformatted("No project is open.");
        if (ImGui::Button("Open Project...")) command = StudioUiCommand::chooseProject;
    }
    ImGui::Spacing();
    ImGui::TextWrapped("Status: %s", frame.status.c_str());
    ImGui::End();
    return command;
}

void ImGuiUiPort::endFrame() {
    ImGui::Render();
    if (window_.beginRender()) {
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        window_.present();
    }
}

} // namespace yorstudio
