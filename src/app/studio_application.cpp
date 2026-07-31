#include "yorstudio/studio_application.hpp"

#include <exception>
#include <utility>

namespace yorstudio {

StudioApplication::StudioApplication() = default;

StudioApplication::~StudioApplication() {
    closeProject();
}

void StudioApplication::openProject(const std::filesystem::path& projectRoot) {
    try {
        const auto root = std::filesystem::absolute(projectRoot).lexically_normal();
        WorkspaceRoots roots({root.parent_path()});
        auto session = roots.openProject(root, ProjectAccess::readWrite);
        project_ = std::move(session);
        status_ = "Project opened.";
    } catch (const std::exception& error) {
        status_ = error.what();
    }
}

void StudioApplication::closeProject() noexcept {
    if (project_) project_->close();
    project_.reset();
    if (running_) status_ = "Choose a YOR project to open.";
}

void StudioApplication::handle(StudioUiCommand command, const std::filesystem::path& selectedProject) {
    switch (command) {
    case StudioUiCommand::chooseProject:
        if (!selectedProject.empty()) openProject(selectedProject);
        break;
    case StudioUiCommand::closeProject:
        closeProject();
        break;
    case StudioUiCommand::quit:
        closeProject();
        requestQuit();
        break;
    case StudioUiCommand::none:
        break;
    }
}

StudioUiFrame StudioApplication::frame() const {
    StudioUiFrame result;
    result.status = status_;
    if (project_) {
        result.projectOpen = true;
        result.projectName = project_->manifest().name();
        result.projectRoot = project_->root().string();
        result.readOnly = project_->isReadOnly();
    }
    return result;
}

} // namespace yorstudio
