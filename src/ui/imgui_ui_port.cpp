#include "imgui_ui_port.hpp"

#include "../platform/win32_window.hpp"

#include "backends/imgui_impl_dx11.h"
#include "backends/imgui_impl_win32.h"
#include "imgui.h"

#include <cstring>

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

StudioUiAction ImGuiUiPort::draw(const StudioUiFrame& frame) {
    StudioUiAction action;
    ImGui::DockSpaceOverViewport();

    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("Open Project...")) action.command = StudioUiCommand::chooseProject;
            if (ImGui::MenuItem("New Project...")) ImGui::OpenPopup("New Project");
            if (frame.editorOpen && ImGui::MenuItem("Save Scene")) action.command = StudioUiCommand::saveScene;
            if (frame.projectOpen && ImGui::MenuItem("Close Project")) action.command = StudioUiCommand::closeProject;
            ImGui::Separator();
            if (ImGui::MenuItem("Quit")) action.command = StudioUiCommand::quit;
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
        if (ImGui::Button("Close Project")) action.command = StudioUiCommand::closeProject;
    } else {
        ImGui::TextUnformatted("No project is open.");
        if (ImGui::Button("Open Project...")) action.command = StudioUiCommand::chooseProject;
        ImGui::SameLine();
        if (ImGui::Button("New Project...")) ImGui::OpenPopup("New Project");
    }
    if (!frame.recentProjects.empty()) {
        ImGui::Separator();
        ImGui::TextUnformatted("Recent projects");
        for (const auto& recent : frame.recentProjects) {
            ImGui::PushID(recent.root.c_str());
            if (ImGui::Selectable(recent.name.c_str())) {
                action.command = StudioUiCommand::openRecentProject;
                action.projectRoot = recent.root;
            }
            ImGui::TextDisabled("%s", recent.root.c_str());
            ImGui::PopID();
        }
    }
    ImGui::Spacing();
    ImGui::TextWrapped("Status: %s", frame.status.c_str());
    ImGui::End();

    if (frame.editorOpen) {
        ImGui::Begin("Scene");
        if (ImGui::Button("Create Object...")) ImGui::OpenPopup("Create Object");
        ImGui::SameLine();
        if (ImGui::Button("Save Scene")) action.command = StudioUiCommand::saveScene;
        ImGui::SameLine();
        if (ImGui::Button("Undo")) action.command = StudioUiCommand::undo;
        ImGui::SameLine();
        if (ImGui::Button("Redo")) action.command = StudioUiCommand::redo;
        if (frame.sceneDirty) ImGui::SameLine(), ImGui::TextDisabled("modified");
        ImGui::Separator();
        for (const auto& entity : frame.sceneEntities) {
            ImGui::PushID(static_cast<int>(entity.index));
            const bool selected = ImGui::Selectable(entity.name.c_str(), entity.selected);
            if (selected) {
                action.command = StudioUiCommand::selectObject;
                action.entityIndex = entity.index;
                action.entityGeneration = entity.generation;
            }
            ImGui::PopID();
        }
        if (frame.sceneEntities.empty()) ImGui::TextDisabled("The scene has no objects.");
        ImGui::End();

        ImGui::Begin("Inspector");
        const StudioUiEntity* inspected = nullptr;
        for (const auto& entity : frame.sceneEntities) {
            if (entity.selected) {
                inspected = &entity;
                break;
            }
        }
        if (!inspected) {
            ImGui::TextDisabled("Select an object to inspect it.");
        } else {
            if (inspected->index != inspectedIndex_ || inspected->generation != inspectedGeneration_) {
                inspectedIndex_ = inspected->index;
                inspectedGeneration_ = inspected->generation;
                std::strncpy(renameName_, inspected->name.c_str(), sizeof(renameName_) - 1);
                renameName_[sizeof(renameName_) - 1] = '\0';
                editedTransform_ = inspected->transform;
            }
            ImGui::InputText("Name", renameName_, sizeof(renameName_));
            if (ImGui::Button("Apply Name")) {
                action.command = StudioUiCommand::renameObject;
                action.objectName = renameName_;
            }
            ImGui::Separator();
            ImGui::DragFloat3("Position", editedTransform_.position, 0.1f);
            ImGui::DragFloat4("Rotation", editedTransform_.rotation, 0.01f);
            ImGui::DragFloat3("Scale", editedTransform_.scale, 0.1f);
            if (ImGui::Button("Apply Transform")) {
                action.command = StudioUiCommand::setTransform;
                action.transform = editedTransform_;
            }
        }
        ImGui::End();
    }

    if (ImGui::BeginPopupModal("Create Object", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::InputText("Object name", newObjectName_, sizeof(newObjectName_));
        if (ImGui::Button("Create")) {
            action.command = StudioUiCommand::createObject;
            action.objectName = newObjectName_;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    if (ImGui::BeginPopupModal("New Project", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::InputText("Project name", newProjectName_, sizeof(newProjectName_));
        if (ImGui::Button("Create")) {
            action.command = StudioUiCommand::newProject;
            action.projectName = newProjectName_;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    return action;
}

void ImGuiUiPort::endFrame() {
    ImGui::Render();
    if (window_.beginRender()) {
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        window_.present();
    }
}

} // namespace yorstudio
