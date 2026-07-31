#include "yorstudio/project_manifest.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
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

        const ProjectManifest custom = ProjectManifest::create(
            "Custom Roots", "v0.1.0", {}, "content/main.yorscene", {"linux-x64"}, {"gameplay"}, {"content"});
        const auto customPaths = createProject(temporary.path / "custom", custom);
        CHECK(std::filesystem::is_directory(customPaths.root / "content"));
        CHECK(validateProject(customPaths.root, true).startupScene() == "content/main.yorscene");

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
