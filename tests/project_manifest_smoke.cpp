#include "yorstudio/project_manifest.hpp"
#include "yorstudio/project_lock.hpp"
#include "yorstudio/project_lifecycle.hpp"
#include "yorstudio/project_workspace.hpp"
#include "yorstudio/studio_application.hpp"
#include "yorstudio/ui/viewport_math.hpp"

#include <algorithm>
#include <cmath>
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

class FakeUiPort final : public yorstudio::StudioUiPort {
public:
    void beginFrame() override { ++beginCount; }

    yorstudio::StudioUiAction draw(const yorstudio::StudioUiFrame& frame) override {
        lastFrame = frame;
        return nextAction;
    }

    void endFrame() override { ++endCount; }

    int beginCount = 0;
    int endCount = 0;
    yorstudio::StudioUiAction nextAction;
    yorstudio::StudioUiFrame lastFrame;
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

        StudioApplication application;
        CHECK(!application.frame().projectOpen);
        application.openProject(paths.root);
        CHECK(application.frame().projectOpen);
        CHECK(application.frame().projectName == "Replacement");
        application.openProject(temporary.path / "missing-project");
        CHECK(application.frame().projectOpen);
        CHECK(application.frame().projectName == "Replacement");
        CHECK(application.frame().editorOpen);
        CHECK(application.frame().viewport.sourceVersion > 0);
        CHECK(application.frame().viewport.camera.farPlane == 512.0f);
        const auto centerRay = viewportRayFromScreen(application.frame().viewport.camera, 49.5f, 49.5f, 100.0f, 100.0f);
        CHECK(std::abs(centerRay.direction[0]) < 0.001f);
        CHECK(std::abs(centerRay.direction[1]) < 0.001f);
        CHECK(centerRay.direction[2] > 0.99f);
        float centerHitDistance = 0.0f;
        CHECK(viewportIntersectTriangle(
            centerRay, {-1.0f, -1.0f, 0.0f}, {1.0f, -1.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, centerHitDistance));
        CHECK(std::abs(centerHitDistance - 5.0f) < 0.001f);
        StudioUiAction createObject;
        createObject.command = StudioUiCommand::createObject;
        createObject.objectName = "Hero";
        application.handle(createObject);
        CHECK(application.frame().sceneEntities.size() == 1);
        CHECK(application.frame().sceneEntities.front().name == "Hero");
        CHECK(application.frame().sceneEntities.front().selected);
        StudioUiAction renameObject;
        renameObject.command = StudioUiCommand::renameObject;
        renameObject.objectName = "Player";
        application.handle(renameObject);
        CHECK(application.frame().sceneEntities.front().name == "Player");
        application.handle({StudioUiCommand::undo});
        CHECK(application.frame().sceneEntities.front().name == "Hero");
        application.handle({StudioUiCommand::redo});
        CHECK(application.frame().sceneEntities.front().name == "Player");
        StudioUiAction setTransform;
        setTransform.command = StudioUiCommand::setTransform;
        setTransform.transform.position[0] = 4.0f;
        setTransform.transform.scale[1] = 2.0f;
        application.handle(setTransform);
        CHECK(application.frame().sceneEntities.front().transform.position[0] == 4.0f);
        CHECK(application.frame().sceneEntities.front().transform.scale[1] == 2.0f);
        application.handle({StudioUiCommand::undo});
        CHECK(application.frame().sceneEntities.front().transform.position[0] == 0.0f);
        const auto appEntity = application.frame().sceneEntities.front();
        StudioUiAction selectAppEntity;
        selectAppEntity.command = StudioUiCommand::selectObject;
        selectAppEntity.entityIndex = appEntity.index;
        selectAppEntity.entityGeneration = appEntity.generation;
        application.handle(selectAppEntity);
        application.handle({StudioUiCommand::addCamera});
        CHECK(application.frame().sceneEntities.front().camera.has_value());
        StudioUiAction setAppCamera;
        setAppCamera.command = StudioUiCommand::setCamera;
        setAppCamera.camera = StudioUiCamera{};
        setAppCamera.camera->fovYDegrees = 55.0f;
        application.handle(setAppCamera);
        CHECK(application.frame().sceneEntities.front().camera->fovYDegrees == 55.0f);
        application.handle({StudioUiCommand::undo});
        CHECK(application.frame().sceneEntities.front().camera->fovYDegrees == 70.0f);
        application.handle({StudioUiCommand::redo});
        CHECK(application.frame().sceneEntities.front().camera->fovYDegrees == 55.0f);
        application.handle({StudioUiCommand::addLight});
        CHECK(application.frame().sceneEntities.front().light.has_value());
        StudioUiAction setAppLight;
        setAppLight.command = StudioUiCommand::setLight;
        setAppLight.light = StudioUiLight{};
        setAppLight.light->kind = StudioUiLightKind::point;
        setAppLight.light->intensity = 2.0f;
        application.handle(setAppLight);
        CHECK(application.frame().sceneEntities.front().light->kind == StudioUiLightKind::point);
        CHECK(application.frame().sceneEntities.front().light->intensity == 2.0f);
        application.handle({StudioUiCommand::undo});
        CHECK(application.frame().sceneEntities.front().light->kind == StudioUiLightKind::directional);
        application.handle({StudioUiCommand::redo});
        CHECK(application.frame().sceneEntities.front().light->kind == StudioUiLightKind::point);
        application.handle({StudioUiCommand::addTriangle});
        CHECK(application.frame().sceneEntities.front().mesh->vertexCount == 3);
        CHECK(application.frame().viewport.entities.size() == 1);
        CHECK(application.frame().viewport.entities.front().vertexCount == 3);
        application.handle({StudioUiCommand::undo});
        CHECK(!application.frame().sceneEntities.front().mesh.has_value());
        application.handle({StudioUiCommand::redo});
        CHECK(application.frame().sceneEntities.front().mesh->vertexCount == 3);
        application.handle({StudioUiCommand::saveScene});
        CHECK(!application.frame().sceneDirty);
        nlohmann::json savedScene;
        {
            std::ifstream sceneFile(paths.root / "scenes" / "main.yorscene", std::ios::binary);
            sceneFile >> savedScene;
        }
        CHECK(savedScene["schema_version"] == 1);
        CHECK(savedScene["objects"].size() == 1);
        CHECK(savedScene["objects"][0]["name"] == "Player");
        savedScene["x_editor_extension"] = { {"preserve", true} };
        savedScene["objects"][0]["x_object_extension"] = "preserve";
        {
            std::ofstream sceneFile(paths.root / "scenes" / "main.yorscene", std::ios::binary | std::ios::trunc);
            sceneFile << savedScene.dump(2) << '\n';
        }
        application.handle({StudioUiCommand::closeProject});
        application.openProject(paths.root);
        CHECK(application.frame().sceneEntities.size() == 1);
        CHECK(application.frame().sceneEntities.front().name == "Player");
        application.handle({StudioUiCommand::saveScene});
        {
            std::ifstream sceneFile(paths.root / "scenes" / "main.yorscene", std::ios::binary);
            sceneFile >> savedScene;
        }
        CHECK(savedScene["x_editor_extension"]["preserve"] == true);
        CHECK(savedScene["objects"][0]["x_object_extension"] == "preserve");

        EditorDocument document;
        CHECK(document.createObject("Stable Object"));
        const auto originalEntity = document.entities().front().id;
        CHECK(document.undo());
        CHECK(!document.scene().isAlive(originalEntity));
        CHECK(document.redo());
        const auto recreatedEntity = document.entities().front().id;
        CHECK(recreatedEntity != originalEntity);
        CHECK(document.selection().active() == recreatedEntity);
        CHECK(!document.select(originalEntity));

        const auto parentScenePath = temporary.path / "parent.yorscene";
        nlohmann::json parentScene = { {"schema_version", 1}, {"objects", nlohmann::json::array()} };
        parentScene["objects"].push_back({
            {"guid", "11111111-1111-4111-8111-111111111111"},
            {"name", "Parent"},
        });
        parentScene["objects"].push_back({
            {"guid", "22222222-2222-4222-8222-222222222222"},
            {"name", "Child"},
            {"parent_guid", "11111111-1111-4111-8111-111111111111"},
        });
        {
            std::ofstream sceneFile(parentScenePath, std::ios::binary);
            sceneFile << parentScene.dump(2) << '\n';
        }
        EditorDocument parentDocument;
        parentDocument.load(parentScenePath);
        const auto parentEntities = parentDocument.entities();
        CHECK(parentEntities.size() == 2);
        CHECK(parentDocument.scene().parent(parentEntities[1].id) == parentEntities[0].id);
        parentDocument.save();
        {
            std::ifstream sceneFile(parentScenePath, std::ios::binary);
            sceneFile >> parentScene;
        }
        CHECK(parentScene["objects"][1]["parent_guid"] == "11111111-1111-4111-8111-111111111111");

        const auto cameraScenePath = temporary.path / "camera.yorscene";
        const std::string brainGuid = "33333333-3333-4333-8333-333333333333";
        const std::string shotGuid = "44444444-4444-4444-8444-444444444444";
        const std::string targetGuid = "55555555-5555-4555-8555-555555555555";
        const std::string lightGuid = "77777777-7777-4777-8777-777777777777";
        nlohmann::json cameraScene = { {"schema_version", 1}, {"objects", nlohmann::json::array()} };
        nlohmann::json brainObject = { {"guid", brainGuid}, {"name", "Brain"} };
        brainObject["components"]["camera"]["lens"] = {
            {"fov_y_degrees", 70.0f}, {"aspect_ratio", 16.0f / 9.0f}, {"near_plane", 0.1f}, {"far_plane", 1000.0f},
        };
        brainObject["components"]["camera"]["channel_mask"] = 2;

        nlohmann::json shotObject = { {"guid", shotGuid}, {"name", "Shot"} };
        auto& keyPointJson = shotObject["components"]["camera_key_point"];
        keyPointJson["lens"] = {
            {"fov_y_degrees", 80.0f}, {"aspect_ratio", 16.0f / 9.0f}, {"near_plane", 0.1f}, {"far_plane", 500.0f},
        };
        keyPointJson["priority"] = 10;
        keyPointJson["enabled"] = true;
        keyPointJson["channel_mask"] = 2;
        keyPointJson["blend_duration_seconds"] = 0.5f;
        keyPointJson["follow_target_guid"] = targetGuid;
        keyPointJson["follow_offset"] = {1.0f, 2.0f, 3.0f};
        keyPointJson["follow_offset_space"] = "world";
        keyPointJson["look_at_target_guid"] = targetGuid;
        keyPointJson["look_at_offset"] = {0.0f, 1.0f, 0.0f};
        keyPointJson["look_at_offset_space"] = "target_local";
        keyPointJson["x_unknown"] = true;
        auto& noiseJson = shotObject["components"]["camera_noise"];
        noiseJson["position_amplitude"] = {1.0f, 0.5f, 0.25f};
        noiseJson["rotation_amplitude_degrees"] = {2.0f, 1.0f, 0.0f};
        noiseJson["frequency"] = 2.0f;
        noiseJson["seed"] = 42;
        shotObject["components"]["custom_component"]["preserve"] = true;

        nlohmann::json targetObject = {
            {"guid", targetGuid}, {"name", "Target"},
            {"transform", {{"position", {10.0f, 2.0f, -3.0f}}}},
        };
        auto& meshVerticesJson = targetObject["components"]["mesh"]["vertices"];
        meshVerticesJson.push_back({
            {"position", {-1.0f, -1.0f, 0.0f}}, {"color", {1.0f, 0.0f, 0.0f, 1.0f}}, {"uv", {0.0f, 1.0f}},
        });
        meshVerticesJson.push_back({
            {"position", {1.0f, -1.0f, 0.0f}}, {"color", {0.0f, 1.0f, 0.0f, 1.0f}}, {"uv", {1.0f, 1.0f}},
        });
        meshVerticesJson.push_back({
            {"position", {0.0f, 1.0f, 0.0f}}, {"color", {0.0f, 0.0f, 1.0f, 1.0f}}, {"uv", {0.5f, 0.0f}},
        });
        nlohmann::json lightObject = {
            {"guid", lightGuid}, {"name", "Sun"},
            {"components", {{"light", {
                {"kind", "spot"}, {"color", {1.0f, 0.8f, 0.5f}}, {"intensity", 3.0f},
                {"range", 25.0f}, {"inner_cone_degrees", 10.0f}, {"outer_cone_degrees", 40.0f},
            }}}},
        };
        cameraScene["objects"].push_back(std::move(brainObject));
        cameraScene["objects"].push_back(std::move(shotObject));
        cameraScene["objects"].push_back(std::move(targetObject));
        cameraScene["objects"].push_back(std::move(lightObject));
        {
            std::ofstream sceneFile(cameraScenePath, std::ios::binary);
            sceneFile << cameraScene.dump(2) << '\n';
        }
        EditorDocument cameraDocument;
        cameraDocument.load(cameraScenePath);
        std::optional<yorengine::EntityId> brainEntity;
        std::optional<yorengine::EntityId> shotEntity;
        std::optional<yorengine::EntityId> targetEntity;
        std::optional<yorengine::EntityId> lightEntity;
        for (const auto& object : cameraDocument.scene().objects()) {
            if (object.name() == "Brain") brainEntity = object.id();
            if (object.name() == "Shot") shotEntity = object.id();
            if (object.name() == "Target") targetEntity = object.id();
            if (object.name() == "Sun") lightEntity = object.id();
        }
        CHECK(brainEntity && shotEntity && targetEntity && lightEntity);
        const auto* brainCamera = cameraDocument.scene().component<yorengine::Camera>(*brainEntity);
        const auto* shotKeyPoint = cameraDocument.scene().component<yorengine::CameraKeyPoint>(*shotEntity);
        const auto* shotNoise = cameraDocument.scene().component<yorengine::CameraNoise>(*shotEntity);
        CHECK(brainCamera && shotKeyPoint && shotNoise);
        CHECK(brainCamera->channelMask() == 2 && shotKeyPoint->priority() == 10);
        CHECK(shotKeyPoint->followTarget() == *targetEntity && shotKeyPoint->lookAtTarget() == *targetEntity);
        CHECK(shotKeyPoint->followOffsetSpace() == yorengine::CameraOffsetSpace::World);
        CHECK(shotKeyPoint->lookAtOffsetSpace() == yorengine::CameraOffsetSpace::TargetLocal);
        CHECK(shotNoise->seed() == 42);
        const auto* sun = cameraDocument.scene().component<yorengine::Light>(*lightEntity);
        CHECK(sun && sun->kind() == yorengine::Light::Kind::Spot && sun->intensity() == 3.0f);
        const auto* targetMesh = cameraDocument.scene().component<yorengine::Mesh>(*targetEntity);
        CHECK(targetMesh && targetMesh->vertices().size() == 3);
        for (const auto& state : cameraDocument.entities()) {
            if (state.id == *shotEntity) {
                CHECK(state.cameraKeyPoint && state.cameraNoise);
                CHECK(state.cameraKeyPoint->followTargetGuid == targetGuid);
                CHECK(state.cameraNoise->frequency == 2.0f);
            }
            if (state.id == *targetEntity) CHECK(state.mesh && state.mesh->vertices.size() == 3);
        }
        CHECK(cameraDocument.select(*shotEntity));
        CHECK(cameraDocument.duplicateSelected());
        const auto duplicateCamera = cameraDocument.selection().active();
        CHECK(duplicateCamera && cameraDocument.scene().component<yorengine::CameraKeyPoint>(*duplicateCamera));
        CHECK(cameraDocument.scene().component<yorengine::CameraKeyPoint>(*duplicateCamera)->followTarget() == *targetEntity);
        cameraDocument.save();
        {
            std::ifstream sceneFile(cameraScenePath, std::ios::binary);
            sceneFile >> cameraScene;
        }
        CHECK(cameraScene["objects"][1]["components"]["camera_key_point"]["x_unknown"] == true);
        CHECK(cameraScene["objects"][1]["components"]["custom_component"]["preserve"] == true);

        CHECK(cameraDocument.select(*shotEntity));
        std::optional<EditorCameraKeyPointState> editedKeyPoint;
        std::optional<EditorCameraNoiseState> editedNoise;
        for (const auto& state : cameraDocument.entities()) {
            if (state.id != *shotEntity) continue;
            editedKeyPoint = state.cameraKeyPoint;
            editedNoise = state.cameraNoise;
        }
        CHECK(editedKeyPoint && editedNoise);
        editedKeyPoint->priority = 20;
        editedKeyPoint->blendDurationSeconds = 0.75f;
        CHECK(cameraDocument.setSelectedCameraKeyPoint(*editedKeyPoint));
        CHECK(cameraDocument.scene().component<yorengine::CameraKeyPoint>(*shotEntity)->priority() == 20);
        CHECK(cameraDocument.undo());
        CHECK(cameraDocument.scene().component<yorengine::CameraKeyPoint>(*shotEntity)->priority() == 10);
        CHECK(cameraDocument.redo());
        CHECK(cameraDocument.scene().component<yorengine::CameraKeyPoint>(*shotEntity)->priority() == 20);
        const auto validKeyPointState = *editedKeyPoint;
        editedKeyPoint->lens.fovYDegrees = 180.0f;
        CHECK(!cameraDocument.setSelectedCameraKeyPoint(*editedKeyPoint));
        CHECK(cameraDocument.scene().component<yorengine::CameraKeyPoint>(*shotEntity)->priority() == 20);
        editedKeyPoint = validKeyPointState;
        editedNoise->frequency = 4.0f;
        CHECK(cameraDocument.setSelectedCameraNoise(*editedNoise));
        CHECK(cameraDocument.scene().component<yorengine::CameraNoise>(*shotEntity)->frequency() == 4.0f);
        CHECK(cameraDocument.undo());
        CHECK(cameraDocument.scene().component<yorengine::CameraNoise>(*shotEntity)->frequency() == 2.0f);
        CHECK(cameraDocument.redo());
        CHECK(cameraDocument.scene().component<yorengine::CameraNoise>(*shotEntity)->frequency() == 4.0f);

        CHECK(cameraDocument.select(*lightEntity));
        EditorLightState editedLight;
        for (const auto& state : cameraDocument.entities()) {
            if (state.id == *lightEntity) {
                CHECK(state.light.has_value());
                editedLight = *state.light;
            }
        }
        editedLight.kind = yorengine::Light::Kind::Point;
        editedLight.intensity = 5.0f;
        CHECK(cameraDocument.setSelectedLight(editedLight));
        CHECK(cameraDocument.scene().component<yorengine::Light>(*lightEntity)->kind() == yorengine::Light::Kind::Point);
        CHECK(cameraDocument.scene().component<yorengine::Light>(*lightEntity)->intensity() == 5.0f);
        CHECK(cameraDocument.undo());
        CHECK(cameraDocument.scene().component<yorengine::Light>(*lightEntity)->kind() == yorengine::Light::Kind::Spot);
        CHECK(cameraDocument.redo());
        EditorLightState invalidLight = editedLight;
        invalidLight.intensity = -1.0f;
        CHECK(!cameraDocument.setSelectedLight(invalidLight));
        CHECK(cameraDocument.scene().component<yorengine::Light>(*lightEntity)->intensity() == 5.0f);
        CHECK(cameraDocument.removeSelectedLight());
        CHECK(!cameraDocument.scene().component<yorengine::Light>(*lightEntity));
        CHECK(cameraDocument.undo());
        CHECK(cameraDocument.scene().component<yorengine::Light>(*lightEntity));
        cameraDocument.redo();
        CHECK(!cameraDocument.scene().component<yorengine::Light>(*lightEntity));

        CHECK(cameraDocument.select(*brainEntity));
        CHECK(cameraDocument.addSelectedTriangle());
        CHECK(cameraDocument.scene().component<yorengine::Mesh>(*brainEntity)->vertices().size() == 3);
        CHECK(cameraDocument.undo());
        CHECK(!cameraDocument.scene().component<yorengine::Mesh>(*brainEntity));
        CHECK(cameraDocument.redo());
        CHECK(cameraDocument.removeSelectedMesh());
        CHECK(!cameraDocument.scene().component<yorengine::Mesh>(*brainEntity));
        CHECK(cameraDocument.undo());
        CHECK(cameraDocument.scene().component<yorengine::Mesh>(*brainEntity)->vertices().size() == 3);

        CHECK(cameraDocument.select(*targetEntity));
        CHECK(cameraDocument.addSelectedCamera());
        CHECK(cameraDocument.scene().component<yorengine::Camera>(*targetEntity));
        EditorCameraState invalidCamera;
        invalidCamera.channelMask = 0;
        CHECK(!cameraDocument.setSelectedCamera(invalidCamera));
        CHECK(cameraDocument.scene().component<yorengine::Camera>(*targetEntity)->channelMask() == 1);
        CHECK(cameraDocument.removeSelectedCamera());
        CHECK(!cameraDocument.scene().component<yorengine::Camera>(*targetEntity));
        CHECK(cameraDocument.undo());
        CHECK(cameraDocument.scene().component<yorengine::Camera>(*targetEntity));
        cameraDocument.redo();
        CHECK(!cameraDocument.scene().component<yorengine::Camera>(*targetEntity));
        cameraDocument.save();
        {
            std::ifstream sceneFile(cameraScenePath, std::ios::binary);
            sceneFile >> cameraScene;
        }
        for (const auto& object : cameraScene["objects"]) {
            if (object["guid"] == brainGuid) CHECK(object["components"]["mesh"]["vertices"].size() == 3);
        }
        for (const auto& object : cameraScene["objects"]) {
            if (object["guid"] == targetGuid) {
                CHECK(object.contains("components"));
                CHECK(object["components"].contains("mesh"));
                CHECK(!object["components"].contains("camera"));
            }
        }

        auto invalidCameraScene = cameraScene;
        invalidCameraScene["objects"][1]["components"]["camera_key_point"]["follow_target_guid"] =
            "66666666-6666-4666-8666-666666666666";
        const auto invalidCameraPath = temporary.path / "invalid-camera.yorscene";
        {
            std::ofstream sceneFile(invalidCameraPath, std::ios::binary);
            sceneFile << invalidCameraScene.dump(2) << '\n';
        }
        EditorDocument invalidCameraDocument;
        bool invalidCameraRejected = false;
        try {
            invalidCameraDocument.load(invalidCameraPath);
        } catch (const EditorDocumentError&) {
            invalidCameraRejected = true;
        }
        CHECK(invalidCameraRejected);
        CHECK(invalidCameraDocument.scene().entities().empty());

        EditorDocument hierarchy;
        CHECK(hierarchy.createObject("Root"));
        const auto rootEntity = hierarchy.selection().active();
        CHECK(rootEntity.has_value());
        CHECK(hierarchy.createObject("Child"));
        const auto childEntity = hierarchy.selection().active();
        CHECK(childEntity.has_value());
        CHECK(hierarchy.setSelectedParent(rootEntity));
        CHECK(hierarchy.scene().parent(*childEntity) == *rootEntity);
        CHECK(hierarchy.select(*rootEntity));
        CHECK(!hierarchy.setSelectedParent(*childEntity));
        CHECK(!hierarchy.scene().parent(*rootEntity).valid());
        CHECK(hierarchy.select(*childEntity));
        CHECK(hierarchy.setSelectedActive(false));
        CHECK(!hierarchy.scene().active(*childEntity));
        CHECK(hierarchy.undo());
        CHECK(hierarchy.scene().active(*childEntity));
        CHECK(hierarchy.redo());
        CHECK(!hierarchy.scene().active(*childEntity));
        CHECK(hierarchy.select(*rootEntity));
        CHECK(hierarchy.duplicateSelected());
        const auto duplicateRoot = hierarchy.selection().active();
        CHECK(duplicateRoot.has_value());
        CHECK(hierarchy.scene().object(*duplicateRoot).name() == "Root Copy");
        CHECK(hierarchy.scene().children(*duplicateRoot).size() == 1);
        CHECK(hierarchy.undo());
        CHECK(hierarchy.scene().entities().size() == 2);
        CHECK(hierarchy.redo());
        CHECK(hierarchy.scene().entities().size() == 4);
        CHECK(hierarchy.select(*rootEntity));
        CHECK(hierarchy.deleteSelected());
        CHECK(hierarchy.scene().entities().size() == 2);
        CHECK(hierarchy.undo());
        CHECK(hierarchy.scene().entities().size() == 4);
        CHECK(hierarchy.selection().active().has_value());
        CHECK(hierarchy.scene().children(*hierarchy.selection().active()).size() == 1);
        FakeUiPort fakeUi;
        fakeUi.beginFrame();
        CHECK(fakeUi.draw(application.frame()).command == StudioUiCommand::none);
        fakeUi.endFrame();
        CHECK(fakeUi.beginCount == 1);
        CHECK(fakeUi.endCount == 1);
        CHECK(fakeUi.lastFrame.projectName == "Replacement");
        application.handle({StudioUiCommand::closeProject});
        CHECK(!application.frame().projectOpen);
        application.handle({StudioUiCommand::quit});
        CHECK(!application.running());

        const auto recentRegistry = temporary.path / "launcher-recent.yorprojects";
        StudioApplication launcher(recentRegistry);
        launcher.handle({StudioUiCommand::newProject, {}, "Created Game"}, temporary.path);
        CHECK(launcher.frame().projectOpen);
        CHECK(launcher.frame().projectName == "Created Game");
        CHECK(std::filesystem::is_regular_file(temporary.path / "Created Game" / "project.yorproject"));
        CHECK(launcher.frame().recentProjects.size() == 1);
        const std::string createdRoot = launcher.frame().recentProjects.front().root;
        launcher.handle({StudioUiCommand::closeProject});

        StudioApplication reopened(recentRegistry);
        CHECK(!reopened.frame().projectOpen);
        CHECK(reopened.frame().recentProjects.size() == 1);
        reopened.handle({StudioUiCommand::openRecentProject, createdRoot});
        CHECK(reopened.frame().projectOpen);
        CHECK(reopened.frame().projectName == "Created Game");
        reopened.handle({StudioUiCommand::quit});
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}

#undef CHECK
