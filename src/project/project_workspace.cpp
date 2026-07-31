#include "yorstudio/project_workspace.hpp"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <fstream>
#include <iterator>
#include <random>
#include <set>
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
    throw WorkspaceError(std::string(field) + ": " + std::string(message));
}

std::string requiredString(const Json& object, const char* key) {
    if (!object.contains(key) || !object.at(key).is_string() || object.at(key).get<std::string>().empty()) {
        fail(key, "must be a non-empty string");
    }
    return object.at(key).get<std::string>();
}

bool isSymlink(const fs::path& path) {
    std::error_code error;
    const auto status = fs::symlink_status(path, error);
    return !error && fs::is_symlink(status);
}

fs::path canonicalDirectory(const fs::path& path, std::string_view field) {
    if (!fs::is_directory(path) || isSymlink(path)) fail(field, "must be a real directory");
    std::error_code error;
    const fs::path canonical = fs::weakly_canonical(path, error);
    if (error) throw WorkspaceError(std::string(field) + ": cannot canonicalize path: " + error.message());
    return canonical;
}

std::string portablePath(const fs::path& path) {
    return path.lexically_normal().generic_string();
}

bool isWithin(const fs::path& child, const fs::path& parent) {
    const fs::path relative = child.lexically_relative(parent);
    if (relative.empty()) return true;
    if (relative.is_absolute()) return false;
    const auto first = relative.begin();
    return first == relative.end() || *first != "..";
}

std::string utcTimestamp() {
    const auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm utc{};
#ifdef _WIN32
    gmtime_s(&utc, &now);
#else
    gmtime_r(&now, &utc);
#endif
    char buffer[32]{};
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &utc);
    return buffer;
}

fs::path uniqueSibling(const fs::path& destination) {
    const fs::path parent = destination.parent_path().empty() ? fs::current_path() : destination.parent_path();
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    std::random_device random;
    for (int attempt = 0; attempt < 32; ++attempt) {
        const fs::path candidate = parent / ("." + destination.filename().string() + ".tmp-" +
            std::to_string(stamp) + "-" + std::to_string(random()));
        std::error_code error;
        if (!fs::exists(candidate, error) && !error) return candidate;
    }
    throw WorkspaceError("filesystem: cannot reserve a temporary registry path");
}

void publishFile(const fs::path& temporary, const fs::path& destination) {
#ifdef _WIN32
    if (!MoveFileExW(temporary.c_str(), destination.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        std::error_code error;
        fs::remove(temporary, error);
        throw WorkspaceError("recent projects: atomic replace failed");
    }
#else
    std::error_code error;
    fs::rename(temporary, destination, error);
    if (error) {
        fs::remove(temporary, error);
        throw WorkspaceError("recent projects: atomic replace failed: " + error.message());
    }
#endif
}

void recordCandidate(
    const fs::path& candidate,
    const WorkspaceRoots& roots,
    std::vector<DiscoveredProject>& projects,
    std::vector<DiscoveryIssue>& issues,
    std::set<std::string>& seen) {
    const std::string key = portablePath(candidate);
    if (!seen.insert(key).second) return;
    if (!roots.allows(candidate)) return;
    if (isSymlink(candidate)) {
        issues.push_back({candidate, "project root is a symlink"});
        return;
    }

    std::error_code error;
    const fs::path manifestPath = candidate / "project.yorproject";
    if (!fs::is_regular_file(manifestPath, error) || isSymlink(manifestPath)) return;
    try {
        const ProjectManifest manifest = validateProject(candidate, false);
        projects.push_back({candidate, manifest.projectGuid(), manifest.name()});
    } catch (const ProjectError& failure) {
        issues.push_back({candidate, failure.what()});
    }
}

} // namespace

ProjectSession ProjectSession::open(const fs::path& projectRoot, ProjectAccess access) {
    const ProjectManifest manifest = validateProject(projectRoot, false);
    std::optional<ProjectLock> lock;
    if (access == ProjectAccess::readWrite) {
        lock.emplace(ProjectLock::acquire(projectRoot));
    }
    return ProjectSession(projectRoot, manifest, access, std::move(lock));
}

ProjectSession::ProjectSession(
    fs::path root,
    ProjectManifest manifest,
    ProjectAccess access,
    std::optional<ProjectLock> lock)
    : root_(std::move(root)), manifest_(std::move(manifest)), access_(access), lock_(std::move(lock)) {}

