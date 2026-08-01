#include "imgui_ui_port.hpp"

#include "../platform/win32_window.hpp"
#include "yor_theme.hpp"

#include "backends/imgui_impl_dx11.h"
#include "backends/imgui_impl_win32.h"
#include "imgui.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <functional>
#include <string_view>
#include <utility>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace yorstudio {

namespace {

ImGuiContext* mainImGuiContext = nullptr;

LRESULT handleMainWindowMessage(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    if (mainImGuiContext == nullptr) return 0;

    ImGuiContext* previousContext = ImGui::GetCurrentContext();
    ImGui::SetCurrentContext(mainImGuiContext);
    const LRESULT handled = ImGui_ImplWin32_WndProcHandler(window, message, wParam, lParam);
    ImGui::SetCurrentContext(previousContext);
    return handled;
}

std::uint64_t entityKey(const StudioUiEntity& entity) noexcept {
    return (static_cast<std::uint64_t>(entity.index) << 32u) | entity.generation;
}

bool containsCaseInsensitive(std::string_view value, std::string_view query) {
    if (query.empty()) return true;
    return std::search(value.begin(), value.end(), query.begin(), query.end(), [](char left, char right) {
        return std::tolower(static_cast<unsigned char>(left)) == std::tolower(static_cast<unsigned char>(right));
    }) != value.end();
}

} // namespace

ImGuiUiPort::ImGuiUiPort(Win32Window& window)
    : window_(window), iconStore_(window.device(), ui::assetDirectory() / L"icons") {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    mainImGuiContext = ImGui::GetCurrentContext();

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = nullptr;
    ui::applyYorTheme();
    ImGui_ImplWin32_Init(window_.handle());
    ImGui_ImplDX11_Init(window_.device(), window_.context());
    window_.setMessageHandler(&handleMainWindowMessage);

    compositor_.initialize(
        window_,
        [this](YorImWindowId id) { drawWindow(id); },
        [this] { drawMainMenu(); });
}

ImGuiUiPort::~ImGuiUiPort() {
    window_.setMessageHandler(nullptr);
    compositor_.shutdown();
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    mainImGuiContext = nullptr;
    ImGui::DestroyContext();
}

void ImGuiUiPort::setNewProjectParent(std::filesystem::path parent) {
    if (!parent.empty()) newProjectParent_ = std::move(parent);
}

void ImGuiUiPort::beginFrame() {
    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
}

StudioUiAction ImGuiUiPort::draw(const StudioUiFrame& frame) {
    currentFrame_ = &frame;
    currentAction_ = {};
    compositor_.setEditorWindowsVisible(frame.projectOpen);
    compositor_.draw();

    if (newProjectPopupRequested_) {
        newProjectDialogOpen_ = true;
        newProjectPopupRequested_ = false;
        const ImVec2 displaySize = ImGui::GetIO().DisplaySize;
        ImGui::SetNextWindowPos({displaySize.x * 0.5f, displaySize.y * 0.5f}, ImGuiCond_Appearing,
                                {0.5f, 0.5f});
    }
    drawNewProjectPopup();

    currentFrame_ = nullptr;
    return currentAction_;
}

void ImGuiUiPort::endFrame() {
    compositor_.render();
}

void ImGuiUiPort::drawMainMenu() {
    if (currentFrame_ == nullptr) return;
    const auto& frame = *currentFrame_;
    if (ImGui::BeginMenu("File")) {
        if (ImGui::MenuItem("Open Project...")) currentAction_.command = StudioUiCommand::chooseProject;
        if (ImGui::MenuItem("New Project...")) newProjectPopupRequested_ = true;
        if (frame.editorOpen && ImGui::MenuItem("Save Scene")) currentAction_.command = StudioUiCommand::saveScene;
        if (frame.projectOpen && ImGui::MenuItem("Close Project")) currentAction_.command = StudioUiCommand::closeProject;
        ImGui::Separator();
        if (ImGui::MenuItem("Quit")) currentAction_.command = StudioUiCommand::quit;
        ImGui::EndMenu();
    }
}

