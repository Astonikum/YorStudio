#pragma once

#include "yorstudio/ui/studio_ui_port.hpp"

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

private:
    Win32Window& window_;
    char newProjectName_[128] = "NewYORProject";
};

} // namespace yorstudio