const ProjectLockInfo* ProjectSession::lockInfo() const noexcept {
    return lock_.has_value() ? &lock_->info() : nullptr;
}

void ProjectSession::saveManifest(const ProjectManifest& manifest) {
    if (isReadOnly()) throw WorkspaceError("project session: read-only session cannot save a manifest");
    if (!lock_.has_value() || !lock_->ownsLock()) throw WorkspaceError("project session: write lock is not owned");
    if (manifest.projectGuid() != this->manifest().projectGuid()) {
        throw WorkspaceError("project session: project identity cannot change while open");
    }
    const ProjectLockInfo currentLock = ProjectLock::inspect(root_);
    if (currentLock.ownerId != lock_->info().ownerId) {
        throw WorkspaceError("project session: write lock ownership changed");
    }
    manifest.writeAtomic(ProjectPaths{root_}.manifestPath());
    manifest_ = manifest;
}

void ProjectSession::close() noexcept {
    lock_.reset();
}

WorkspaceRoots::WorkspaceRoots(std::vector<fs::path> roots) {
    for (const auto& root : roots) addRoot(root);
}

WorkspaceRoots WorkspaceRoots::fromJson(std::string_view text) {
    Json data;
    try {
        data = Json::parse(text);
    } catch (const Json::exception& error) {
        throw WorkspaceError(std::string("workspace roots: invalid JSON: ") + error.what());
    }
    if (!data.is_object()) fail("workspace roots", "must be an object");
    if (!data.contains("schema_version") || !data.at("schema_version").is_number_integer() ||
        data.at("schema_version").get<int>() != CurrentSchemaVersion) {
        fail("schema_version", "expected 1");
    }
    if (!data.contains("roots") || !data.at("roots").is_array()) fail("roots", "must be a list");

    WorkspaceRoots result;
    for (const auto& value : data.at("roots")) {
        if (!value.is_string()) fail("roots", "entries must be strings");
        result.addRoot(value.get<std::string>());
    }
    return result;
}

std::string WorkspaceRoots::toJson() const {
    Json roots = Json::array();
    for (const auto& root : roots_) roots.push_back(portablePath(root));
    return Json{{"schema_version", CurrentSchemaVersion}, {"roots", std::move(roots)}}.dump(2) + "\n";
}

void WorkspaceRoots::addRoot(const fs::path& root) {
    const fs::path canonical = canonicalDirectory(root, "workspace root");
    const std::string key = portablePath(canonical);
    for (const auto& existing : roots_) {
        if (portablePath(existing) == key) return;
    }
    roots_.push_back(canonical);
}

void WorkspaceRoots::removeRoot(const fs::path& root) {
    const fs::path canonical = canonicalDirectory(root, "workspace root");
    const std::string key = portablePath(canonical);
    roots_.erase(
        std::remove_if(roots_.begin(), roots_.end(), [&](const auto& existing) {
            return portablePath(existing) == key;
        }),
        roots_.end());
}

bool WorkspaceRoots::allows(const fs::path& projectRoot) const {
    if (projectRoot.empty()) return false;
    std::error_code error;
    const fs::path candidate = fs::weakly_canonical(projectRoot, error);
    if (error || isSymlink(projectRoot)) return false;
    return std::any_of(roots_.begin(), roots_.end(), [&](const auto& root) {
        return isWithin(candidate, root);
    });
}

ProjectSession WorkspaceRoots::openProject(const fs::path& projectRoot, ProjectAccess access) const {
    if (!allows(projectRoot)) {
        throw WorkspaceError("project session: project is outside configured workspace roots");
    }
    return ProjectSession::open(projectRoot, access);
}

std::vector<DiscoveredProject> WorkspaceRoots::discover(std::vector<DiscoveryIssue>& issues) const {
    std::vector<DiscoveredProject> projects;
    std::set<std::string> seen;
    for (const auto& root : roots_) {
        recordCandidate(root, *this, projects, issues, seen);

        std::error_code iteratorError;
        for (fs::directory_iterator iterator(root, iteratorError), end; iterator != end && !iteratorError; iterator.increment(iteratorError)) {
            const fs::path candidate = iterator->path();
            if (isSymlink(candidate)) {
                if (fs::is_directory(candidate)) issues.push_back({candidate, "symlink directory is not scanned"});
                continue;
            }
            if (!fs::is_directory(candidate)) continue;
            recordCandidate(candidate, *this, projects, issues, seen);
        }
        if (iteratorError) issues.push_back({root, "cannot enumerate workspace root: " + iteratorError.message()});
    }
    return projects;
}

