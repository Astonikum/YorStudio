#pragma once

#include "yorstudio/project_lifecycle.hpp"
#include "yorstudio/ui/studio_ui_port.hpp"

#include <filesystem>
#include <optional>

namespace yorstudio {

class StudioApplication {
public:
    explicit StudioApplication(std::filesystem::path recentProjectsPath = {});
    ~StudioApplication();

    StudioApplication(const StudioApplication&) = delete;
    StudioApplication& operator=(const StudioApplication&) = delete;

    void openProject(const std::filesystem::path& projectRoot);
    void closeProject() noexcept;
    void handle(const StudioUiAction& action, const std::filesystem::path& selectedProject = {});

    StudioUiFrame frame() const;
    bool running() const noexcept { return running_; }
    void requestQuit() noexcept { running_ = false; }

private:
    bool running_ = true;
    std::string status_ = "Choose a YOR project to open.";
    std::filesystem::path recentProjectsPath_;
    RecentProjects recentProjects_;
    std::optional<ProjectSession> project_;

    bool recordRecent(const ProjectManifest& manifest, const std::filesystem::path& projectRoot);
    void createProject(const std::filesystem::path& parentRoot, std::string name);
};

} // namespace yorstudio
