#pragma once

#include "yorstudio/project_lock.hpp"
#include "yorstudio/project_manifest.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace yorstudio {

class WorkspaceError : public ProjectError {
public:
    using ProjectError::ProjectError;
};

enum class ProjectAccess {
    readWrite,
    readOnly,
};

class ProjectSession {
public:
    static ProjectSession open(
        const std::filesystem::path& projectRoot,
        ProjectAccess access = ProjectAccess::readWrite);

    ProjectSession(const ProjectSession&) = delete;
    ProjectSession& operator=(const ProjectSession&) = delete;
    ProjectSession(ProjectSession&&) noexcept = default;
    ProjectSession& operator=(ProjectSession&&) noexcept = default;

    const std::filesystem::path& root() const noexcept { return root_; }
    const ProjectManifest& manifest() const noexcept { return *manifest_; }
    ProjectAccess access() const noexcept { return access_; }
    bool isReadOnly() const noexcept { return access_ == ProjectAccess::readOnly; }
    const ProjectLockInfo* lockInfo() const noexcept;

    void saveManifest(const ProjectManifest& manifest);
    void close() noexcept;

private:
    ProjectSession(
        std::filesystem::path root,
        ProjectManifest manifest,
        ProjectAccess access,
        std::optional<ProjectLock> lock);

    std::filesystem::path root_;
    std::optional<ProjectManifest> manifest_;
    ProjectAccess access_ = ProjectAccess::readOnly;
    std::optional<ProjectLock> lock_;
};

struct DiscoveredProject {
    std::filesystem::path root;
    std::string projectGuid;
    std::string name;
};

struct DiscoveryIssue {
    std::filesystem::path root;
    std::string message;
};

class WorkspaceRoots {
public:
    static constexpr int CurrentSchemaVersion = 1;

    explicit WorkspaceRoots(std::vector<std::filesystem::path> roots = {});

    static WorkspaceRoots fromJson(std::string_view text);
    std::string toJson() const;

    void addRoot(const std::filesystem::path& root);
    void removeRoot(const std::filesystem::path& root);
    bool allows(const std::filesystem::path& projectRoot) const;
    ProjectSession openProject(
        const std::filesystem::path& projectRoot,
        ProjectAccess access = ProjectAccess::readWrite) const;

    const std::vector<std::filesystem::path>& roots() const noexcept { return roots_; }
    std::vector<DiscoveredProject> discover(std::vector<DiscoveryIssue>& issues) const;

private:
    std::vector<std::filesystem::path> roots_;
};

struct RecentProject {
    std::filesystem::path root;
    std::string projectGuid;
    std::string name;
    std::string lastOpenedAt;
};

class RecentProjects {
public:
    static constexpr int CurrentSchemaVersion = 1;
    static constexpr std::size_t MaximumEntries = 32;

    static RecentProjects read(const std::filesystem::path& path);

    void record(const std::filesystem::path& projectRoot, const ProjectManifest& manifest);
    void remove(const std::filesystem::path& projectRoot);
    void writeAtomic(const std::filesystem::path& path) const;

    const std::vector<RecentProject>& entries() const noexcept { return entries_; }

private:
    std::vector<RecentProject> entries_;
};

} // namespace yorstudio
