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
    StudioUiCommand draw(const StudioUiFrame& frame) override;
    void endFrame() override;

private:
    Win32Window& window_;
};

} // namespace yorstudio
