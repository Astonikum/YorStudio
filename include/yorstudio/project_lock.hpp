#pragma once

#include "yorstudio/project_manifest.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

namespace yorstudio {

class ProjectLockError : public ProjectError {
public:
    using ProjectError::ProjectError;
};

struct ProjectLockInfo {
    static constexpr int CurrentSchemaVersion = 1;

    int schemaVersion = CurrentSchemaVersion;
    std::string projectGuid;
    std::string ownerId;
    std::string host;
    std::uint64_t processId = 0;
    std::string acquiredAt;
    std::string studioVersion;

    static ProjectLockInfo fromJson(std::string_view text);
    std::string toJson() const;
    bool isStale() const;
};

class ProjectLock {
public:
    static ProjectLock acquire(
        const std::filesystem::path& projectRoot,
        std::string studioVersion = "v0.1.0");
    static ProjectLockInfo inspect(const std::filesystem::path& projectRoot);
    static void recoverStale(const std::filesystem::path& projectRoot);

    static std::string localHostName();
    static std::uint64_t currentProcessId();

    ProjectLock(const ProjectLock&) = delete;
    ProjectLock& operator=(const ProjectLock&) = delete;
    ProjectLock(ProjectLock&& other) noexcept;
    ProjectLock& operator=(ProjectLock&& other) noexcept;
    ~ProjectLock();

    const ProjectLockInfo& info() const noexcept { return info_; }
    const std::filesystem::path& path() const noexcept { return path_; }
    bool ownsLock() const noexcept { return ownsLock_; }

    void release();

private:
    ProjectLock(std::filesystem::path path, ProjectLockInfo info);
    void releaseNoexcept() noexcept;

    std::filesystem::path path_;
    ProjectLockInfo info_;
    bool ownsLock_ = false;
};

} // namespace yorstudio
