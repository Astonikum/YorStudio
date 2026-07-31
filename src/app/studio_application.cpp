#include "yorstudio/studio_application.hpp"

#include <exception>
#include <utility>

namespace yorstudio {

StudioApplication::StudioApplication(std::filesystem::path recentProjectsPath)
    : recentProjectsPath_(std::move(recentProjectsPath)) {
    if (recentProjectsPath_.empty()) return;
    try {
        recentProjects_ = RecentProjects::read(recentProjectsPath_);
    } catch (const std::exception& error) {
        status_ = std::string("Recent projects unavailable: ") + error.what();
    }
}

StudioApplication::~StudioApplication() {
    closeProject();
}

void StudioApplication::openProject(const std::filesystem::path& projectRoot) {
    try {
        const auto root = std::filesystem::absolute(projectRoot).lexically_normal();
        WorkspaceRoots roots({root.parent_path()});
        auto session = roots.openProject(root, ProjectAccess::readWrite);
        const ProjectManifest manifest = session.manifest();
        project_ = std::move(session);
        if (recordRecent(manifest, root)) status_ = "Project opened.";
    } catch (const std::exception& error) {
        status_ = error.what();
    }
}

void StudioApplication::closeProject() noexcept {
    if (project_) project_->close();
    project_.reset();
    if (running_) status_ = "Choose a YOR project to open.";
}

void StudioApplication::handle(const StudioUiAction& action, const std::filesystem::path& selectedProject) {
    switch (action.command) {
    case StudioUiCommand::chooseProject:
        if (!selectedProject.empty()) openProject(selectedProject);
        break;
    case StudioUiCommand::openRecentProject:
        if (!action.projectRoot.empty()) openProject(action.projectRoot);
        break;
    case StudioUiCommand::newProject:
        if (!selectedProject.empty() && !action.projectName.empty()) createProject(selectedProject, action.projectName);
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
    result.recentProjects.reserve(recentProjects_.entries().size());
    for (const auto& recent : recentProjects_.entries()) {
        result.recentProjects.push_back({recent.name, recent.root.string()});
    }
    return result;
}

bool StudioApplication::recordRecent(const ProjectManifest& manifest, const std::filesystem::path& projectRoot) {
    try {
        recentProjects_.record(projectRoot, manifest);
        if (!recentProjectsPath_.empty()) {
            const auto parent = recentProjectsPath_.parent_path();
            if (!parent.empty()) std::filesystem::create_directories(parent);
            recentProjects_.writeAtomic(recentProjectsPath_);
        }
    } catch (const std::exception& error) {
        status_ = std::string("Project opened; recent registry unavailable: ") + error.what();
        return false;
    }
    return true;
}

void StudioApplication::createProject(const std::filesystem::path& parentRoot, std::string name) {
    try {
        const auto parent = std::filesystem::absolute(parentRoot).lexically_normal();
        const auto root = parent / name;
        WorkspaceRoots roots({parent});
        const ProjectManifest manifest = ProjectManifest::create(std::move(name));
        newProject(roots, root, manifest);
        auto session = roots.openProject(root, ProjectAccess::readWrite);
        const ProjectManifest actual = session.manifest();
        project_ = std::move(session);
        if (recordRecent(actual, root)) status_ = "Project created and opened.";
    } catch (const std::exception& error) {
        status_ = error.what();
    }
}

} // namespace yorstudio
