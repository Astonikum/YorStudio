#pragma once

#include <nlohmann/json.hpp>

#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace yorstudio {

class ProjectError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

struct ProjectPaths {
    std::filesystem::path root;

    std::filesystem::path manifestPath() const;
    std::filesystem::path hiddenStatePath() const;
    std::filesystem::path lockPath() const;
    std::filesystem::path safeModePath() const;
};

class ProjectManifest {
public:
    static constexpr int CurrentSchemaVersion = 1;
    static constexpr std::string_view CanonicalEngineRepository =
        "https://github.com/Astonikum/YorEngine.git";

    static ProjectManifest create(
        std::string name,
        std::string engineVersion = "v0.1.0",
        std::string projectGuid = {},
        std::string startupScene = "scenes/main.yorscene",
        std::vector<std::string> targetPlatforms = {"windows-x64"},
        std::vector<std::string> modules = {},
        std::vector<std::string> contentRoots = {"assets", "scenes", "shaders"},
        std::string uiAdapter = "imgui");

    static ProjectManifest fromJson(std::string_view text);
    static ProjectManifest read(const std::filesystem::path& path);

    ProjectManifest rename(std::string newName) const;
    ProjectManifest reidentify(std::string newName = {}) const;

    void writeAtomic(const std::filesystem::path& path) const;
    std::string toJson() const;

    int schemaVersion() const noexcept { return schemaVersion_; }
    const std::string& projectGuid() const noexcept { return projectGuid_; }
    const std::string& name() const noexcept { return name_; }
    const std::string& engineRepository() const noexcept { return engineRepository_; }
    const std::string& engineVersion() const noexcept { return engineVersion_; }
    const std::string& cxxStandard() const noexcept { return cxxStandard_; }
    const std::string& startupScene() const noexcept { return startupScene_; }
    const std::vector<std::string>& targetPlatforms() const noexcept { return targetPlatforms_; }
    const std::vector<std::string>& modules() const noexcept { return modules_; }
    const std::vector<std::string>& contentRoots() const noexcept { return contentRoots_; }
    const std::string& uiAdapter() const noexcept { return uiAdapter_; }

private:
    ProjectManifest(
        int schemaVersion,
        std::string projectGuid,
        std::string name,
        std::string engineRepository,
        std::string engineVersion,
        std::string cxxStandard,
        std::string startupScene,
        std::vector<std::string> targetPlatforms,
        std::vector<std::string> modules,
        std::vector<std::string> contentRoots,
        std::string uiAdapter,
        nlohmann::json extra,
        nlohmann::json engineExtra,
        nlohmann::json toolchainExtra,
        nlohmann::json editorExtra);

    static nlohmann::json migrate(nlohmann::json data);

    int schemaVersion_;
    std::string projectGuid_;
    std::string name_;
    std::string engineRepository_;
    std::string engineVersion_;
    std::string cxxStandard_;
    std::string startupScene_;
    std::vector<std::string> targetPlatforms_;
    std::vector<std::string> modules_;
    std::vector<std::string> contentRoots_;
    std::string uiAdapter_;
    nlohmann::json extra_;
    nlohmann::json engineExtra_;
    nlohmann::json toolchainExtra_;
    nlohmann::json editorExtra_;
};

ProjectPaths createProject(const std::filesystem::path& root, const ProjectManifest& manifest);
ProjectManifest validateProject(const std::filesystem::path& root, bool requireLayout = false);

} // namespace yorstudio
