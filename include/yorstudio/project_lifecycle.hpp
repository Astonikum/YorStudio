#pragma once

#include "yorstudio/project_workspace.hpp"

#include <filesystem>
#include <string>

namespace yorstudio {

class ProjectOperationError : public ProjectError {
public:
    using ProjectError::ProjectError;
};

struct SafeModeState {
    bool enabled = false;
    std::string reason;
};

ProjectPaths newProject(
    const WorkspaceRoots& roots,
    const std::filesystem::path& projectRoot,
    const ProjectManifest& manifest);

ProjectSession openProject(
    const WorkspaceRoots& roots,
    const std::filesystem::path& projectRoot,
    ProjectAccess access = ProjectAccess::readWrite);

ProjectPaths importProject(
    const WorkspaceRoots& roots,
    const std::filesystem::path& sourceRoot,
    const std::filesystem::path& destinationRoot,
    std::string newName = {});

ProjectPaths duplicateProject(
    const WorkspaceRoots& roots,
    const std::filesystem::path& sourceRoot,
    const std::filesystem::path& destinationRoot,
    std::string newName = {});

ProjectManifest migrateProject(const std::filesystem::path& projectRoot);

std::filesystem::path revealProject(
    const WorkspaceRoots& roots,
    const std::filesystem::path& projectRoot);

void removeRecentProject(RecentProjects& recent, const std::filesystem::path& projectRoot);

SafeModeState readSafeMode(const std::filesystem::path& projectRoot);
void setSafeMode(const std::filesystem::path& projectRoot, bool enabled, std::string reason = {});
void resetDisposableState(const std::filesystem::path& projectRoot);
void recoverProject(const std::filesystem::path& projectRoot);

} // namespace yorstudio
