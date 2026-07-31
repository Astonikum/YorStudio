#include "yorstudio/project_manifest.hpp"
#include "yorstudio/project_lock.hpp"
#include "yorstudio/project_workspace.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>

#define CHECK(expression) \
    do { \
        if (!(expression)) throw std::runtime_error("check failed: " #expression); \
    } while (false)

namespace {

struct TemporaryDirectory {
    std::filesystem::path path = std::filesystem::temp_directory_path() / "yorstudio-project-contract";

    TemporaryDirectory() {
        std::error_code error;
        std::filesystem::remove_all(path, error);
    }

    ~TemporaryDirectory() {
        std::error_code error;
        std::filesystem::remove_all(path, error);
    }
};

} // namespace

int main() {
    try {
        using namespace yorstudio;

        const ProjectManifest created = ProjectManifest::create("Contract Game");
        const ProjectManifest roundTrip = ProjectManifest::fromJson(created.toJson());
        CHECK(roundTrip.name() == "Contract Game");
        CHECK(roundTrip.schemaVersion() == 1);

        nlohmann::json normalized = nlohmann::json::parse(created.toJson());
        normalized["startup_scene"] = R"(scenes\main.yorscene)";
        const ProjectManifest normalizedManifest = ProjectManifest::fromJson(normalized.dump());
        CHECK(normalizedManifest.startupScene() == "scenes/main.yorscene");

        nlohmann::json extension = nlohmann::json::parse(created.toJson());
        extension["x_plugin"] = {{"enabled", true}};
        extension["engine"]["x_note"] = "pinned";
        const ProjectManifest preserved = ProjectManifest::fromJson(extension.dump());
        const auto preservedJson = nlohmann::json::parse(preserved.toJson());
        CHECK(preservedJson["x_plugin"]["enabled"] == true);
        CHECK(preservedJson["engine"]["x_note"] == "pinned");

        const ProjectManifest migrated = ProjectManifest::fromJson(R"({
            "guid": "2d3d62d0-6c10-4a36-bf58-5e3e40bcf7e3",
            "display_name": "Legacy Game",
            "engine_version": "v0.1.0"
        })");
        CHECK(migrated.schemaVersion() == 1);
        CHECK(migrated.name() == "Legacy Game");

        bool rejected = false;
        nlohmann::json unsupportedRevision = nlohmann::json::parse(created.toJson());
        unsupportedRevision["engine"]["version"] = "main";
        rejected = false;
        try {
            ProjectManifest::fromJson(unsupportedRevision.dump());
        } catch (const ProjectError&) {
            rejected = true;
        }
        CHECK(rejected);

        rejected = false;
        try {
            ProjectManifest::fromJson(R"({
                "schema_version": 1,
                "project_guid": "2d3d62d0-6c10-4a36-bf58-5e3e40bcf7e3",
                "name": "Bad",
                "engine": {"repository": "https://github.com/Astonikum/YorEngine.git", "version": "v0.1.0"},
                "toolchain": {"cxx_standard": "c++20"},
                "startup_scene": "../main.yorscene",
                "target_platforms": ["windows-x64"],
                "modules": [],
                "content_roots": ["scenes"],
                "editor": {"ui_adapter": "imgui"}
            })");
        } catch (const ProjectError&) {
            rejected = true;
        }
        CHECK(rejected);

        TemporaryDirectory temporary;
        const auto paths = createProject(temporary.path, created);
        CHECK(validateProject(paths.root, true).projectGuid() == created.projectGuid());
        CHECK(std::filesystem::is_directory(paths.root / "code" / "src"));
        CHECK(std::filesystem::is_directory(paths.hiddenStatePath() / "cache"));
        CHECK(std::filesystem::is_regular_file(paths.root / "scenes" / "main.yorscene"));

        auto lock = ProjectLock::acquire(paths.root);
        CHECK(lock.ownsLock());
        CHECK(ProjectLock::inspect(paths.root).projectGuid == created.projectGuid());
        rejected = false;
        try {
            const auto secondLock = ProjectLock::acquire(paths.root);
        } catch (const ProjectLockError&) {
            rejected = true;
        }
        CHECK(rejected);
        lock.release();
        CHECK(!std::filesystem::exists(paths.lockPath()));

        nlohmann::json staleLock = {
            {"schema_version", 1},
            {"project_guid", created.projectGuid()},
            {"owner_id", "stale-owner"},
            {"host", ProjectLock::localHostName()},
            {"process_id", std::numeric_limits<std::uint64_t>::max()},
            {"acquired_at", "2026-01-01T00:00:00Z"},
            {"studio_version", "v0.1.0"},
        };
        {
            std::ofstream staleFile(paths.lockPath(), std::ios::binary);
            staleFile << staleLock.dump(2) << '\n';
        }
        ProjectLock::recoverStale(paths.root);
        CHECK(!std::filesystem::exists(paths.lockPath()));

        {
            std::ofstream malformed(paths.lockPath(), std::ios::binary);
            malformed << "not json";
        }
        rejected = false;
        try {
            ProjectLock::recoverStale(paths.root);
        } catch (const ProjectLockError&) {
            rejected = true;
        }
        CHECK(rejected);
        CHECK(std::filesystem::exists(paths.lockPath()));
        std::filesystem::remove(paths.lockPath());

        const ProjectManifest custom = ProjectManifest::create(
            "Custom Roots", "v0.1.0", {}, "content/main.yorscene", {"linux-x64"}, {"gameplay"}, {"content"});
        const auto customPaths = createProject(temporary.path / "custom", custom);
        CHECK(std::filesystem::is_directory(customPaths.root / "content"));
        CHECK(validateProject(customPaths.root, true).startupScene() == "content/main.yorscene");

        const auto workspacePath = temporary.path / "workspace";
        std::filesystem::create_directories(workspacePath);
        const auto gamePaths = createProject(workspacePath / "Game", created);
        std::filesystem::create_directories(workspacePath / "Nested" / "Deep");
        const auto invalidPath = workspacePath / "Broken";
        std::filesystem::create_directories(invalidPath);
        {
            std::ofstream invalidManifest(invalidPath / "project.yorproject", std::ios::binary);
            invalidManifest << "{}\n";
        }

        WorkspaceRoots workspaceRoots({workspacePath});
        CHECK(workspaceRoots.allows(gamePaths.root));
        CHECK(!workspaceRoots.allows(temporary.path / "outside"));
        std::vector<DiscoveryIssue> issues;
        const auto discovered = workspaceRoots.discover(issues);
        CHECK(discovered.size() == 1);
        CHECK(discovered.front().root == gamePaths.root);
        CHECK(!issues.empty());
        const WorkspaceRoots roundTripRoots = WorkspaceRoots::fromJson(workspaceRoots.toJson());
        CHECK(roundTripRoots.roots().size() == 1);

        RecentProjects recent;
        recent.record(gamePaths.root, created);
        recent.record(customPaths.root, custom);
        recent.record(gamePaths.root, created);
        CHECK(recent.entries().size() == 2);
        CHECK(recent.entries().front().root == gamePaths.root);
        const auto recentPath = temporary.path / "recent.yorprojects";
        recent.writeAtomic(recentPath);
        const RecentProjects loadedRecent = RecentProjects::read(recentPath);
        CHECK(loadedRecent.entries().size() == 2);
        CHECK(loadedRecent.entries().front().projectGuid == created.projectGuid());
        recent.remove(customPaths.root);
        CHECK(recent.entries().size() == 1);

        const ProjectManifest replacement = ProjectManifest::create("Replacement");
        replacement.writeAtomic(paths.manifestPath());
        CHECK(ProjectManifest::read(paths.manifestPath()).name() == "Replacement");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}

#undef CHECK
