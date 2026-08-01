#pragma once

#include "yorstudio/ui/studio_ui_port.hpp"
#include "yor_icons.hpp"
#include "yor_imwindow_compositor.hpp"

#include <cstdint>
#include <filesystem>
#include <unordered_set>

namespace yorstudio {

class Win32Window;

class ImGuiUiPort final : public StudioUiPort {
public:
    explicit ImGuiUiPort(Win32Window& window);
    ~ImGuiUiPort() override;

    ImGuiUiPort(const ImGuiUiPort&) = delete;
    ImGuiUiPort& operator=(const ImGuiUiPort&) = delete;

    void beginFrame() override;
    StudioUiAction draw(const StudioUiFrame& frame) override;
    void endFrame() override;

    void setNewProjectParent(std::filesystem::path parent);

private:
    void drawMainMenu();
    void drawLauncher();
    void drawScene();
    void drawInspector();
    void drawPopups();
    void drawNewProjectPopup();
    void drawWindow(YorImWindowId);

    Win32Window& window_;
    icons::YorIconStore iconStore_;
    YorImWindowCompositor compositor_;
    const StudioUiFrame* currentFrame_ = nullptr;
    StudioUiAction currentAction_;
    bool newProjectPopupRequested_ = false;
    bool newProjectDialogOpen_ = false;
    char projectSearch_[128] = {};
    char newProjectName_[128] = "NewYORProject";
    char newProjectEngineVersion_[64] = "v0.1.0";
    char newProjectStartupScene_[128] = "scenes/main.yorscene";
    bool newProjectInitializeGit_ = true;
    bool newProjectWriteGitIgnore_ = true;
    bool newProjectWriteGitAttributes_ = true;
    bool newProjectInitializeGitLfs_ = false;
    std::filesystem::path newProjectParent_ = std::filesystem::current_path();
    char newObjectName_[128] = "Object";
    char renameName_[128] = {};
    char tagName_[128] = {};
    StudioUiTransform editedTransform_;
    int editedLayer_ = 0;
    std::unordered_set<std::uint64_t> collapsedEntities_;
    unsigned int inspectedIndex_ = 0;
    unsigned int inspectedGeneration_ = 0;
};

} // namespace yorstudio
