#include "yorstudio/project_lock.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <limits>
#include <random>
#include <sstream>
#include <system_error>
#include <utility>

#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <signal.h>
#include <unistd.h>
#endif

namespace yorstudio {

namespace {

using Json = nlohmann::json;
namespace fs = std::filesystem;

[[noreturn]] void fail(std::string_view field, std::string_view message) {
    throw ProjectLockError(std::string(field) + ": " + std::string(message));
}

std::string requiredString(const Json& object, const char* key) {
    if (!object.contains(key) || !object.at(key).is_string() || object.at(key).get<std::string>().empty()) {
        fail(key, "must be a non-empty string");
    }
    return object.at(key).get<std::string>();
}

std::string newOwnerId() {
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

std::string utcTimestamp() {
    const auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm utc{};
#ifdef _WIN32
    gmtime_s(&utc, &now);
#else
    gmtime_r(&now, &utc);
#endif
    std::ostringstream result;
    result << std::put_time(&utc, "%Y-%m-%dT%H:%M:%SZ");
    return result.str();
}

bool processAlive(std::uint64_t processId) {
    if (processId == 0) return false;
#ifdef _WIN32
    if (processId > static_cast<std::uint64_t>((std::numeric_limits<DWORD>::max)())) return false;
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, static_cast<DWORD>(processId));
    if (process == nullptr) return GetLastError() == ERROR_ACCESS_DENIED;
    DWORD exitCode = 0;
    const bool alive = GetExitCodeProcess(process, &exitCode) != 0 && exitCode == STILL_ACTIVE;
    CloseHandle(process);
    return alive;
#else
    if (processId > static_cast<std::uint64_t>((std::numeric_limits<pid_t>::max)())) return false;
    if (::kill(static_cast<pid_t>(processId), 0) == 0) return true;
    return errno == EPERM;
#endif
}

void removeNoexcept(const fs::path& path) noexcept {
    std::error_code error;
    fs::remove(path, error);
}

void writeExclusive(const fs::path& path, std::string_view contents) {
#ifdef _WIN32
    HANDLE handle = CreateFileW(
        path.c_str(),
        GENERIC_WRITE,
        FILE_SHARE_READ,
        nullptr,
        CREATE_NEW,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        if (GetLastError() == ERROR_FILE_EXISTS || GetLastError() == ERROR_ALREADY_EXISTS) {
            throw ProjectLockError("project.lock: project is already locked");
        }
        throw ProjectLockError("project.lock: cannot create exclusive lock file");
    }

    bool success = true;
    std::size_t offset = 0;
    while (offset < contents.size()) {
        const DWORD chunk = static_cast<DWORD>(std::min<std::size_t>(contents.size() - offset, 1U << 20));
        DWORD written = 0;
        if (!WriteFile(handle, contents.data() + offset, chunk, &written, nullptr) || written != chunk) {
            success = false;
            break;
        }
        offset += written;
    }
    if (success && FlushFileBuffers(handle) == 0) success = false;
    CloseHandle(handle);
    if (!success) {
        removeNoexcept(path);
        throw ProjectLockError("project.lock: cannot write exclusive lock file");
    }
#else
    const int descriptor = ::open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0644);
    if (descriptor == -1) {
        if (errno == EEXIST) throw ProjectLockError("project.lock: project is already locked");
        throw ProjectLockError(std::string("project.lock: cannot create exclusive lock file: ") + std::strerror(errno));
    }

    bool success = true;
    std::size_t offset = 0;
    while (offset < contents.size()) {
        const ssize_t written = ::write(descriptor, contents.data() + offset, contents.size() - offset);
        if (written <= 0) {
            if (errno == EINTR) continue;
            success = false;
            break;
        }
        offset += static_cast<std::size_t>(written);
    }
    if (success && ::fsync(descriptor) != 0) success = false;
    if (::close(descriptor) != 0) success = false;
    if (!success) {
        removeNoexcept(path);
        throw ProjectLockError(std::string("project.lock: cannot write exclusive lock file: ") + std::strerror(errno));
    }
#endif
}

std::string readText(const fs::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) throw ProjectLockError("project.lock: cannot open lock file");
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

} // namespace

ProjectLockInfo ProjectLockInfo::fromJson(std::string_view text) {
    Json data;
    try {
        data = Json::parse(text);
    } catch (const Json::exception& error) {
        throw ProjectLockError(std::string("project.lock: invalid JSON: ") + error.what());
    }
    if (!data.is_object()) fail("project.lock", "must be an object");
    if (!data.contains("schema_version") || !data.at("schema_version").is_number_integer() ||
        data.at("schema_version").get<int>() != CurrentSchemaVersion) {
        fail("schema_version", "expected 1");
    }
    if (!data.contains("process_id") || !data.at("process_id").is_number_unsigned()) {
        fail("process_id", "must be an unsigned integer");
    }

    ProjectLockInfo info;
    info.schemaVersion = CurrentSchemaVersion;
    info.projectGuid = requiredString(data, "project_guid");
    info.ownerId = requiredString(data, "owner_id");
    info.host = requiredString(data, "host");
    info.processId = data.at("process_id").get<std::uint64_t>();
    info.acquiredAt = requiredString(data, "acquired_at");
    info.studioVersion = requiredString(data, "studio_version");
    return info;
}