void ImGuiUiPort::drawLauncher() {
    if (currentFrame_ == nullptr) return;
    const auto& frame = *currentFrame_;

    ImGui::TextUnformatted("Projects");
    const auto& style = ImGui::GetStyle();
    const float contentRight = ImGui::GetWindowContentRegionMax().x;
    const float actionWidth = 230.0f + style.ItemSpacing.x + 78.0f + style.ItemSpacing.x + 152.0f;
    ImGui::SameLine(std::max(ImGui::GetCursorPosX(), contentRight - actionWidth));
    ImGui::SetNextItemWidth(230.0f);
    ImGui::InputTextWithHint("##project-search", "Search", projectSearch_, sizeof(projectSearch_));
    ImGui::SameLine();
    if (ImGui::Button("Add", {78.0f, 0.0f})) ImGui::OpenPopup("ProjectActions");
    if (ImGui::BeginPopup("ProjectActions")) {
        if (ImGui::MenuItem("Open Project...")) currentAction_.command = StudioUiCommand::chooseProject;
        ImGui::EndPopup();
    }
    ImGui::SameLine();
    const auto& materialColors = ImGui::GetStyle().Colors;
    ImGui::PushStyleColor(ImGuiCol_Button, materialColors[ImGuiCol_HeaderActive]);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, materialColors[ImGuiCol_CheckMark]);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, materialColors[ImGuiCol_HeaderActive]);
    const bool newProjectButtonPressed = iconStore_.button("folder-plus", "New project", {18.0f, 18.0f});
    ImGui::PopStyleColor(3);
    if (newProjectButtonPressed) newProjectPopupRequested_ = true;
    ImGui::Separator();
    ImGui::Dummy({0.0f, 8.0f});

    const auto tableFlags = ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg |
                            ImGuiTableFlags_BordersInnerV;
    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(20.0f, 12.0f));
    if (ImGui::BeginTable("RecentProjects", 3, tableFlags)) {
        ImGui::TableSetupColumn("Pin", ImGuiTableColumnFlags_WidthFixed, 54.0f);
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Date", ImGuiTableColumnFlags_WidthFixed, 220.0f);
        ImGui::TableHeadersRow();
        const float rowHeight = ImGui::GetTextLineHeight() * 2.0f + style.CellPadding.y * 2.0f;
        for (const auto& recent : frame.recentProjects) {
            if (!containsCaseInsensitive(recent.name, projectSearch_) &&
                !containsCaseInsensitive(recent.root, projectSearch_)) {
                continue;
            }
            ImGui::PushID(recent.root.c_str());
            ImGui::TableNextRow(ImGuiTableRowFlags_None, rowHeight);
            ImGui::TableSetColumnIndex(0);
            if (iconStore_.button(recent.pinned ? "pin" : "pin-outline", "")) {
                currentAction_.command = StudioUiCommand::setRecentPinned;
                currentAction_.projectRoot = recent.root;
                currentAction_.active = !recent.pinned;
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip(recent.pinned ? "Unpin project" : "Pin project");

            ImGui::TableSetColumnIndex(1);
            const ImVec2 namePosition = ImGui::GetCursorScreenPos();
            if (ImGui::Selectable("##open-project", false,
                                  ImGuiSelectableFlags_AllowOverlap, ImVec2(0.0f, rowHeight))) {
                currentAction_.command = StudioUiCommand::openRecentProject;
                currentAction_.projectRoot = recent.root;
            }
            if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
                ImGui::SetDragDropPayload("YOR_RECENT_PROJECT", recent.root.c_str(), recent.root.size() + 1);
                ImGui::TextUnformatted(recent.name.c_str());
                ImGui::EndDragDropSource();
            }
            if (ImGui::BeginDragDropTarget()) {
                if (const auto* payload = ImGui::AcceptDragDropPayload("YOR_RECENT_PROJECT")) {
                    const auto* source = static_cast<const char*>(payload->Data);
                    if (source != nullptr && recent.root != source) {
                        currentAction_.command = StudioUiCommand::moveRecentProjectBefore;
                        currentAction_.projectRoot = source;
                        currentAction_.targetProjectRoot = recent.root;
                    }
                }
                ImGui::EndDragDropTarget();
            }
            if (ImGui::BeginPopupContextItem("##project-menu")) {
                if (ImGui::MenuItem("Open project")) {
                    currentAction_.command = StudioUiCommand::openRecentProject;
                    currentAction_.projectRoot = recent.root;
                }
                if (ImGui::MenuItem(recent.pinned ? "Unpin project" : "Pin project")) {
                    currentAction_.command = StudioUiCommand::setRecentPinned;
                    currentAction_.projectRoot = recent.root;
                    currentAction_.active = !recent.pinned;
                }
                if (ImGui::MenuItem("Remove from list")) {
                    currentAction_.command = StudioUiCommand::removeRecentProject;
                    currentAction_.projectRoot = recent.root;
                }
                ImGui::EndPopup();
            }
            auto* draw = ImGui::GetWindowDrawList();
            const auto textColor = ImGui::GetColorU32(ImGuiCol_Text);
            const auto mutedColor = ImGui::GetColorU32(ImGuiCol_TextDisabled);
            draw->AddText({namePosition.x + style.CellPadding.x, namePosition.y + style.CellPadding.y}, textColor,
                          recent.name.c_str());
            draw->AddText({namePosition.x + style.CellPadding.x,
                           namePosition.y + style.CellPadding.y + ImGui::GetTextLineHeight()},
                          mutedColor, recent.root.c_str());

            ImGui::TableSetColumnIndex(2);
            const ImVec2 datePosition = ImGui::GetCursorScreenPos();
            const std::string created = "Created  " + (recent.createdAt.empty() ? std::string("-") : recent.createdAt);
            const std::string modified = "Modified " + (recent.modifiedAt.empty() ? std::string("-") : recent.modifiedAt);
            draw->AddText({datePosition.x + style.CellPadding.x, datePosition.y + style.CellPadding.y}, mutedColor,
                          created.c_str());
            draw->AddText({datePosition.x + style.CellPadding.x,
                           datePosition.y + style.CellPadding.y + ImGui::GetTextLineHeight()},
                          mutedColor, modified.c_str());
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    ImGui::PopStyleVar();
    ImGui::Spacing();
    drawPopups();
}

void ImGuiUiPort::drawScene() {
    if (currentFrame_ == nullptr) return;
    const auto& frame = *currentFrame_;
    if (!frame.editorOpen) {
        ImGui::TextDisabled("Open a project to edit a scene.");
        return;
    }

    if (iconStore_.button("plus", "Create Object...")) ImGui::OpenPopup("Create Object");
    ImGui::SameLine();
    if (iconStore_.button("content-save", "Save Scene")) currentAction_.command = StudioUiCommand::saveScene;
    ImGui::SameLine();
    if (iconStore_.button("delete", "Delete")) currentAction_.command = StudioUiCommand::deleteObject;
    ImGui::SameLine();
    if (iconStore_.button("content-duplicate", "Duplicate")) currentAction_.command = StudioUiCommand::duplicateObject;
    ImGui::SameLine();
    if (iconStore_.button("vector-square", "Set Parent...")) ImGui::OpenPopup("Set Parent");
    ImGui::SameLine();
    if (iconStore_.button("undo", "Undo")) currentAction_.command = StudioUiCommand::undo;
    ImGui::SameLine();
    if (iconStore_.button("redo", "Redo")) currentAction_.command = StudioUiCommand::redo;
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
            currentAction_.command = StudioUiCommand::selectObject;
            currentAction_.entityIndex = entity.index;
            currentAction_.entityGeneration = entity.generation;
        }
        if (children) {
            if (open) {
                collapsedEntities_.erase(key);
                for (const auto& child : frame.sceneEntities) {
                    if (child.parentIndex == entity.index && child.parentGeneration == entity.generation) drawEntity(child);
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
            currentAction_.command = StudioUiCommand::clearParent;
            ImGui::CloseCurrentPopup();
        }
        ImGui::Separator();
        for (const auto& entity : frame.sceneEntities) {
            if (entity.selected) continue;
            ImGui::PushID(static_cast<int>(entity.index));
            if (ImGui::Selectable(entity.name.c_str())) {
                currentAction_.command = StudioUiCommand::setParent;
                currentAction_.parentIndex = entity.index;
                currentAction_.parentGeneration = entity.generation;
                ImGui::CloseCurrentPopup();
            }
            ImGui::PopID();
        }
        ImGui::EndPopup();
    }
}

void ImGuiUiPort::drawInspector() {
    if (currentFrame_ == nullptr) return;
    const auto& frame = *currentFrame_;
    if (!frame.editorOpen) {
        ImGui::TextDisabled("Open a project to inspect objects.");
        return;
    }

    const StudioUiEntity* inspected = nullptr;
    for (const auto& entity : frame.sceneEntities) {
        if (entity.selected) {
            inspected = &entity;
            break;
        }
    }
    if (!inspected) {
        ImGui::TextDisabled("Select an object to inspect it.");
        return;
    }
    if (inspected->index != inspectedIndex_ || inspected->generation != inspectedGeneration_) {
        inspectedIndex_ = inspected->index;
        inspectedGeneration_ = inspected->generation;
        std::strncpy(renameName_, inspected->name.c_str(), sizeof(renameName_) - 1);
        renameName_[sizeof(renameName_) - 1] = '\0';
        editedLayer_ = static_cast<int>(inspected->layer);
        tagName_[0] = '\0';
        editedTransform_ = inspected->transform;
    }
    ImGui::InputText("Name", renameName_, sizeof(renameName_));
    if (ImGui::Button("Apply Name")) {
        currentAction_.command = StudioUiCommand::renameObject;
        currentAction_.objectName = renameName_;
    }
    bool active = inspected->active;
    if (ImGui::Checkbox("Active", &active)) {
        currentAction_.command = StudioUiCommand::setActive;
        currentAction_.active = active;
    }
    ImGui::InputInt("Layer", &editedLayer_);
    if (ImGui::Button("Apply Layer")) {
        currentAction_.command = StudioUiCommand::setLayer;
        currentAction_.layer = editedLayer_ < 0 ? 32u : static_cast<unsigned int>(editedLayer_);
    }
    ImGui::InputText("Tag", tagName_, sizeof(tagName_));
    if (ImGui::Button("Add Tag")) {
        currentAction_.command = StudioUiCommand::addTag;
        currentAction_.tag = tagName_;
    }
    for (const auto& tag : inspected->tags) {
        ImGui::PushID(tag.c_str());
        ImGui::TextUnformatted(tag.c_str());
        ImGui::SameLine();
        if (ImGui::SmallButton("Remove")) {
            currentAction_.command = StudioUiCommand::removeTag;
            currentAction_.tag = tag;
        }
        ImGui::PopID();
    }
    ImGui::Separator();
    ImGui::DragFloat3("Position", editedTransform_.position, 0.1f);
    ImGui::DragFloat4("Rotation", editedTransform_.rotation, 0.01f);
    ImGui::DragFloat3("Scale", editedTransform_.scale, 0.1f);
    if (ImGui::Button("Apply Transform")) {
        currentAction_.command = StudioUiCommand::setTransform;
        currentAction_.transform = editedTransform_;
    }
}

void ImGuiUiPort::drawPopups() {
    if (ImGui::BeginPopupModal("Create Object", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::InputText("Object name", newObjectName_, sizeof(newObjectName_));
        if (ImGui::Button("Create")) {
            currentAction_.command = StudioUiCommand::createObject;
            currentAction_.objectName = newObjectName_;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

}

void ImGuiUiPort::drawNewProjectPopup() {
    if (!newProjectDialogOpen_) return;
    bool open = true;
    if (ImGui::Begin("New Project", &open, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse)) {
        ImGui::TextUnformatted("Project settings");
        ImGui::Separator();
        ImGui::InputText("Project name", newProjectName_, sizeof(newProjectName_));
        ImGui::InputText("Engine version", newProjectEngineVersion_, sizeof(newProjectEngineVersion_));
        ImGui::InputText("Startup scene", newProjectStartupScene_, sizeof(newProjectStartupScene_));
        ImGui::TextWrapped("Location: %s", newProjectParent_.string().c_str());
        ImGui::TextWrapped("Project path: %s", (newProjectParent_ / newProjectName_).string().c_str());
        if (iconStore_.button("folder-open", "Choose folder...")) currentAction_.command = StudioUiCommand::chooseProjectParent;

        ImGui::Separator();
        ImGui::TextUnformatted("Git");
        ImGui::Checkbox("Initialize Git repository", &newProjectInitializeGit_);
        ImGui::Checkbox("Create .gitignore", &newProjectWriteGitIgnore_);
        ImGui::Checkbox("Create .gitattributes", &newProjectWriteGitAttributes_);
        if (ImGui::Checkbox("Initialize Git LFS", &newProjectInitializeGitLfs_) && newProjectInitializeGitLfs_) {
            newProjectInitializeGit_ = true;
            newProjectWriteGitAttributes_ = true;
        }

        if (ImGui::Button("Create project")) {
            currentAction_.command = StudioUiCommand::newProject;
            currentAction_.projectSettings.parentDirectory = newProjectParent_;
            currentAction_.projectSettings.name = newProjectName_;
            currentAction_.projectSettings.engineVersion = newProjectEngineVersion_;
            currentAction_.projectSettings.startupScene = newProjectStartupScene_;
            currentAction_.projectSettings.initializeGit = newProjectInitializeGit_;
            currentAction_.projectSettings.writeGitIgnore = newProjectWriteGitIgnore_;
            currentAction_.projectSettings.writeGitAttributes = newProjectWriteGitAttributes_;
            currentAction_.projectSettings.initializeGitLfs = newProjectInitializeGitLfs_;
            newProjectDialogOpen_ = false;
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) newProjectDialogOpen_ = false;
        ImGui::End();
    }
    if (!open) newProjectDialogOpen_ = false;
}

void ImGuiUiPort::drawWindow(YorImWindowId id) {
    switch (id) {
    case YorImWindowId::launcher: drawLauncher(); break;
    case YorImWindowId::scene: drawScene(); break;
    case YorImWindowId::inspector: drawInspector(); break;
    }
}

} // namespace yorstudio
