#include "yorstudio/project_manifest.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <iterator>
#include <random>
#include <set>
#include <system_error>

#ifdef _WIN32
#include <windows.h>
#endif

namespace yorstudio {

namespace {

using Json = nlohmann::json;
namespace fs = std::filesystem;

[[noreturn]] void fail(std::string_view field, std::string_view message) {
    throw ProjectError(std::string(field) + ": " + std::string(message));
}

const Json& requireObject(const Json& value, std::string_view field) {
    if (!value.is_object()) fail(field, "must be an object");
    return value;
}

const Json& requiredObjectField(const Json& object, const char* key) {
    if (!object.contains(key)) fail(key, "is required");
    return requireObject(object.at(key), key);
}

std::string requiredString(const Json& value, std::string_view field) {
    if (!value.is_string() || value.get<std::string>().empty()) fail(field, "must be a non-empty string");
    std::string result = value.get<std::string>();
    if (std::any_of(result.begin(), result.end(), [](char character) {
            return static_cast<unsigned char>(character) < 0x20;
        })) {
        fail(field, "must not contain control characters");
    }
    return result;
}

std::string normalizeRelativePath(const Json& value, std::string_view field) {
    std::string path = requiredString(value, field);
    std::replace(path.begin(), path.end(), '\\', '/');
    if (path.front() == '/' || (path.size() >= 2 && std::isalpha(static_cast<unsigned char>(path[0])) && path[1] == ':')) {
        fail(field, "must be a relative portable path");
    }

    std::vector<std::string> parts;
    std::size_t begin = 0;
    while (begin <= path.size()) {
        const std::size_t end = path.find('/', begin);
        const std::string part = path.substr(begin, end == std::string::npos ? std::string::npos : end - begin);
        if (part.empty() || part == "." || part == "..") fail(field, "contains an invalid path segment");
        if (part.find(':') != std::string::npos) fail(field, "must not contain a drive or stream separator");
        parts.push_back(part);
        if (end == std::string::npos) break;
        begin = end + 1;
    }

    std::string normalized;
    for (const auto& part : parts) {
        if (!normalized.empty()) normalized += '/';
        normalized += part;
    }
    return normalized;
}

std::vector<std::string> relativeStringList(const Json& value, std::string_view field) {
    if (!value.is_array()) fail(field, "must be a list");
    std::vector<std::string> result;
    std::set<std::string> unique;
    for (std::size_t index = 0; index < value.size(); ++index) {
        const std::string item = normalizeRelativePath(value[index], std::string(field) + '[' + std::to_string(index) + ']');
        if (!unique.insert(item).second) fail(field, "must not contain duplicates");
        result.push_back(item);
    }
    return result;
}

std::vector<std::string> identifierList(const Json& value, std::string_view field) {
    if (!value.is_array()) fail(field, "must be a list");
    std::vector<std::string> result;
    std::set<std::string> unique;
    for (std::size_t index = 0; index < value.size(); ++index) {
        const std::string item = requiredString(value[index], std::string(field) + '[' + std::to_string(index) + ']');
        if (!std::all_of(item.begin(), item.end(), [](char character) {
                return std::isalnum(static_cast<unsigned char>(character)) || character == '-' || character == '_' || character == '.';
            })) {
            fail(field, "entries must be portable identifiers");
        }
        if (!unique.insert(item).second) fail(field, "must not contain duplicates");
        result.push_back(item);
    }
    return result;
}

std::string canonicalGuid(std::string value) {
    if (value.size() != 36 || value[8] != '-' || value[13] != '-' || value[18] != '-' || value[23] != '-') {
        fail("project_guid", "must be a UUID");
    }
    for (std::size_t index = 0; index < value.size(); ++index) {
        if (value[index] == '-') continue;
        if (!std::isxdigit(static_cast<unsigned char>(value[index]))) fail("project_guid", "must be a UUID");
        value[index] = static_cast<char>(std::tolower(static_cast<unsigned char>(value[index])));
    }
    return value;
}

std::string newGuid() {
    std::array<unsigned char, 16> bytes{};
    std::random_device random;
    for (auto& byte : bytes) byte = static_cast<unsigned char>(random());
    bytes[6] = static_cast<unsigned char>((bytes[6] & 0x0f) | 0x40);
    bytes[8] = static_cast<unsigned char>((bytes[8] & 0x3f) | 0x80);
    constexpr char digits[] = "0123456789abcdef";
    std::string result;
    result.reserve(36);
    for (std::size_t index = 0; index < bytes.size(); ++index) {
        if (index == 4 || index == 6 || index == 8 || index == 10) result += '-';
        result += digits[bytes[index] >> 4];
        result += digits[bytes[index] & 0x0f];
    }
    return result;
}

Json unknownFields(const Json& object, std::initializer_list<const char*> known) {
    Json result = Json::object();
    for (const auto& [key, value] : object.items()) {
        if (std::find(known.begin(), known.end(), key) == known.end()) result[key] = value;
    }
    return result;
}

fs::path uniqueSibling(const fs::path& root, std::string_view suffix) {
    const fs::path parent = root.parent_path().empty() ? fs::current_path() : root.parent_path();
    for (int attempt = 0; attempt < 32; ++attempt) {
        const fs::path candidate = parent / ("." + root.filename().string() + "-" + std::string(suffix) + "-" + newGuid());
        std::error_code error;
        if (!fs::exists(candidate, error) && !error) return candidate;
    }
    throw ProjectError("filesystem: cannot reserve a unique temporary path");
}

bool containsSymlink(const fs::path& root, const fs::path& relative) {
    std::error_code error;
    fs::path current = root;
    const auto rootStatus = fs::symlink_status(current, error);
    if (error || fs::is_symlink(rootStatus)) return true;
    for (const auto& part : relative) {
        current /= part;
        error.clear();
        const auto status = fs::symlink_status(current, error);
        if (!error && fs::is_symlink(status)) return true;
    }
    return false;
}

void removeTemporary(const fs::path& path) noexcept {
    std::error_code error;
    fs::remove_all(path, error);
}

void publishFile(const fs::path& temporary, const fs::path& destination) {
#ifdef _WIN32
    if (!MoveFileExW(temporary.c_str(), destination.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        removeTemporary(temporary);
        throw ProjectError("manifest: atomic replace failed");
    }
#else
    std::error_code error;
    fs::rename(temporary, destination, error);
    if (error) {
        removeTemporary(temporary);
        throw ProjectError("manifest: atomic replace failed: " + error.message());
    }
#endif
}

} // namespace

fs::path ProjectPaths::manifestPath() const {
    return root / "project.yorproject";
}

fs::path ProjectPaths::hiddenStatePath() const {
    return root / ".yor";
}

fs::path ProjectPaths::lockPath() const {
    return hiddenStatePath() / "project.lock";
}

ProjectManifest::ProjectManifest(
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
    Json extra,
    Json engineExtra,
    Json toolchainExtra,
    Json editorExtra)
    : schemaVersion_(schemaVersion),
      projectGuid_(std::move(projectGuid)),
      name_(std::move(name)),
      engineRepository_(std::move(engineRepository)),
      engineVersion_(std::move(engineVersion)),
      cxxStandard_(std::move(cxxStandard)),
      startupScene_(std::move(startupScene)),
      targetPlatforms_(std::move(targetPlatforms)),
      modules_(std::move(modules)),
      contentRoots_(std::move(contentRoots)),
      uiAdapter_(std::move(uiAdapter)),
      extra_(std::move(extra)),
      engineExtra_(std::move(engineExtra)),
      toolchainExtra_(std::move(toolchainExtra)),
      editorExtra_(std::move(editorExtra)) {}

ProjectManifest ProjectManifest::create(
    std::string name,
    std::string engineVersion,
    std::string projectGuid,
    std::string startupScene,
    std::vector<std::string> targetPlatforms,
    std::vector<std::string> modules,
    std::vector<std::string> contentRoots,
    std::string uiAdapter) {
    Json data = {
        {"schema_version", CurrentSchemaVersion},
        {"project_guid", projectGuid.empty() ? newGuid() : std::move(projectGuid)},
        {"name", std::move(name)},
        {"engine", {{"repository", CanonicalEngineRepository}, {"version", std::move(engineVersion)}}},
        {"toolchain", {{"cxx_standard", "c++20"}}},
        {"startup_scene", std::move(startupScene)},
        {"target_platforms", std::move(targetPlatforms)},
        {"modules", std::move(modules)},
        {"content_roots", std::move(contentRoots)},
        {"editor", {{"ui_adapter", std::move(uiAdapter)}}},
    };
    return fromJson(data.dump());
}

Json ProjectManifest::migrate(Json data) {
    if (!data.is_object()) fail("manifest", "must be an object");
    int version = 0;
    if (data.contains("schema_version")) {
        if (!data["schema_version"].is_number_integer()) fail("schema_version", "must be an integer");
        version = data["schema_version"].get<int>();
    }
    if (version == CurrentSchemaVersion) return data;
    if (version != 0) fail("schema_version", "unsupported version");

    if (!data.contains("name") && data.contains("display_name")) data["name"] = data["display_name"];
    if (!data.contains("project_guid") && data.contains("guid")) data["project_guid"] = data["guid"];
    if (!data.contains("engine") && data.contains("engine_version")) {
        data["engine"] = {{"repository", CanonicalEngineRepository}, {"version", data["engine_version"]}};
    }
    if (!data.contains("toolchain")) data["toolchain"] = {{"cxx_standard", "c++20"}};
    if (!data.contains("startup_scene")) data["startup_scene"] = "scenes/main.yorscene";
    if (!data.contains("target_platforms")) data["target_platforms"] = {"windows-x64"};
    if (!data.contains("modules")) data["modules"] = Json::array();
    if (!data.contains("content_roots")) data["content_roots"] = {"assets", "scenes", "shaders"};
    if (!data.contains("editor")) data["editor"] = {{"ui_adapter", "imgui"}};
    data["schema_version"] = CurrentSchemaVersion;
    return data;
}

ProjectManifest ProjectManifest::fromJson(std::string_view text) {
    Json data;
    try {
        data = Json::parse(text);
    } catch (const Json::exception& error) {
        throw ProjectError(std::string("manifest: invalid JSON: ") + error.what());
    }
    data = migrate(std::move(data));

    const int schemaVersion = data.value("schema_version", 0);
    if (schemaVersion != CurrentSchemaVersion) fail("schema_version", "expected 1");
    const std::string projectGuid = canonicalGuid(requiredString(data.value("project_guid", Json{}), "project_guid"));
    std::string name = requiredString(data.value("name", Json{}), "name");
    if (name == "." || name == ".." || name.front() == ' ' || name.back() == ' ' || name.back() == '.' ||
        name.find_first_of("\\/:") != std::string::npos) {
        fail("name", "must be path-safe and must not have surrounding whitespace");
    }

    const Json& engine = requiredObjectField(data, "engine");
    const std::string repository = requiredString(engine.value("repository", Json{}), "engine.repository");
    if (repository != CanonicalEngineRepository) fail("engine.repository", "must use the canonical YorEngine URL");
    const std::string engineVersion = requiredString(engine.value("version", Json{}), "engine.version");
    const bool immutableCommit = engineVersion.size() == 40 && std::all_of(engineVersion.begin(), engineVersion.end(), [](char character) {
        return std::isxdigit(static_cast<unsigned char>(character));
    });
    const bool releaseTag = engineVersion.size() > 1 && engineVersion.front() == 'v' &&
        std::isdigit(static_cast<unsigned char>(engineVersion[1]));
    if (!immutableCommit && !releaseTag) {
        fail("engine.version", "must be a release tag or immutable commit");
    }

    const Json& toolchain = requiredObjectField(data, "toolchain");
    const std::string cxxStandard = requiredString(toolchain.value("cxx_standard", Json{}), "toolchain.cxx_standard");
    if (cxxStandard != "c++20") fail("toolchain.cxx_standard", "must be c++20");

    Json startupJson = data.value("startup_scene", Json{});
    const std::string startupScene = normalizeRelativePath(startupJson, "startup_scene");
    Json targetJson = data.value("target_platforms", Json{});
    Json moduleJson = data.value("modules", Json{});
    Json rootsJson = data.value("content_roots", Json{});
    const auto targetPlatforms = identifierList(targetJson, "target_platforms");
    const auto modules = identifierList(moduleJson, "modules");
    const auto contentRoots = relativeStringList(rootsJson, "content_roots");
    if (contentRoots.empty()) fail("content_roots", "must not be empty");
    if (std::none_of(contentRoots.begin(), contentRoots.end(), [&](const auto& root) {
            return startupScene == root || startupScene.starts_with(root + '/');
        })) {
        fail("startup_scene", "must be inside one of content_roots");
    }

    const Json& editor = requiredObjectField(data, "editor");
    const std::string uiAdapter = requiredString(editor.value("ui_adapter", Json{}), "editor.ui_adapter");
    if (uiAdapter != "imgui") fail("editor.ui_adapter", "must be imgui in schema v1");

    return ProjectManifest(
        schemaVersion,
        projectGuid,
        std::move(name),
        repository,
        engineVersion,
        cxxStandard,
        startupScene,
        targetPlatforms,
        modules,
        contentRoots,
        uiAdapter,
        unknownFields(data, {"schema_version", "project_guid", "name", "engine", "toolchain", "startup_scene", "target_platforms", "modules", "content_roots", "editor"}),
        unknownFields(engine, {"repository", "version"}),
        unknownFields(toolchain, {"cxx_standard"}),
        unknownFields(editor, {"ui_adapter"}));
}

ProjectManifest ProjectManifest::read(const fs::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) throw ProjectError("manifest: cannot open " + path.string());
    const std::string text(
        (std::istreambuf_iterator<char>(stream)),
        std::istreambuf_iterator<char>());
    return fromJson(text);
}

std::string ProjectManifest::toJson() const {
    Json result = extra_.is_object() ? extra_ : Json::object();
    Json engine = engineExtra_.is_object() ? engineExtra_ : Json::object();
    Json toolchain = toolchainExtra_.is_object() ? toolchainExtra_ : Json::object();
    Json editor = editorExtra_.is_object() ? editorExtra_ : Json::object();
    engine["repository"] = engineRepository_;
    engine["version"] = engineVersion_;
    toolchain["cxx_standard"] = cxxStandard_;
    editor["ui_adapter"] = uiAdapter_;
    result["schema_version"] = schemaVersion_;
    result["project_guid"] = projectGuid_;
    result["name"] = name_;
    result["engine"] = std::move(engine);
    result["toolchain"] = std::move(toolchain);
    result["startup_scene"] = startupScene_;
    result["target_platforms"] = targetPlatforms_;
    result["modules"] = modules_;
    result["content_roots"] = contentRoots_;
    result["editor"] = std::move(editor);
    return result.dump(2) + "\n";
}

void ProjectManifest::writeAtomic(const fs::path& path) const {
    const fs::path parent = path.parent_path().empty() ? fs::current_path() : path.parent_path();
    if (!fs::is_directory(parent)) throw ProjectError("manifest: parent directory does not exist");
    const fs::path temporary = uniqueSibling(path, "manifest.tmp");
    try {
        std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
        if (!stream) throw ProjectError("manifest: cannot create temporary file");
        stream << toJson();
        stream.flush();
        if (!stream) throw ProjectError("manifest: cannot write temporary file");
        stream.close();
        publishFile(temporary, path);
    } catch (...) {
        removeTemporary(temporary);
        throw;
    }
}

ProjectPaths createProject(const fs::path& root, const ProjectManifest& manifest) {
    if (fs::exists(root) || fs::is_symlink(root)) throw ProjectError("project: target already exists");
    const fs::path parent = root.parent_path().empty() ? fs::current_path() : root.parent_path();
    std::error_code error;
    fs::create_directories(parent, error);
    if (error) throw ProjectError("project: cannot create parent: " + error.message());
    const fs::path temporary = uniqueSibling(root, "create");

    try {
        fs::create_directory(temporary);
        for (const char* relative : {
                 "code/include", "code/src", "code/tests", "config", "plugins", "build", ".yor/editor", ".yor/cache",
                 ".yor/derived", ".yor/logs", ".yor/generated", ".yor/recovery"}) {
            fs::create_directories(temporary / relative);
        }
        for (const auto& contentRoot : manifest.contentRoots()) {
            fs::create_directories(temporary / fs::path(contentRoot));
        }
        manifest.writeAtomic(temporary / "project.yorproject");
        const fs::path startupScene = temporary / fs::path(manifest.startupScene());
        fs::create_directories(startupScene.parent_path());
        std::ofstream scene(startupScene, std::ios::binary | std::ios::trunc);
        if (!scene) throw ProjectError("project: cannot create startup scene");
        scene << "{\n  \"schema_version\": 1,\n  \"objects\": []\n}\n";
        scene.close();
        fs::rename(temporary, root, error);
        if (error) throw ProjectError("project: cannot publish directory: " + error.message());
    } catch (...) {
        removeTemporary(temporary);
        throw;
    }
    return {root};
}

ProjectManifest validateProject(const fs::path& root, bool requireLayout) {
    if (!fs::is_directory(root) || containsSymlink(root, {})) throw ProjectError("project: root must be a real directory");
    ProjectManifest manifest = ProjectManifest::read(root / "project.yorproject");
    if (!requireLayout) return manifest;
    for (const auto& contentRoot : manifest.contentRoots()) {
        const fs::path path = fs::path(contentRoot);
        if (containsSymlink(root, path) || !fs::is_directory(root / path)) {
            throw ProjectError(contentRoot + ": content root is missing or uses a symlink");
        }
    }
    const fs::path startupScene = fs::path(manifest.startupScene());
    if (containsSymlink(root, startupScene) || !fs::is_regular_file(root / startupScene)) {
        throw ProjectError(manifest.startupScene() + ": startup scene is missing or uses a symlink");
    }
    return manifest;
}

} // namespace yorstudio