RecentProjects RecentProjects::read(const fs::path& path) {
    RecentProjects result;
    if (!fs::exists(path)) return result;
    std::ifstream stream(path, std::ios::binary);
    if (!stream) throw WorkspaceError("recent projects: cannot open registry");
    const std::string text((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());

    Json data;
    try {
        data = Json::parse(text);
    } catch (const Json::exception& error) {
        throw WorkspaceError(std::string("recent projects: invalid JSON: ") + error.what());
    }
    if (!data.is_object()) fail("recent projects", "must be an object");
    if (!data.contains("schema_version") || !data.at("schema_version").is_number_integer() ||
        data.at("schema_version").get<int>() != CurrentSchemaVersion) {
        fail("schema_version", "expected 1");
    }
    if (!data.contains("entries") || !data.at("entries").is_array()) fail("entries", "must be a list");

    std::set<std::string> seen;
    for (const auto& entry : data.at("entries")) {
        if (!entry.is_object()) fail("entries", "each entry must be an object");
        const fs::path root = entry.contains("root") ? fs::path(requiredString(entry, "root")) : fs::path{};
        if (!root.is_absolute()) fail("root", "must be an absolute path");
        const fs::path normalized = root.lexically_normal();
        const std::string key = portablePath(normalized);
        if (!seen.insert(key).second) fail("entries", "must not contain duplicate roots");
        result.entries_.push_back({
            normalized,
            requiredString(entry, "project_guid"),
            requiredString(entry, "name"),
            requiredString(entry, "last_opened_at"),
        });
        if (result.entries_.size() > MaximumEntries) fail("entries", "exceeds the maximum entry count");
    }
    return result;
}

void RecentProjects::record(const fs::path& projectRoot, const ProjectManifest& manifest) {
    const fs::path canonical = canonicalDirectory(projectRoot, "project root");
    const ProjectManifest actual = validateProject(canonical, false);
    if (actual.projectGuid() != manifest.projectGuid()) {
        throw WorkspaceError("recent projects: manifest identity does not match project root");
    }
    const std::string key = portablePath(canonical);
    entries_.erase(
        std::remove_if(entries_.begin(), entries_.end(), [&](const auto& entry) {
            return portablePath(entry.root) == key;
        }),
        entries_.end());
    entries_.insert(entries_.begin(), {canonical, actual.projectGuid(), actual.name(), utcTimestamp()});
    if (entries_.size() > MaximumEntries) entries_.resize(MaximumEntries);
}

void RecentProjects::remove(const fs::path& projectRoot) {
    fs::path normalized;
    if (fs::is_directory(projectRoot) && !isSymlink(projectRoot)) {
        normalized = canonicalDirectory(projectRoot, "project root");
    } else {
        std::error_code error;
        normalized = fs::weakly_canonical(projectRoot, error);
        if (error) throw WorkspaceError("project root: cannot resolve path: " + error.message());
    }
    const std::string key = portablePath(normalized);
    entries_.erase(
        std::remove_if(entries_.begin(), entries_.end(), [&](const auto& entry) {
            return portablePath(entry.root) == key;
        }),
        entries_.end());
}

void RecentProjects::writeAtomic(const fs::path& path) const {
    const fs::path parent = path.parent_path().empty() ? fs::current_path() : path.parent_path();
    if (!fs::is_directory(parent)) throw WorkspaceError("recent projects: parent directory does not exist");
    Json entries = Json::array();
    for (const auto& entry : entries_) {
        entries.push_back({
            {"root", portablePath(entry.root)},
            {"project_guid", entry.projectGuid},
            {"name", entry.name},
            {"last_opened_at", entry.lastOpenedAt},
        });
    }
    const fs::path temporary = uniqueSibling(path);
    try {
        std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
        if (!stream) throw WorkspaceError("recent projects: cannot create temporary registry");
        stream << Json{{"schema_version", CurrentSchemaVersion}, {"entries", std::move(entries)}}.dump(2) << '\n';
        stream.flush();
        if (!stream) throw WorkspaceError("recent projects: cannot write temporary registry");
        stream.close();
        publishFile(temporary, path);
    } catch (...) {
        std::error_code error;
        fs::remove(temporary, error);
        throw;
    }
}

} // namespace yorstudio
