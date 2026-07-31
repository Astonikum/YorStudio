#pragma once

#include "yorstudio/project_workspace.hpp"
#include "yorstudio/ui/studio_ui_port.hpp"

#include <filesystem>
#include <optional>

namespace yorstudio {

class StudioApplication {
public:
    StudioApplication();
    ~StudioApplication();

    StudioApplication(const StudioApplication&) = delete;
    StudioApplication& operator=(const StudioApplication&) = delete;

    void openProject(const std::filesystem::path& projectRoot);
    void closeProject() noexcept;
    void handle(StudioUiCommand command, const std::filesystem::path& selectedProject = {});

    StudioUiFrame frame() const;
    bool running() const noexcept { return running_; }
    void requestQuit() noexcept { running_ = false; }

private:
    bool running_ = true;
    std::string status_ = "Choose a YOR project to open.";
    std::optional<ProjectSession> project_;
};

} // namespace yorstudio
