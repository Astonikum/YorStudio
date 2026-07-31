#include "yorstudio/project_lifecycle.hpp"

#include <chrono>
#include <fstream>
#include <iterator>
#include <random>
#include <system_error>
#include <utility>

#ifdef _WIN32
#include <windows.h>
#endif

namespace yorstudio {

namespace {

using Json = nlohmann::json;
namespace fs = std::filesystem;

[[noreturn]] void fail(std::string_view field, std::string_view message) {
    throw ProjectOperationError(std::string(field) + ": " + std::string(message));
}

bool isSymlink(const fs::path& path) {
    std::error_code error;
    const auto status = fs::symlink_status(path, error);
    return !error && fs::is_symlink(status);
}

fs::path absoluteNormalized(const fs::path& path, std::string_view field) {
    if (path.empty()) fail(field, "must not be empty");
    std::error_code error;
    const fs::path absolute = fs::absolute(path, error);
    if (error) throw ProjectOperationError(std::string(field) + ": cannot resolve path: " + error.message());
    return absolute.lexically_normal();
}

fs::path canonicalProjectRoot(const fs::path& path, std::string_view field) {
    const fs::path absolute = absoluteNormalized(path, field);
    if (!fs::is_directory(absolute) || isSymlink(absolute)) fail(field, "must be a real directory");
    std::error_code error;
    const fs::path canonical = fs::weakly_canonical(absolute, error);
    if (error) throw ProjectOperationError(std::string(field) + ": cannot canonicalize path: " + error.message());
    return canonical;
}

bool isWithin(const fs::path& child, const fs::path& parent) {
    const fs::path relative = child.lexically_relative(parent);
    if (relative.empty()) return true;
    if (relative.is_absolute()) return false;
    const auto first = relative.begin();
    return first == relative.end() || *first != "..";
}

fs::path temporarySibling(const fs::path& destination, std::string_view operation) {
    const fs::path parent = destination.parent_path().empty() ? fs::current_path() : destination.parent_path();
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    std::random_device random;
    for (int attempt = 0; attempt < 32; ++attempt) {
        const fs::path candidate = parent / ("." + destination.filename().string() + "-" + std::string(operation) + "-" +
            std::to_string(stamp) + "-" + std::to_string(random()));
        std::error_code error;
        if (!fs::exists(candidate, error) && !error) return candidate;
    }
    throw ProjectOperationError("filesystem: cannot reserve a temporary project path");
}

void removeNoexcept(const fs::path& path) noexcept {
    std::error_code error;
    fs::remove_all(path, error);
}

void publishFile(const fs::path& temporary, const fs::path& destination) {
#ifdef _WIN32
    if (!MoveFileExW(temporary.c_str(), destination.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        removeNoexcept(temporary);
        throw ProjectOperationError("safe mode: atomic replace failed");
    }
#else
    std::error_code error;
    fs::rename(temporary, destination, error);
    if (error) {
        removeNoexcept(temporary);
        throw ProjectOperationError("safe mode: atomic replace failed: " + error.message());
    }
#endif
}

void ensureUnlocked(const fs::path& projectRoot) {
    const fs::path lockPath = ProjectPaths{projectRoot}.lockPath();
    std::error_code error;
    if (!fs::exists(lockPath, error)) {
        if (error) throw ProjectOperationError("project: cannot inspect lock: " + error.message());
        return;
    }
    try {
        const ProjectLockInfo info = ProjectLock::inspect(projectRoot);
        throw ProjectOperationError("project: locked by " + info.host + " process " + std::to_string(info.processId));
    } catch (const ProjectOperationError&) {
        throw;
    } catch (const ProjectLockError& failure) {
        throw ProjectOperationError(std::string("project: lock cannot be inspected: ") + failure.what());
    }
}

void createDisposableState(const fs::path& root) {
    for (const char* relative : {"editor", "cache", "derived", "logs", "generated", "recovery"}) {
        std::error_code error;
        fs::create_directories(root / ".yor" / relative, error);
        if (error) throw ProjectOperationError("project: cannot create .yor state: " + error.message());
    }
}

void copyTree(const fs::path& source, const fs::path& destination) {
    std::error_code error;
    fs::create_directory(destination, error);
    if (error) throw ProjectOperationError("project: cannot create destination: " + error.message());

    for (fs::directory_iterator iterator(source, error), end; iterator != end && !error; iterator.increment(error)) {
        const fs::path entry = iterator->path();
        const std::string name = entry.filename().string();
        if (name == ".yor" || name == "build") continue;
        if (isSymlink(entry)) fail("project", "source contains a symlink: " + entry.string());
        const fs::path target = destination / entry.filename();
        const auto status = fs::status(entry, error);
        if (error) throw ProjectOperationError("project: cannot inspect source entry: " + error.message());
        if (fs::is_directory(status)) {
            copyTree(entry, target);
        } else if (fs::is_regular_file(status)) {
            fs::copy_file(entry, target, fs::copy_options::overwrite_existing, error);
            if (error) throw ProjectOperationError("project: cannot copy source file: " + error.message());
        } else {
            fail("project", "source contains an unsupported filesystem entry: " + entry.string());
        }
    }
    if (error) throw ProjectOperationError("project: cannot enumerate source: " + error.message());
}

ProjectPaths copyProject(
    const WorkspaceRoots& roots,
    const fs::path& sourceRoot,
    const fs::path& destinationRoot,
    std::string newName,
    bool reidentify,
    std::string_view operation) {
    const fs::path source = canonicalProjectRoot(sourceRoot, "source project");
    const fs::path destination = absoluteNormalized(destinationRoot, "destination project");
    if (isWithin(destination, source)) fail("destination project", "must not be inside the source project");
    if (!roots.allows(destination)) fail("destination project", "is outside configured workspace roots");
    std::error_code error;
    if (fs::exists(destination, error) || error) {
        if (error) throw ProjectOperationError("destination project: cannot inspect path: " + error.message());
        fail("destination project", "already exists");
    }
    const ProjectManifest sourceManifest = validateProject(source, true);
    ensureUnlocked(source);
    ProjectManifest destinationManifest = sourceManifest;
    if (reidentify) {
        destinationManifest = sourceManifest.reidentify(std::move(newName));
    } else if (!newName.empty()) {
        destinationManifest = sourceManifest.rename(std::move(newName));
    }
    fs::create_directories(destination.parent_path(), error);
    if (error) throw ProjectOperationError("destination project: cannot create parent: " + error.message());
    const fs::path temporary = temporarySibling(destination, operation);
    try {
        copyTree(source, temporary);
        createDisposableState(temporary);
        destinationManifest.writeAtomic(temporary / "project.yorproject");
        fs::rename(temporary, destination, error);
        if (error) throw ProjectOperationError("destination project: cannot publish copy: " + error.message());
    } catch (...) {
        removeNoexcept(temporary);
        throw;
    }
    return {destination};
}

} // namespace

ProjectPaths newProject(const WorkspaceRoots& roots, const fs::path& projectRoot, const ProjectManifest& manifest) {
    const fs::path destination = absoluteNormalized(projectRoot, "project");
    if (!roots.allows(destination)) fail("project", "is outside configured workspace roots");
    return createProject(destination, manifest);
}

ProjectSession openProject(const WorkspaceRoots& roots, const fs::path& projectRoot, ProjectAccess access) {
    return roots.openProject(projectRoot, access);
}

ProjectPaths importProject(const WorkspaceRoots& roots, const fs::path& sourceRoot, const fs::path& destinationRoot, std::string newName) {
    return copyProject(roots, sourceRoot, destinationRoot, std::move(newName), false, "import");
}

ProjectPaths duplicateProject(const WorkspaceRoots& roots, const fs::path& sourceRoot, const fs::path& destinationRoot, std::string newName) {
    const fs::path source = canonicalProjectRoot(sourceRoot, "source project");
    const ProjectManifest manifest = validateProject(source, false);
    if (newName.empty()) newName = manifest.name() + " Copy";
    return copyProject(roots, source, destinationRoot, std::move(newName), true, "duplicate");
}

ProjectManifest migrateProject(const fs::path& projectRoot) {
    const fs::path root = canonicalProjectRoot(projectRoot, "project");
    ensureUnlocked(root);
    const ProjectManifest manifest = ProjectManifest::read(ProjectPaths{root}.manifestPath());
    manifest.writeAtomic(ProjectPaths{root}.manifestPath());
    return manifest;
}

fs::path revealProject(const WorkspaceRoots& roots, const fs::path& projectRoot) {
    const fs::path root = canonicalProjectRoot(projectRoot, "project");
    if (!roots.allows(root)) fail("project", "is outside configured workspace roots");
    validateProject(root, false);
    return root;
}

void removeRecentProject(RecentProjects& recent, const fs::path& projectRoot) {
    recent.remove(projectRoot);
}

SafeModeState readSafeMode(const fs::path& projectRoot) {
    const fs::path root = canonicalProjectRoot(projectRoot, "project");
    const fs::path path = ProjectPaths{root}.safeModePath();
    std::error_code error;
    if (!fs::exists(path, error)) {
        if (error) throw ProjectOperationError("safe mode: cannot inspect marker: " + error.message());
        return {};
    }
    std::ifstream stream(path, std::ios::binary);
    if (!stream) throw ProjectOperationError("safe mode: cannot open marker");
    const std::string text((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
    try {
        const Json data = Json::parse(text);
        if (!data.is_object() || data.value("schema_version", 0) != 1 || !data.contains("enabled") || !data.at("enabled").is_boolean()) {
            fail("safe mode", "marker has an invalid schema");
        }
        if (data.contains("reason") && !data.at("reason").is_string()) fail("safe mode", "reason must be a string");
        return {data.at("enabled").get<bool>(), data.value("reason", "")};
    } catch (const Json::exception& failure) {
        throw ProjectOperationError(std::string("safe mode: invalid JSON: ") + failure.what());
    }
}

void setSafeMode(const fs::path& projectRoot, bool enabled, std::string reason) {
    const fs::path root = canonicalProjectRoot(projectRoot, "project");
    ensureUnlocked(root);
    const fs::path path = ProjectPaths{root}.safeModePath();
    if (!enabled) {
        std::error_code error;
        fs::remove(path, error);
        if (error) throw ProjectOperationError("safe mode: cannot remove marker: " + error.message());
        return;
    }
    createDisposableState(root);
    const fs::path temporary = temporarySibling(path, "safe-mode");
    try {
        std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
        if (!stream) throw ProjectOperationError("safe mode: cannot create marker");
        stream << Json{{"schema_version", 1}, {"enabled", true}, {"reason", std::move(reason)}}.dump(2) << '\n';
        stream.flush();
        if (!stream) throw ProjectOperationError("safe mode: cannot write marker");
        stream.close();
        publishFile(temporary, path);
    } catch (...) {
        removeNoexcept(temporary);
        throw;
    }
}

void resetDisposableState(const fs::path& projectRoot) {
    const fs::path root = canonicalProjectRoot(projectRoot, "project");
    ensureUnlocked(root);
    validateProject(root, false);
    for (const char* relative : {"editor", "cache", "derived", "logs", "generated", "recovery"}) {
        std::error_code error;
        fs::remove_all(root / ".yor" / relative, error);
        if (error) throw ProjectOperationError("project: cannot reset disposable state: " + error.message());
    }
}

void recoverProject(const fs::path& projectRoot) {
    const fs::path root = canonicalProjectRoot(projectRoot, "project");
    const fs::path lockPath = ProjectPaths{root}.lockPath();
    std::error_code error;
    if (!fs::exists(lockPath, error)) {
        if (error) throw ProjectOperationError("project recovery: cannot inspect lock: " + error.message());
        return;
    }
    ProjectLock::recoverStale(root);
}

} // namespace yorstudio
