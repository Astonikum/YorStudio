#include "imgui_ui_port.hpp"

#include "../platform/win32_window.hpp"

#include "backends/imgui_impl_dx11.h"
#include "backends/imgui_impl_win32.h"
#include "imgui.h"

#include <algorithm>
#include <cstring>
#include <functional>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace yorstudio {

namespace {

std::uint64_t entityKey(const StudioUiEntity& entity) noexcept {
    return (static_cast<std::uint64_t>(entity.index) << 32u) | entity.generation;
}

void targetCombo(const char* label, const std::vector<StudioUiEntity>& entities,
                 std::optional<std::string>& targetGuid) {
    std::string preview = "None";
    if (targetGuid) {
        for (const auto& entity : entities) {
            if (entity.guid == *targetGuid) {
                preview = entity.name;
                break;
            }
        }
    }
    if (!ImGui::BeginCombo(label, preview.c_str())) return;
    if (ImGui::Selectable("None", !targetGuid)) targetGuid.reset();
    for (const auto& entity : entities) {
        const bool selected = targetGuid && *targetGuid == entity.guid;
        if (ImGui::Selectable(entity.name.c_str(), selected)) targetGuid = entity.guid;
        if (selected) ImGui::SetItemDefaultFocus();
    }
    ImGui::EndCombo();
}

void offsetSpaceCombo(const char* label, StudioUiCameraOffsetSpace& space) {
    const char* values[] = {"Target local", "World"};
    int selected = space == StudioUiCameraOffsetSpace::world ? 1 : 0;
    if (ImGui::Combo(label, &selected, values, 2)) {
        space = selected == 1 ? StudioUiCameraOffsetSpace::world : StudioUiCameraOffsetSpace::targetLocal;
    }
}

void lightKindCombo(StudioUiLightKind& kind) {
    const char* values[] = {"Directional", "Point", "Spot"};
    int selected = kind == StudioUiLightKind::point ? 1 : kind == StudioUiLightKind::spot ? 2 : 0;
    if (ImGui::Combo("Kind", &selected, values, 3)) {
        kind = selected == 1 ? StudioUiLightKind::point
            : selected == 2 ? StudioUiLightKind::spot : StudioUiLightKind::directional;
    }
}

} // namespace

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
        if (ImGui::Button("Delete")) action.command = StudioUiCommand::deleteObject;
        ImGui::SameLine();
        if (ImGui::Button("Duplicate")) action.command = StudioUiCommand::duplicateObject;
        ImGui::SameLine();
        if (ImGui::Button("Set Parent...")) ImGui::OpenPopup("Set Parent");
        ImGui::SameLine();
        if (ImGui::Button("Undo")) action.command = StudioUiCommand::undo;
        ImGui::SameLine();
        if (ImGui::Button("Redo")) action.command = StudioUiCommand::redo;
        if (frame.sceneDirty) ImGui::SameLine(), ImGui::TextDisabled("modified");
        ImGui::Separator();
        std::unordered_set<std::uint64_t> rendered;
        const auto hasChildren = [&](const StudioUiEntity& entity) {
            return std::any_of(frame.sceneEntities.begin(), frame.sceneEntities.end(), [&](const StudioUiEntity& candidate) {
                return candidate.parentIndex == entity.index && candidate.parentGeneration == entity.generation;
            });
        };
        std::function<void(const StudioUiEntity&)> drawEntity;
        drawEntity = [&](const StudioUiEntity& entity) {
            const auto key = entityKey(entity);
            if (!rendered.insert(key).second) return;
            const bool children = hasChildren(entity);
            ImGui::PushID(static_cast<int>(entity.index));
            ImGui::PushID(static_cast<int>(entity.generation));
            ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanAvailWidth;
            if (entity.selected) flags |= ImGuiTreeNodeFlags_Selected;
            if (!children) flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
            if (children) ImGui::SetNextItemOpen(!collapsedEntities_.contains(key), ImGuiCond_Always);
            const bool open = ImGui::TreeNodeEx(entity.name.c_str(), flags);
            if (ImGui::IsItemClicked()) {
                action.command = StudioUiCommand::selectObject;
                action.entityIndex = entity.index;
                action.entityGeneration = entity.generation;
            }
            if (children) {
                if (open) {
                    collapsedEntities_.erase(key);
                    for (const auto& child : frame.sceneEntities) {
                        if (child.parentIndex == entity.index && child.parentGeneration == entity.generation) {
                            drawEntity(child);
                        }
                    }
                    ImGui::TreePop();
                } else {
                    collapsedEntities_.insert(key);
                }
            }
            ImGui::PopID();
            ImGui::PopID();
        };
        for (const auto& entity : frame.sceneEntities) {
            if (entity.parentGeneration == 0) drawEntity(entity);
        }
        for (const auto& entity : frame.sceneEntities) drawEntity(entity);
        if (frame.sceneEntities.empty()) ImGui::TextDisabled("The scene has no objects.");
        if (ImGui::BeginPopup("Set Parent")) {
            if (ImGui::Selectable("No Parent")) {
                action.command = StudioUiCommand::clearParent;
                ImGui::CloseCurrentPopup();
            }
            ImGui::Separator();
            for (const auto& entity : frame.sceneEntities) {
                if (entity.selected) continue;
                ImGui::PushID(static_cast<int>(entity.index));
                if (ImGui::Selectable(entity.name.c_str())) {
                    action.command = StudioUiCommand::setParent;
                    action.parentIndex = entity.index;
                    action.parentGeneration = entity.generation;
                    ImGui::CloseCurrentPopup();
                }
                ImGui::PopID();
            }
            ImGui::EndPopup();
        }
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
                editedLayer_ = static_cast<int>(inspected->layer);
                tagName_[0] = '\0';
                editedTransform_ = inspected->transform;
                editedCamera_ = inspected->camera.value_or(StudioUiCamera{});
                editedCameraKeyPoint_ = inspected->cameraKeyPoint.value_or(StudioUiCameraKeyPoint{});
                editedCameraNoise_ = inspected->cameraNoise.value_or(StudioUiCameraNoise{});
                editedLight_ = inspected->light.value_or(StudioUiLight{});
            }
            ImGui::InputText("Name", renameName_, sizeof(renameName_));
            if (ImGui::Button("Apply Name")) {
                action.command = StudioUiCommand::renameObject;
                action.objectName = renameName_;
            }
            bool active = inspected->active;
            if (ImGui::Checkbox("Active", &active)) {
                action.command = StudioUiCommand::setActive;
                action.active = active;
            }
            ImGui::InputInt("Layer", &editedLayer_);
            if (ImGui::Button("Apply Layer")) {
                action.command = StudioUiCommand::setLayer;
                action.layer = editedLayer_ < 0 ? 32u : static_cast<unsigned int>(editedLayer_);
            }
            ImGui::InputText("Tag", tagName_, sizeof(tagName_));
            if (ImGui::Button("Add Tag")) {
                action.command = StudioUiCommand::addTag;
                action.tag = tagName_;
            }
            for (const auto& tag : inspected->tags) {
                ImGui::PushID(tag.c_str());
                ImGui::TextUnformatted(tag.c_str());
                ImGui::SameLine();
                if (ImGui::SmallButton("Remove")) {
                    action.command = StudioUiCommand::removeTag;
                    action.tag = tag;
                }
                ImGui::PopID();
            }
            ImGui::Separator();
            ImGui::DragFloat3("Position", editedTransform_.position, 0.1f);
            ImGui::DragFloat4("Rotation", editedTransform_.rotation, 0.01f);
            ImGui::DragFloat3("Scale", editedTransform_.scale, 0.1f);
            if (ImGui::Button("Apply Transform")) {
                action.command = StudioUiCommand::setTransform;
                action.transform = editedTransform_;
            }
            ImGui::Separator();
            ImGui::TextUnformatted("Camera");
            if (!inspected->camera) {
                if (ImGui::Button("Add Camera")) action.command = StudioUiCommand::addCamera;
            } else {
                ImGui::PushID("Camera");
                ImGui::DragFloat("FOV Y", &editedCamera_.fovYDegrees, 0.1f, 0.1f, 179.9f);
                ImGui::DragFloat("Aspect Ratio", &editedCamera_.aspectRatio, 0.01f, 0.01f, 100.0f);
                ImGui::DragFloat("Near Plane", &editedCamera_.nearPlane, 0.01f, 0.001f, 100000.0f);
                ImGui::DragFloat("Far Plane", &editedCamera_.farPlane, 0.1f, 0.002f, 100000.0f);
                ImGui::InputScalar("Channel Mask", ImGuiDataType_U32, &editedCamera_.channelMask);
                if (ImGui::Button("Apply Camera")) {
                    action.command = StudioUiCommand::setCamera;
                    action.camera = editedCamera_;
                }
                ImGui::SameLine();
                if (ImGui::Button("Remove Camera")) action.command = StudioUiCommand::removeCamera;
                ImGui::PopID();
            }

            ImGui::TextUnformatted("Camera Key Point");
            if (!inspected->cameraKeyPoint) {
                if (ImGui::Button("Add Camera Key Point")) action.command = StudioUiCommand::addCameraKeyPoint;
            } else {
                ImGui::PushID("CameraKeyPoint");
                ImGui::Checkbox("Enabled", &editedCameraKeyPoint_.enabled);
                ImGui::InputInt("Priority", &editedCameraKeyPoint_.priority);
                ImGui::InputScalar("Key Point Channel Mask", ImGuiDataType_U32, &editedCameraKeyPoint_.channelMask);
                ImGui::DragFloat("Blend Duration", &editedCameraKeyPoint_.blendDurationSeconds, 0.01f, 0.0f, 1000.0f);
                ImGui::DragFloat("Key Point FOV Y", &editedCameraKeyPoint_.lens.fovYDegrees, 0.1f, 0.1f, 179.9f);
                ImGui::DragFloat("Key Point Aspect Ratio", &editedCameraKeyPoint_.lens.aspectRatio, 0.01f, 0.01f, 100.0f);
                ImGui::DragFloat("Key Point Near Plane", &editedCameraKeyPoint_.lens.nearPlane, 0.01f, 0.001f, 100000.0f);
                ImGui::DragFloat("Key Point Far Plane", &editedCameraKeyPoint_.lens.farPlane, 0.1f, 0.002f, 100000.0f);
                targetCombo("Follow Target", frame.sceneEntities, editedCameraKeyPoint_.followTargetGuid);
                ImGui::DragFloat3("Follow Offset", editedCameraKeyPoint_.followOffset, 0.1f);
                offsetSpaceCombo("Follow Offset Space", editedCameraKeyPoint_.followOffsetSpace);
                targetCombo("Look At Target", frame.sceneEntities, editedCameraKeyPoint_.lookAtTargetGuid);
                ImGui::DragFloat3("Look At Offset", editedCameraKeyPoint_.lookAtOffset, 0.1f);
                offsetSpaceCombo("Look At Offset Space", editedCameraKeyPoint_.lookAtOffsetSpace);
                if (ImGui::Button("Apply Camera Key Point")) {
                    action.command = StudioUiCommand::setCameraKeyPoint;
                    action.cameraKeyPoint = editedCameraKeyPoint_;
                }
                ImGui::SameLine();
                if (ImGui::Button("Remove Camera Key Point")) action.command = StudioUiCommand::removeCameraKeyPoint;
                ImGui::PopID();
            }

            ImGui::TextUnformatted("Camera Noise");
            if (!inspected->cameraNoise) {
                if (ImGui::Button("Add Camera Noise")) action.command = StudioUiCommand::addCameraNoise;
            } else {
                ImGui::PushID("CameraNoise");
                ImGui::DragFloat3("Position Amplitude", editedCameraNoise_.positionAmplitude, 0.01f, 0.0f, 100000.0f);
                ImGui::DragFloat3("Rotation Amplitude", editedCameraNoise_.rotationAmplitudeDegrees, 0.1f, 0.0f, 360.0f);
                ImGui::DragFloat("Noise Frequency", &editedCameraNoise_.frequency, 0.01f, 0.001f, 1000.0f);
                ImGui::InputScalar("Noise Seed", ImGuiDataType_U32, &editedCameraNoise_.seed);
                if (ImGui::Button("Apply Camera Noise")) {
                    action.command = StudioUiCommand::setCameraNoise;
                    action.cameraNoise = editedCameraNoise_;
                }
                ImGui::SameLine();
                if (ImGui::Button("Remove Camera Noise")) action.command = StudioUiCommand::removeCameraNoise;
                ImGui::PopID();
            }

            ImGui::TextUnformatted("Light");
            if (!inspected->light) {
                if (ImGui::Button("Add Light")) action.command = StudioUiCommand::addLight;
            } else {
                ImGui::PushID("Light");
                lightKindCombo(editedLight_.kind);
                ImGui::ColorEdit3("Color", editedLight_.color);
                ImGui::DragFloat("Intensity", &editedLight_.intensity, 0.01f, 0.0f, 100000.0f);
                ImGui::DragFloat("Range", &editedLight_.range, 0.1f, 0.001f, 100000.0f);
                ImGui::DragFloat("Inner Cone", &editedLight_.innerConeDegrees, 0.1f, 0.0f, 180.0f);
                ImGui::DragFloat("Outer Cone", &editedLight_.outerConeDegrees, 0.1f, 0.0f, 180.0f);
                if (ImGui::Button("Apply Light")) {
                    action.command = StudioUiCommand::setLight;
                    action.light = editedLight_;
                }
                ImGui::SameLine();
                if (ImGui::Button("Remove Light")) action.command = StudioUiCommand::removeLight;
                ImGui::PopID();
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
