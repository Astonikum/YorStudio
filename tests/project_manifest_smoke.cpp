#include "yorstudio/project_manifest.hpp"
#include "yorstudio/project_lock.hpp"
#include "yorstudio/project_lifecycle.hpp"
#include "yorstudio/project_workspace.hpp"

#include <algorithm>
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
        recoverProject(paths.root);
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
        const auto flowCreated = newProject(workspaceRoots, workspacePath / "FlowCreated", ProjectManifest::create("Flow Created"));
        CHECK(validateProject(flowCreated.root, true).name() == "Flow Created");
        {
            auto opened = openProject(workspaceRoots, flowCreated.root, ProjectAccess::readOnly);
            CHECK(opened.isReadOnly());
            opened.close();
        }
        std::filesystem::create_directories(customPaths.root / "build");
        {
            std::ofstream generated(customPaths.root / "build" / "stale.bin", std::ios::binary);
            generated << "stale";
        }
        const auto imported = importProject(workspaceRoots, customPaths.root, workspacePath / "Imported");
        const auto duplicated = duplicateProject(workspaceRoots, gamePaths.root, workspacePath / "Duplicated");
        CHECK(validateProject(imported.root, true).name() == custom.name());
        CHECK(validateProject(duplicated.root, true).name() == "Contract Game Copy");
        CHECK(validateProject(imported.root, false).projectGuid() == custom.projectGuid());
        CHECK(validateProject(duplicated.root, false).projectGuid() != created.projectGuid());
        CHECK(!std::filesystem::exists(imported.root / "build"));
        CHECK(std::filesystem::equivalent(revealProject(workspaceRoots, imported.root), imported.root));
        rejected = false;
        try {
            importProject(workspaceRoots, customPaths.root, temporary.path / "outside" / "Imported");
        } catch (const ProjectOperationError&) {
            rejected = true;
        }
        CHECK(rejected);
        rejected = false;
        try {
            duplicateProject(workspaceRoots, gamePaths.root, gamePaths.root);
        } catch (const ProjectOperationError&) {
            rejected = true;
        }
        CHECK(rejected);
        CHECK(validateProject(gamePaths.root, false).name() == created.name());

        const auto legacyRoot = temporary.path / "Legacy";
        std::filesystem::create_directories(legacyRoot / ".yor");
        {
            std::ofstream legacyManifest(legacyRoot / "project.yorproject", std::ios::binary);
            legacyManifest << R"({
                "guid": "2d3d62d0-6c10-4a36-bf58-5e3e40bcf7e3",
                "display_name": "Migrated Game",
                "engine_version": "v0.1.0"
            })";
        }
        CHECK(migrateProject(legacyRoot).schemaVersion() == 1);
        CHECK(ProjectManifest::read(legacyRoot / "project.yorproject").name() == "Migrated Game");

        std::vector<DiscoveryIssue> issues;
        const auto discovered = workspaceRoots.discover(issues);
        CHECK(discovered.size() == 4);
        CHECK(std::any_of(discovered.begin(), discovered.end(), [&](const auto& project) {
            return std::filesystem::equivalent(project.root, gamePaths.root);
        }));
        CHECK(!issues.empty());
        const WorkspaceRoots roundTripRoots = WorkspaceRoots::fromJson(workspaceRoots.toJson());
        CHECK(roundTripRoots.roots().size() == 1);

        auto writeSession = workspaceRoots.openProject(gamePaths.root);
        CHECK(writeSession.access() == ProjectAccess::readWrite);
        CHECK(writeSession.lockInfo() != nullptr);
        rejected = false;
        try {
            const auto secondWriteSession = workspaceRoots.openProject(gamePaths.root);
        } catch (const ProjectLockError&) {
            rejected = true;
        }
        CHECK(rejected);
        rejected = false;
        try {
            setSafeMode(gamePaths.root, true, "locked");
        } catch (const ProjectOperationError&) {
            rejected = true;
        }
        CHECK(rejected);
        auto readSession = workspaceRoots.openProject(gamePaths.root, ProjectAccess::readOnly);
        CHECK(readSession.isReadOnly());
        CHECK(readSession.lockInfo() == nullptr);
        rejected = false;
        try {
            readSession.saveManifest(ProjectManifest::create("Read Only Edit"));
        } catch (const WorkspaceError&) {
            rejected = true;
        }
        CHECK(rejected);
        readSession.close();
        const ProjectManifest edited = ProjectManifest::create(
            "Edited Game", "v0.1.0", created.projectGuid());
        writeSession.saveManifest(edited);
        CHECK(writeSession.manifest().name() == "Edited Game");
        writeSession.close();

        CHECK(!readSafeMode(gamePaths.root).enabled);
        setSafeMode(gamePaths.root, true, "crash recovery");
        CHECK(readSafeMode(gamePaths.root).enabled);
        CHECK(readSafeMode(gamePaths.root).reason == "crash recovery");
        {
            std::ofstream malformedSafeMode(ProjectPaths{gamePaths.root}.safeModePath(), std::ios::binary | std::ios::trunc);
            malformedSafeMode << "not json";
        }
        rejected = false;
        try {
            readSafeMode(gamePaths.root);
        } catch (const ProjectOperationError&) {
            rejected = true;
        }
        CHECK(rejected);
        setSafeMode(gamePaths.root, false);
        CHECK(!readSafeMode(gamePaths.root).enabled);
        std::ofstream editorState(gamePaths.root / ".yor" / "editor" / "layout.json", std::ios::binary);
        editorState << "temporary";
        editorState.close();
        resetDisposableState(gamePaths.root);
        CHECK(!std::filesystem::exists(gamePaths.root / ".yor" / "editor"));
        CHECK(std::filesystem::is_regular_file(gamePaths.root / "scenes" / "main.yorscene"));

        RecentProjects recent;
        recent.record(gamePaths.root, created);
        recent.record(customPaths.root, custom);
        recent.record(gamePaths.root, created);
        CHECK(recent.entries().size() == 2);
        CHECK(std::filesystem::equivalent(recent.entries().front().root, gamePaths.root));
        const auto recentPath = temporary.path / "recent.yorprojects";
        recent.writeAtomic(recentPath);
        const RecentProjects loadedRecent = RecentProjects::read(recentPath);
        CHECK(loadedRecent.entries().size() == 2);
        CHECK(loadedRecent.entries().front().projectGuid == created.projectGuid());
        removeRecentProject(recent, customPaths.root);
        CHECK(recent.entries().size() == 1);

        const auto corruptRecentPath = temporary.path / "corrupt.yorprojects";
        {
            std::ofstream corruptRecent(corruptRecentPath, std::ios::binary);
            corruptRecent << "not json";
        }
        rejected = false;
        try {
            RecentProjects::read(corruptRecentPath);
        } catch (const WorkspaceError&) {
            rejected = true;
        }
        CHECK(rejected);

        const auto movedRoot = temporary.path / "MovedSource";
        const auto movedProject = createProject(movedRoot, ProjectManifest::create("Moved Project"));
        RecentProjects movedRecent;
        movedRecent.record(movedProject.root, ProjectManifest::read(movedProject.manifestPath()));
        const auto movedDestination = temporary.path / "MovedDestination";
        std::filesystem::rename(movedRoot, movedDestination);
        removeRecentProject(movedRecent, movedRoot);
        CHECK(movedRecent.entries().empty());

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