std::string ProjectLockInfo::toJson() const {
    return Json{
        {"schema_version", schemaVersion},
        {"project_guid", projectGuid},
        {"owner_id", ownerId},
        {"host", host},
        {"process_id", processId},
        {"acquired_at", acquiredAt},
        {"studio_version", studioVersion},
    }.dump(2) + "\n";
}

bool ProjectLockInfo::isStale() const {
    if (host != ProjectLock::localHostName() || processId == ProjectLock::currentProcessId()) return false;
    return !processAlive(processId);
}

ProjectLock ProjectLock::acquire(const fs::path& projectRoot, std::string studioVersion) {
    const ProjectManifest manifest = validateProject(projectRoot, false);
    const ProjectPaths paths{projectRoot};
    if (!fs::is_directory(paths.hiddenStatePath())) {
        throw ProjectLockError("project.lock: .yor directory does not exist");
    }
    if (studioVersion.empty()) throw ProjectLockError("project.lock: studio version must not be empty");

    ProjectLockInfo info;
    info.projectGuid = manifest.projectGuid();
    info.ownerId = newOwnerId();
    info.host = localHostName();
    info.processId = currentProcessId();
    info.acquiredAt = utcTimestamp();
    info.studioVersion = std::move(studioVersion);
    const fs::path path = paths.lockPath();
    writeExclusive(path, info.toJson());
    return ProjectLock(path, std::move(info));
}

ProjectLockInfo ProjectLock::inspect(const fs::path& projectRoot) {
    return ProjectLockInfo::fromJson(readText(ProjectPaths{projectRoot}.lockPath()));
}

void ProjectLock::recoverStale(const fs::path& projectRoot) {
    const ProjectManifest manifest = validateProject(projectRoot, false);
    const ProjectPaths paths{projectRoot};
    const ProjectLockInfo info = inspect(projectRoot);
    if (info.projectGuid != manifest.projectGuid()) {
        throw ProjectLockError("project.lock: project identity does not match manifest");
    }
    if (!info.isStale()) {
        throw ProjectLockError("project.lock: lock owner is still running or belongs to another host");
    }
    std::error_code error;
    if (!fs::remove(paths.lockPath(), error) && error) {
        throw ProjectLockError("project.lock: stale lock removal failed: " + error.message());
    }
}

std::string ProjectLock::localHostName() {
#ifdef _WIN32
    std::array<char, MAX_COMPUTERNAME_LENGTH + 1> buffer{};
    DWORD size = static_cast<DWORD>(buffer.size());
    if (GetComputerNameA(buffer.data(), &size) != 0) return {buffer.data(), size};
#else
    std::array<char, 256> buffer{};
    if (::gethostname(buffer.data(), buffer.size() - 1) == 0) return buffer.data();
    if (const char* environment = std::getenv("HOSTNAME")) return environment;
#endif
    return "unknown";
}

std::uint64_t ProjectLock::currentProcessId() {
#ifdef _WIN32
    return static_cast<std::uint64_t>(GetCurrentProcessId());
#else
    return static_cast<std::uint64_t>(::getpid());
#endif
}

ProjectLock::ProjectLock(fs::path path, ProjectLockInfo info)
    : path_(std::move(path)), info_(std::move(info)), ownsLock_(true) {}

ProjectLock::ProjectLock(ProjectLock&& other) noexcept
    : path_(std::move(other.path_)), info_(std::move(other.info_)), ownsLock_(other.ownsLock_) {
    other.ownsLock_ = false;
}

ProjectLock& ProjectLock::operator=(ProjectLock&& other) noexcept {
    if (this == &other) return *this;
    releaseNoexcept();
    path_ = std::move(other.path_);
    info_ = std::move(other.info_);
    ownsLock_ = other.ownsLock_;
    other.ownsLock_ = false;
    return *this;
}

ProjectLock::~ProjectLock() {
    releaseNoexcept();
}

void ProjectLock::release() {
    if (!ownsLock_) return;
    std::error_code existsError;
    if (!fs::exists(path_, existsError)) {
        if (existsError) throw ProjectLockError("project.lock: cannot inspect lock file: " + existsError.message());
        ownsLock_ = false;
        return;
    }
    const ProjectLockInfo current = ProjectLockInfo::fromJson(readText(path_));
    if (current.ownerId != info_.ownerId || current.processId != info_.processId) {
        throw ProjectLockError("project.lock: lock ownership changed before release");
    }
    std::error_code removeError;
    fs::remove(path_, removeError);
    if (removeError) throw ProjectLockError("project.lock: release failed: " + removeError.message());
    ownsLock_ = false;
}

void ProjectLock::releaseNoexcept() noexcept {
    if (!ownsLock_) return;
    try {
        release();
    } catch (...) {
        ownsLock_ = false;
    }
}

} // namespace yorstudio
