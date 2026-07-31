#include "yorstudio/editor/editor_document.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <random>
#include <set>
#include <sstream>
#include <memory>
#include <limits>
#include <system_error>
#include <string_view>
#include <utility>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace yorstudio {

namespace {

bool valid(const yorengine::Scene& scene, yorengine::EntityId entity) {
    return entity.valid() && scene.isAlive(entity);
}

std::uint64_t entityKey(yorengine::EntityId entity) noexcept {
    return (static_cast<std::uint64_t>(entity.index) << 32u) | entity.generation;
}

std::string newGuid() {
    std::array<std::uint8_t, 16> bytes{};
    std::random_device random;
    for (auto& byte : bytes) byte = static_cast<std::uint8_t>(random());
    bytes[6] = static_cast<std::uint8_t>((bytes[6] & 0x0Fu) | 0x40u);
    bytes[8] = static_cast<std::uint8_t>((bytes[8] & 0x3Fu) | 0x80u);

    std::ostringstream result;
    result << std::hex << std::setfill('0');
    for (std::size_t index = 0; index < bytes.size(); ++index) {
        if (index == 4 || index == 6 || index == 8 || index == 10) result << '-';
        result << std::setw(2) << static_cast<unsigned int>(bytes[index]);
    }
    return result.str();
}

bool validGuid(const std::string& value) {
    if (value.size() != 36) return false;
    for (std::size_t index = 0; index < value.size(); ++index) {
        if (index == 8 || index == 13 || index == 18 || index == 23) {
            if (value[index] != '-') return false;
        } else if (!std::isxdigit(static_cast<unsigned char>(value[index]))) {
            return false;
        }
    }
    return true;
}

float number(const nlohmann::json& value, std::string_view field) {
    if (!value.is_number()) throw EditorDocumentError(std::string("scene: ") + std::string(field) + " must be a number");
    const float result = value.get<float>();
    if (!std::isfinite(result)) throw EditorDocumentError(std::string("scene: ") + std::string(field) + " must be finite");
    return result;
}

yorengine::Vec3 vector3(const nlohmann::json& value, std::string_view field) {
    if (!value.is_array() || value.size() != 3) {
        throw EditorDocumentError(std::string("scene: ") + std::string(field) + " must have three values");
    }
    return {number(value[0], field), number(value[1], field), number(value[2], field)};
}

yorengine::Quaternion quaternion(const nlohmann::json& value, std::string_view field) {
    if (!value.is_array() || value.size() != 4) {
        throw EditorDocumentError(std::string("scene: ") + std::string(field) + " must have four values");
    }
    return {number(value[0], field), number(value[1], field), number(value[2], field), number(value[3], field)};
}

nlohmann::json vector3Json(yorengine::Vec3 value) {
    return {value.x, value.y, value.z};
}

nlohmann::json quaternionJson(yorengine::Quaternion value) {
    return {value.x, value.y, value.z, value.w};
}

struct LensValues {
    float fovYDegrees = 70.0f;
    float aspectRatio = 16.0f / 9.0f;
    float nearPlane = 0.05f;
    float farPlane = 512.0f;
};

float numberField(const nlohmann::json& value, const char* name, float fallback, std::string_view path) {
    if (!value.contains(name)) return fallback;
    return number(value.at(name), std::string(path) + "." + name);
}

LensValues lensValues(const nlohmann::json& value, std::string_view path) {
    if (!value.is_object()) throw EditorDocumentError(std::string("scene: ") + std::string(path) + " must be an object");
    return {
        numberField(value, "fov_y_degrees", 70.0f, path),
        numberField(value, "aspect_ratio", 16.0f / 9.0f, path),
        numberField(value, "near_plane", 0.05f, path),
        numberField(value, "far_plane", 512.0f, path),
    };
}

template <typename CameraLike>
void applyLens(CameraLike& target, const LensValues& lens) {
    target.setFovYDegrees(lens.fovYDegrees);
    target.setAspectRatio(lens.aspectRatio);
    if (lens.farPlane > target.nearPlane()) {
        target.setFarPlane(lens.farPlane);
        target.setNearPlane(lens.nearPlane);
    } else {
        target.setNearPlane(lens.nearPlane);
        target.setFarPlane(lens.farPlane);
    }
}

std::uint32_t uint32Field(const nlohmann::json& value, const char* name, std::uint32_t fallback,
                          std::string_view path) {
    if (!value.contains(name)) return fallback;
    const auto& field = value.at(name);
    if (!field.is_number_integer()) {
        throw EditorDocumentError(std::string("scene: ") + std::string(path) + "." + name + " must be an integer");
    }
    const auto integer = field.get<std::int64_t>();
    if (integer < 0 || static_cast<std::uint64_t>(integer) > (std::numeric_limits<std::uint32_t>::max)()) {
        throw EditorDocumentError(std::string("scene: ") + std::string(path) + "." + name + " is out of range");
    }
    return static_cast<std::uint32_t>(integer);
}

int intField(const nlohmann::json& value, const char* name, int fallback, std::string_view path) {
    if (!value.contains(name)) return fallback;
    const auto& field = value.at(name);
    if (!field.is_number_integer()) {
        throw EditorDocumentError(std::string("scene: ") + std::string(path) + "." + name + " must be an integer");
    }
    const auto integer = field.get<std::int64_t>();
    if (integer < (std::numeric_limits<int>::min)() || integer > (std::numeric_limits<int>::max)()) {
        throw EditorDocumentError(std::string("scene: ") + std::string(path) + "." + name + " is out of range");
    }
    return static_cast<int>(integer);
}

bool boolField(const nlohmann::json& value, const char* name, bool fallback, std::string_view path) {
    if (!value.contains(name)) return fallback;
    if (!value.at(name).is_boolean()) {
        throw EditorDocumentError(std::string("scene: ") + std::string(path) + "." + name + " must be a boolean");
    }
    return value.at(name).get<bool>();
}

yorengine::CameraOffsetSpace offsetSpace(const nlohmann::json& value, const char* name,
                                          yorengine::CameraOffsetSpace fallback, std::string_view path) {
    if (!value.contains(name)) return fallback;
    const auto& field = value.at(name);
    if (!field.is_string()) {
        throw EditorDocumentError(std::string("scene: ") + std::string(path) + "." + name + " must be a string");
    }
    const auto text = field.get<std::string>();
    if (text == "world") return yorengine::CameraOffsetSpace::World;
    if (text == "target_local") return yorengine::CameraOffsetSpace::TargetLocal;
    throw EditorDocumentError(std::string("scene: ") + std::string(path) + "." + name + " is invalid");
}

const nlohmann::json* componentJson(const nlohmann::json& extensions, const char* name) {
    if (!extensions.contains("components")) return nullptr;
    const auto& components = extensions.at("components");
    if (!components.is_object()) throw EditorDocumentError("scene: components must be an object");
    if (!components.contains(name)) return nullptr;
    if (!components.at(name).is_object()) {
        throw EditorDocumentError(std::string("scene: components.") + name + " must be an object");
    }
    return &components.at(name);
}

using EntityResolver = std::function<std::optional<yorengine::EntityId>(std::string_view)>;

std::optional<yorengine::EntityId> targetEntity(const nlohmann::json& value, const char* name,
                                                const EntityResolver& resolve, std::string_view path) {
    if (!value.contains(name) || value.at(name).is_null()) return std::nullopt;
    if (!value.at(name).is_string()) {
        throw EditorDocumentError(std::string("scene: ") + std::string(path) + "." + name + " must be a UUID or null");
    }
    const auto guid = value.at(name).get<std::string>();
    if (!validGuid(guid)) throw EditorDocumentError(std::string("scene: ") + std::string(path) + "." + name + " must be a UUID or null");
    const auto entity = resolve(guid);
    if (!entity) throw EditorDocumentError(std::string("scene: ") + std::string(path) + "." + name + " references a missing object");
    return entity;
}

void applyKnownComponents(yorengine::Scene& scene, yorengine::EntityId entity,
                          const nlohmann::json& extensions, const EntityResolver& resolve) {
    const auto* cameraData = componentJson(extensions, "camera");
    const auto* keyPointData = componentJson(extensions, "camera_key_point");
    const auto* noiseData = componentJson(extensions, "camera_noise");
    auto object = scene.object(entity);

    if (cameraData) {
        auto& camera = object.add<yorengine::Camera>();
        applyLens(camera, lensValues(cameraData->contains("lens") ? cameraData->at("lens") : *cameraData, "components.camera.lens"));
        camera.setChannelMask(uint32Field(*cameraData, "channel_mask", 1, "components.camera"));
    }
    if (keyPointData) {
        auto& keyPoint = object.add<yorengine::CameraKeyPoint>();
        applyLens(keyPoint, lensValues(keyPointData->value("lens", nlohmann::json::object()), "components.camera_key_point.lens"));
        keyPoint.setPriority(intField(*keyPointData, "priority", 0, "components.camera_key_point"));
        keyPoint.setEnabled(boolField(*keyPointData, "enabled", true, "components.camera_key_point"));
        keyPoint.setChannelMask(uint32Field(*keyPointData, "channel_mask", 1, "components.camera_key_point"));
        keyPoint.setBlendDurationSeconds(numberField(*keyPointData, "blend_duration_seconds", 0.0f, "components.camera_key_point"));
        if (const auto target = targetEntity(*keyPointData, "follow_target_guid", resolve, "components.camera_key_point")) {
            keyPoint.setFollowTarget(*target);
        }
        keyPoint.setFollowOffset(
            keyPointData->contains("follow_offset") ? vector3(keyPointData->at("follow_offset"), "components.camera_key_point.follow_offset") : yorengine::Vec3{},
            offsetSpace(*keyPointData, "follow_offset_space", yorengine::CameraOffsetSpace::TargetLocal, "components.camera_key_point"));
        if (const auto target = targetEntity(*keyPointData, "look_at_target_guid", resolve, "components.camera_key_point")) {
            keyPoint.setLookAtTarget(*target);
        }
        keyPoint.setLookAtOffset(
            keyPointData->contains("look_at_offset") ? vector3(keyPointData->at("look_at_offset"), "components.camera_key_point.look_at_offset") : yorengine::Vec3{},
            offsetSpace(*keyPointData, "look_at_offset_space", yorengine::CameraOffsetSpace::TargetLocal, "components.camera_key_point"));
    }
    if (noiseData) {
        auto& noise = object.add<yorengine::CameraNoise>();
        noise.setPositionAmplitude(
            noiseData->contains("position_amplitude") ? vector3(noiseData->at("position_amplitude"), "components.camera_noise.position_amplitude") : yorengine::Vec3{});
        noise.setRotationAmplitudeDegrees(
            noiseData->contains("rotation_amplitude_degrees") ? vector3(noiseData->at("rotation_amplitude_degrees"), "components.camera_noise.rotation_amplitude_degrees") : yorengine::Vec3{});
        noise.setFrequency(numberField(*noiseData, "frequency", 1.0f, "components.camera_noise"));
        noise.setSeed(uint32Field(*noiseData, "seed", 0, "components.camera_noise"));
    }
}

template <typename CameraLike>
void applyCameraState(CameraLike& camera, const EditorCameraState& state) {
    camera.setFovYDegrees(state.fovYDegrees);
    camera.setAspectRatio(state.aspectRatio);
    if (state.nearPlane < camera.nearPlane()) {
        camera.setNearPlane(state.nearPlane);
        camera.setFarPlane(state.farPlane);
    } else {
        camera.setFarPlane(state.farPlane);
        camera.setNearPlane(state.nearPlane);
    }
    camera.setChannelMask(state.channelMask);
}

void applyCameraKeyPointState(yorengine::CameraKeyPoint& keyPoint, const EditorCameraKeyPointState& state,
                              std::optional<yorengine::EntityId> followTarget,
                              std::optional<yorengine::EntityId> lookAtTarget) {
    auto lens = state.lens;
    lens.channelMask = state.channelMask;
    applyCameraState(keyPoint, lens);
    keyPoint.setPriority(state.priority);
    keyPoint.setEnabled(state.enabled);
    keyPoint.setChannelMask(state.channelMask);
    keyPoint.setBlendDurationSeconds(state.blendDurationSeconds);
    if (followTarget) keyPoint.setFollowTarget(*followTarget);
    else keyPoint.clearFollowTarget();
    keyPoint.setFollowOffset(state.followOffset, state.followOffsetSpace);
    if (lookAtTarget) keyPoint.setLookAtTarget(*lookAtTarget);
    else keyPoint.clearLookAtTarget();
    keyPoint.setLookAtOffset(state.lookAtOffset, state.lookAtOffsetSpace);
}

void applyCameraNoiseState(yorengine::CameraNoise& noise, const EditorCameraNoiseState& state) {
    noise.setPositionAmplitude(state.positionAmplitude);
    noise.setRotationAmplitudeDegrees(state.rotationAmplitudeDegrees);
    noise.setFrequency(state.frequency);
    noise.setSeed(state.seed);
}

bool sameVec3(yorengine::Vec3 left, yorengine::Vec3 right) noexcept {
    return left.x == right.x && left.y == right.y && left.z == right.z;
}

bool sameCameraState(const EditorCameraState& left, const EditorCameraState& right) noexcept {
    return left.fovYDegrees == right.fovYDegrees && left.aspectRatio == right.aspectRatio &&
        left.nearPlane == right.nearPlane && left.farPlane == right.farPlane && left.channelMask == right.channelMask;
}

bool sameCameraKeyPointState(const EditorCameraKeyPointState& left, const EditorCameraKeyPointState& right) noexcept {
    return left.priority == right.priority && left.enabled == right.enabled && left.channelMask == right.channelMask &&
        left.blendDurationSeconds == right.blendDurationSeconds && sameCameraState(left.lens, right.lens) &&
        left.followTargetGuid == right.followTargetGuid && sameVec3(left.followOffset, right.followOffset) &&
        left.followOffsetSpace == right.followOffsetSpace && left.lookAtTargetGuid == right.lookAtTargetGuid &&
        sameVec3(left.lookAtOffset, right.lookAtOffset) && left.lookAtOffsetSpace == right.lookAtOffsetSpace;
}

bool sameCameraNoiseState(const EditorCameraNoiseState& left, const EditorCameraNoiseState& right) noexcept {
    return sameVec3(left.positionAmplitude, right.positionAmplitude) &&
        sameVec3(left.rotationAmplitudeDegrees, right.rotationAmplitudeDegrees) &&
        left.frequency == right.frequency && left.seed == right.seed;
}

std::string offsetSpaceJson(yorengine::CameraOffsetSpace space) {
    return space == yorengine::CameraOffsetSpace::World ? "world" : "target_local";
}

template <typename CameraLike>
nlohmann::json lensJson(const CameraLike& camera) {
    return {
        {"fov_y_degrees", camera.fovYDegrees()},
        {"aspect_ratio", camera.aspectRatio()},
        {"near_plane", camera.nearPlane()},
        {"far_plane", camera.farPlane()},
    };
}

std::optional<std::string> targetGuid(yorengine::EntityId target, const std::function<std::optional<std::string>(yorengine::EntityId)>& resolve) {
    if (!target.valid()) return std::nullopt;
    const auto guid = resolve(target);
    if (!guid) throw EditorDocumentError("scene: camera target identity is missing");
    return guid;
}

nlohmann::json knownComponents(const yorengine::Scene& scene, yorengine::EntityId entity,
                               const std::function<std::optional<std::string>(yorengine::EntityId)>& resolve) {
    nlohmann::json result = nlohmann::json::object();
    if (const auto* camera = scene.component<yorengine::Camera>(entity)) {
        result["camera"] = {{"lens", lensJson(*camera)}, {"channel_mask", camera->channelMask()}};
    }
    if (const auto* keyPoint = scene.component<yorengine::CameraKeyPoint>(entity)) {
        nlohmann::json value = nlohmann::json::object();
        value["lens"] = lensJson(*keyPoint);
        value["priority"] = keyPoint->priority();
        value["enabled"] = keyPoint->enabled();
        value["channel_mask"] = keyPoint->channelMask();
        value["blend_duration_seconds"] = keyPoint->blendDurationSeconds();
        const auto followGuid = targetGuid(keyPoint->followTarget(), resolve);
        const auto lookAtGuid = targetGuid(keyPoint->lookAtTarget(), resolve);
        value["follow_target_guid"] = followGuid ? nlohmann::json(*followGuid) : nlohmann::json(nullptr);
        value["follow_offset"] = vector3Json(keyPoint->followOffset());
        value["follow_offset_space"] = offsetSpaceJson(keyPoint->followOffsetSpace());
        value["look_at_target_guid"] = lookAtGuid ? nlohmann::json(*lookAtGuid) : nlohmann::json(nullptr);
        value["look_at_offset"] = vector3Json(keyPoint->lookAtOffset());
        value["look_at_offset_space"] = offsetSpaceJson(keyPoint->lookAtOffsetSpace());
        result["camera_key_point"] = std::move(value);
    }
    if (const auto* noise = scene.component<yorengine::CameraNoise>(entity)) {
        result["camera_noise"] = {
            {"position_amplitude", vector3Json(noise->positionAmplitude())},
            {"rotation_amplitude_degrees", vector3Json(noise->rotationAmplitudeDegrees())},
            {"frequency", noise->frequency()},
            {"seed", noise->seed()},
        };
    }
    return result;
}

void mergeKnownJson(nlohmann::json& destination, const nlohmann::json& source) {
    if (!destination.is_object()) destination = nlohmann::json::object();
    for (const auto& [name, value] : source.items()) {
        if (destination.contains(name) && destination.at(name).is_object() && value.is_object()) {
            mergeKnownJson(destination[name], value);
        } else {
            destination[name] = value;
        }
    }
}

std::filesystem::path temporaryPath(const std::filesystem::path& destination) {
    const auto parent = destination.parent_path().empty() ? std::filesystem::current_path() : destination.parent_path();
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    std::random_device random;
    for (int attempt = 0; attempt < 32; ++attempt) {
        const auto candidate = parent / ("." + destination.filename().string() + ".tmp-" +
            std::to_string(stamp) + "-" + std::to_string(random()));
        std::error_code error;
        if (!std::filesystem::exists(candidate, error) && !error) return candidate;
    }
    throw EditorDocumentError("scene: cannot reserve temporary save path");
}

void removeNoexcept(const std::filesystem::path& path) noexcept {
    std::error_code error;
    std::filesystem::remove(path, error);
}

void publish(const std::filesystem::path& temporary, const std::filesystem::path& destination) {
#ifdef _WIN32
    if (!MoveFileExW(temporary.c_str(), destination.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        removeNoexcept(temporary);
        throw EditorDocumentError("scene: atomic replace failed");
    }
#else
    std::error_code error;
    std::filesystem::rename(temporary, destination, error);
    if (error) {
        removeNoexcept(temporary);
        throw EditorDocumentError("scene: atomic replace failed: " + error.message());
    }
#endif
}

} // namespace

bool SelectionModel::replace(const yorengine::Scene& scene, yorengine::EntityId entity) {
    if (!valid(scene, entity)) return false;
    entities_.assign(1, entity);
    active_ = entity;
    return true;
}

bool SelectionModel::add(const yorengine::Scene& scene, yorengine::EntityId entity) {
    if (!valid(scene, entity)) return false;
    if (!contains(entity)) entities_.push_back(entity);
    active_ = entity;
    return true;
}

bool SelectionModel::toggle(const yorengine::Scene& scene, yorengine::EntityId entity) {
    if (!valid(scene, entity)) return false;
    const auto found = std::find(entities_.begin(), entities_.end(), entity);
    if (found == entities_.end()) {
        entities_.push_back(entity);
        active_ = entity;
        return true;
    }

    entities_.erase(found);
    if (active_ && *active_ == entity) {
        active_ = entities_.empty() ? std::nullopt : std::optional{entities_.back()};
    }
    return true;
}

void SelectionModel::clear() noexcept {
    entities_.clear();
    active_.reset();
}

void SelectionModel::prune(const yorengine::Scene& scene) {
    entities_.erase(
        std::remove_if(entities_.begin(), entities_.end(), [&](yorengine::EntityId entity) {
            return !valid(scene, entity);
        }),
        entities_.end());
    if (active_ && !valid(scene, *active_)) {
        active_ = entities_.empty() ? std::nullopt : std::optional{entities_.back()};
    }
}

bool SelectionModel::contains(yorengine::EntityId entity) const noexcept {
    return std::find(entities_.begin(), entities_.end(), entity) != entities_.end();
}

std::vector<EditorEntityState> EditorDocument::entities() {
    std::vector<EditorEntityState> result;
    const auto objects = scene_.objects();
    result.reserve(objects.size());
    const auto guidFor = [this](yorengine::EntityId entity) -> std::optional<std::string> {
        if (!entity.valid() || !scene_.isAlive(entity)) return std::nullopt;
        const auto found = entityGuids_.find(entityKey(entity));
        return found == entityGuids_.end() ? std::nullopt : std::optional{found->second};
    };
    for (const auto& object : objects) {
        EditorEntityState state{
            object.id(),
            entityGuids_.at(entityKey(object.id())),
            object.name(),
            object.tags(),
            object.layer(),
            object.transform(),
            object.active(),
            selection_.contains(object.id()),
        };
        if (const auto* camera = object.component<yorengine::Camera>()) {
            state.camera = EditorCameraState{
                camera->fovYDegrees(), camera->aspectRatio(), camera->nearPlane(), camera->farPlane(), camera->channelMask(),
            };
        }
        if (const auto* keyPoint = object.component<yorengine::CameraKeyPoint>()) {
            state.cameraKeyPoint = EditorCameraKeyPointState{
                keyPoint->priority(), keyPoint->enabled(), keyPoint->channelMask(), keyPoint->blendDurationSeconds(),
                {keyPoint->fovYDegrees(), keyPoint->aspectRatio(), keyPoint->nearPlane(), keyPoint->farPlane(), 1},
                guidFor(keyPoint->followTarget()), keyPoint->followOffset(), keyPoint->followOffsetSpace(),
                guidFor(keyPoint->lookAtTarget()), keyPoint->lookAtOffset(), keyPoint->lookAtOffsetSpace(),
            };
        }
        if (const auto* noise = object.component<yorengine::CameraNoise>()) {
            state.cameraNoise = EditorCameraNoiseState{
                noise->positionAmplitude(), noise->rotationAmplitudeDegrees(), noise->frequency(), noise->seed(),
            };
        }
        result.push_back(std::move(state));
    }
    return result;
}

bool EditorDocument::select(yorengine::EntityId entity) {
    return selection_.replace(scene_, entity);
}

bool EditorDocument::createObject(std::string name) {
    if (name.empty()) return false;
    const auto created = std::make_shared<yorengine::EntityId>();
    auto objectName = std::make_shared<std::string>(std::move(name));
    auto guid = std::make_shared<std::string>(newGuid());
    auto removedKey = std::make_shared<std::uint64_t>(0);
    return commit({
        "Create Object",
        [created, objectName](yorengine::Scene& scene) {
            try {
                *created = scene.createObject(*objectName).id();
                return true;
            } catch (...) {
                return false;
            }
        },
        [created, removedKey](yorengine::Scene& scene) {
            if (!scene.isAlive(*created)) return false;
            *removedKey = entityKey(*created);
            const bool destroyed = scene.destroyEntity(*created);
            if (destroyed) *created = {};
            return destroyed;
        },
        [this, created, guid] {
            entityGuids_[entityKey(*created)] = *guid;
            selection_.replace(scene_, *created);
        },
        [this, removedKey] {
            entityGuids_.erase(*removedKey);
            selection_.prune(scene_);
        },
    });
}

std::optional<yorengine::EntityId> EditorDocument::entityForGuid(const std::string& guid) const {
    for (const auto entity : scene_.entities()) {
        const auto found = entityGuids_.find(entityKey(entity));
        if (found != entityGuids_.end() && found->second == guid) return entity;
    }
    return std::nullopt;
}

std::vector<EditorDocument::ObjectSnapshot> EditorDocument::snapshotSubtree(yorengine::EntityId root) {
    std::vector<ObjectSnapshot> result;
    bool complete = true;
    auto collect = [&](auto&& self, yorengine::EntityId entity) -> void {
        if (!complete || !valid(scene_, entity)) {
            complete = false;
            return;
        }
        const auto guid = entityGuids_.find(entityKey(entity));
        if (guid == entityGuids_.end()) {
            complete = false;
            return;
        }
        std::optional<std::string> parentGuid;
        const auto parent = scene_.parent(entity);
        if (valid(scene_, parent)) {
            const auto parentIdentity = entityGuids_.find(entityKey(parent));
            if (parentIdentity == entityGuids_.end()) {
                complete = false;
                return;
            }
            parentGuid = parentIdentity->second;
        }
        const auto object = scene_.object(entity);
        result.push_back({
            guid->second,
            object.name(),
            object.tags(),
            object.layer(),
            object.transform(),
            object.active(),
            std::move(parentGuid),
            objectExtensions_.contains(guid->second) ? objectExtensions_.at(guid->second) : nlohmann::json::object(),
        });
        for (const auto child : scene_.children(entity)) self(self, child);
    };
    collect(collect, root);
    return complete ? std::move(result) : std::vector<ObjectSnapshot>{};
}

bool EditorDocument::restoreSubtree(yorengine::Scene& scene, const std::vector<ObjectSnapshot>& snapshots,
                                    const std::vector<std::string>& guids,
                                    std::vector<yorengine::EntityId>& restored) {
    if (snapshots.empty() || snapshots.size() != guids.size()) return false;
    for (const auto& guid : guids) {
        if (entityForGuid(guid)) return false;
    }

    std::unordered_map<std::string, yorengine::EntityId> remapped;
    std::vector<yorengine::EntityId> created;
    try {
        created.reserve(snapshots.size());
        for (std::size_t index = 0; index < snapshots.size(); ++index) {
            const auto& snapshot = snapshots[index];
            auto object = scene.createObject(snapshot.name);
            created.push_back(object.id());
            if (!object.setTransform(snapshot.transform)) throw std::logic_error("invalid transform");
            object.setActive(snapshot.active);
            for (const auto& tag : snapshot.tags) {
                if (!object.addTag(tag)) throw std::logic_error("duplicate tag");
            }
            object.setLayer(snapshot.layer);
            entityGuids_[entityKey(object.id())] = guids[index];
            objectExtensions_[guids[index]] = snapshot.extensions;
            remapped.emplace(snapshot.guid, object.id());
        }
        for (std::size_t index = 0; index < snapshots.size(); ++index) {
            const auto& parentGuid = snapshots[index].parentGuid;
            if (!parentGuid) continue;
            const auto internalParent = remapped.find(*parentGuid);
            const auto parent = internalParent != remapped.end() ? std::optional{internalParent->second}
                                                                  : entityForGuid(*parentGuid);
            if (!parent || !scene.setParent(created[index], *parent)) {
                throw std::logic_error("invalid parent relationship");
            }
        }
        const EntityResolver resolve = [&](std::string_view guid) -> std::optional<yorengine::EntityId> {
            const auto internal = remapped.find(std::string(guid));
            if (internal != remapped.end()) return internal->second;
            return entityForGuid(std::string(guid));
        };
        for (std::size_t index = 0; index < snapshots.size(); ++index) {
            applyKnownComponents(scene, created[index], snapshots[index].extensions, resolve);
        }
    } catch (...) {
        if (!created.empty()) scene.destroyEntity(created.front());
        for (std::size_t index = 0; index < created.size(); ++index) {
            entityGuids_.erase(entityKey(created[index]));
            if (index < guids.size()) objectExtensions_.erase(guids[index]);
        }
        return false;
    }
    restored = std::move(created);
    return true;
}

bool EditorDocument::deleteSelected() {
    const auto selected = selection_.active();
    if (!selected || !valid(scene_, *selected)) return false;
    auto snapshots = std::make_shared<std::vector<ObjectSnapshot>>(snapshotSubtree(*selected));
    if (snapshots->empty()) return false;
    auto guids = std::make_shared<std::vector<std::string>>();
    for (const auto& snapshot : *snapshots) guids->push_back(snapshot.guid);
    auto root = std::make_shared<yorengine::EntityId>(*selected);
    return commit({
        "Delete Object",
        [root](yorengine::Scene& scene) {
            if (!scene.isAlive(*root)) return false;
            return scene.destroyEntity(*root);
        },
        [this, snapshots, guids, root](yorengine::Scene& scene) {
            std::vector<yorengine::EntityId> restored;
            if (!restoreSubtree(scene, *snapshots, *guids, restored)) return false;
            *root = restored.front();
            return true;
        },
        [this, snapshots] {
            for (const auto& snapshot : *snapshots) objectExtensions_.erase(snapshot.guid);
            for (auto identity = entityGuids_.begin(); identity != entityGuids_.end();) {
                const bool removed = std::any_of(snapshots->begin(), snapshots->end(),
                    [&](const ObjectSnapshot& snapshot) { return snapshot.guid == identity->second; });
                if (removed) identity = entityGuids_.erase(identity);
                else ++identity;
            }
            selection_.clear();
        },
        [this, root] { selection_.replace(scene_, *root); },
    });
}

bool EditorDocument::duplicateSelected() {
    const auto selected = selection_.active();
    if (!selected || !valid(scene_, *selected)) return false;
    auto snapshots = std::make_shared<std::vector<ObjectSnapshot>>(snapshotSubtree(*selected));
    if (snapshots->empty()) return false;
    snapshots->front().name += " Copy";
    auto guids = std::make_shared<std::vector<std::string>>();
    for (std::size_t index = 0; index < snapshots->size(); ++index) {
        std::string guid;
        do {
            guid = newGuid();
        } while (entityForGuid(guid));
        guids->push_back(std::move(guid));
    }
    auto root = std::make_shared<yorengine::EntityId>();
    return commit({
        "Duplicate Object",
        [this, snapshots, guids, root](yorengine::Scene& scene) {
            std::vector<yorengine::EntityId> restored;
            if (!restoreSubtree(scene, *snapshots, *guids, restored)) return false;
            *root = restored.front();
            return true;
        },
        [this, snapshots, guids, root](yorengine::Scene& scene) {
            if (!scene.isAlive(*root)) return false;
            const bool destroyed = scene.destroyEntity(*root);
            if (!destroyed) return false;
            for (const auto& guid : *guids) objectExtensions_.erase(guid);
            for (auto identity = entityGuids_.begin(); identity != entityGuids_.end();) {
                if (std::find(guids->begin(), guids->end(), identity->second) != guids->end()) {
                    identity = entityGuids_.erase(identity);
                } else {
                    ++identity;
                }
            }
            *root = {};
            return true;
        },
        [this, root] { selection_.replace(scene_, *root); },
        [this] { selection_.prune(scene_); },
    });
}

bool EditorDocument::setSelectedParent(std::optional<yorengine::EntityId> parent) {
    const auto selected = selection_.active();
    if (!selected || !valid(scene_, *selected)) return false;
    if (parent && (!valid(scene_, *parent) || *parent == *selected)) return false;
    const auto previousParent = scene_.parent(*selected);
    const std::optional previous = valid(scene_, previousParent) ? std::optional{previousParent} : std::nullopt;
    if (previous == parent) return true;
    return commit({
        "Set Parent",
        [selected, parent](yorengine::Scene& scene) {
            return parent ? scene.setParent(*selected, *parent) : scene.clearParent(*selected);
        },
        [selected, previous](yorengine::Scene& scene) {
            return previous ? scene.setParent(*selected, *previous) : scene.clearParent(*selected);
        },
        {},
        {},
    });
}

bool EditorDocument::setSelectedActive(bool active) {
    const auto selected = selection_.active();
    if (!selected || !valid(scene_, *selected)) return false;
    const bool previous = scene_.active(*selected);
    if (previous == active) return true;
    return commit({
        "Set Active",
        [selected, active](yorengine::Scene& scene) { return scene.setActive(*selected, active); },
        [selected, previous](yorengine::Scene& scene) { return scene.setActive(*selected, previous); },
        {},
        {},
    });
}

bool EditorDocument::addSelectedTag(std::string tag) {
    const auto selected = selection_.active();
    if (!selected || !valid(scene_, *selected) || tag.empty()) return false;
    if (scene_.hasTag(*selected, tag)) return true;
    auto value = std::make_shared<std::string>(std::move(tag));
    return commit({
        "Add Tag",
        [selected, value](yorengine::Scene& scene) { return scene.addTag(*selected, *value); },
        [selected, value](yorengine::Scene& scene) { return scene.removeTag(*selected, *value); },
        {},
        {},
    });
}

bool EditorDocument::removeSelectedTag(std::string tag) {
    const auto selected = selection_.active();
    if (!selected || !valid(scene_, *selected) || tag.empty()) return false;
    if (!scene_.hasTag(*selected, tag)) return true;
    auto value = std::make_shared<std::string>(std::move(tag));
    return commit({
        "Remove Tag",
        [selected, value](yorengine::Scene& scene) { return scene.removeTag(*selected, *value); },
        [selected, value](yorengine::Scene& scene) { return scene.addTag(*selected, *value); },
        {},
        {},
    });
}

bool EditorDocument::setSelectedLayer(std::uint32_t layer) {
    const auto selected = selection_.active();
    if (!selected || !valid(scene_, *selected) || layer > yorengine::Scene::MaxLayer) return false;
    const std::uint32_t previous = scene_.layer(*selected);
    if (previous == layer) return true;
    return commit({
        "Set Layer",
        [selected, layer](yorengine::Scene& scene) { return scene.setLayer(*selected, layer); },
        [selected, previous](yorengine::Scene& scene) { return scene.setLayer(*selected, previous); },
        {},
        {},
    });
}

bool EditorDocument::renameSelected(std::string name) {
    const auto selected = selection_.active();
    if (!selected || !scene_.isAlive(*selected) || name.empty()) return false;
    const std::string before = scene_.object(*selected).name();
    if (before == name) return true;
    const auto after = std::make_shared<std::string>(std::move(name));
    return commit({
        "Rename Object",
        [selected, after](yorengine::Scene& scene) {
            if (!scene.isAlive(*selected)) return false;
            try {
                scene.object(*selected).setName(*after);
                return true;
            } catch (...) {
                return false;
            }
        },
        [selected, before](yorengine::Scene& scene) {
            if (!scene.isAlive(*selected)) return false;
            try {
                scene.object(*selected).setName(before);
                return true;
            } catch (...) {
                return false;
            }
        },
        {},
        {},
    });
}

bool EditorDocument::setSelectedTransform(yorengine::Transform transform) {
    const auto selected = selection_.active();
    if (!selected || !scene_.isAlive(*selected)) return false;
    const yorengine::Transform before = scene_.transform(*selected);
    if (before.position.x == transform.position.x && before.position.y == transform.position.y &&
        before.position.z == transform.position.z && before.rotation.x == transform.rotation.x &&
        before.rotation.y == transform.rotation.y && before.rotation.z == transform.rotation.z &&
        before.rotation.w == transform.rotation.w && before.scale.x == transform.scale.x &&
        before.scale.y == transform.scale.y && before.scale.z == transform.scale.z) {
        return true;
    }
    return commit({
        "Set Transform",
        [selected, transform](yorengine::Scene& scene) { return scene.setTransform(*selected, transform); },
        [selected, before](yorengine::Scene& scene) { return scene.setTransform(*selected, before); },
        {},
        {},
    });
}

bool EditorDocument::addSelectedCamera() {
    const auto selected = selection_.active();
    if (!selected || !valid(scene_, *selected) || scene_.component<yorengine::Camera>(*selected)) return false;
    return commit({
        "Add Camera",
        [selected](yorengine::Scene& scene) {
            try {
                scene.object(*selected).add<yorengine::Camera>();
                return true;
            } catch (...) {
                return false;
            }
        },
        [selected](yorengine::Scene& scene) { return scene.removeComponent<yorengine::Camera>(*selected); },
        {},
        {},
    });
}

bool EditorDocument::removeSelectedCamera() {
    const auto selected = selection_.active();
    if (!selected || !valid(scene_, *selected)) return false;
    const auto* current = scene_.component<yorengine::Camera>(*selected);
    if (!current) return false;
    const EditorCameraState before{
        current->fovYDegrees(), current->aspectRatio(), current->nearPlane(), current->farPlane(), current->channelMask(),
    };
    return commit({
        "Remove Camera",
        [selected](yorengine::Scene& scene) { return scene.removeComponent<yorengine::Camera>(*selected); },
        [selected, before](yorengine::Scene& scene) {
            try {
                auto& camera = scene.object(*selected).add<yorengine::Camera>();
                applyCameraState(camera, before);
                return true;
            } catch (...) {
                return false;
            }
        },
        {},
        {},
    });
}

bool EditorDocument::setSelectedCamera(EditorCameraState state) {
    const auto selected = selection_.active();
    if (!selected || !valid(scene_, *selected)) return false;
    const auto* current = scene_.component<yorengine::Camera>(*selected);
    if (!current) return false;
    const EditorCameraState before{
        current->fovYDegrees(), current->aspectRatio(), current->nearPlane(), current->farPlane(), current->channelMask(),
    };
    if (sameCameraState(before, state)) return true;
    try {
        yorengine::Camera validation;
        applyCameraState(validation, state);
    } catch (...) {
        return false;
    }
    return commit({
        "Set Camera",
        [selected, state](yorengine::Scene& scene) {
            try {
                applyCameraState(*scene.component<yorengine::Camera>(*selected), state);
                return true;
            } catch (...) {
                return false;
            }
        },
        [selected, before](yorengine::Scene& scene) {
            try {
                applyCameraState(*scene.component<yorengine::Camera>(*selected), before);
                return true;
            } catch (...) {
                return false;
            }
        },
        {},
        {},
    });
}

bool EditorDocument::addSelectedCameraKeyPoint() {
    const auto selected = selection_.active();
    if (!selected || !valid(scene_, *selected) || scene_.component<yorengine::CameraKeyPoint>(*selected)) return false;
    return commit({
        "Add Camera Key Point",
        [selected](yorengine::Scene& scene) {
            try {
                scene.object(*selected).add<yorengine::CameraKeyPoint>();
                return true;
            } catch (...) {
                return false;
            }
        },
        [selected](yorengine::Scene& scene) { return scene.removeComponent<yorengine::CameraKeyPoint>(*selected); },
        {},
        {},
    });
}

bool EditorDocument::removeSelectedCameraKeyPoint() {
    const auto selected = selection_.active();
    if (!selected || !valid(scene_, *selected)) return false;
    const auto* current = scene_.component<yorengine::CameraKeyPoint>(*selected);
    if (!current) return false;
    const auto guidFor = [this](yorengine::EntityId entity) -> std::optional<std::string> {
        if (!valid(scene_, entity)) return std::nullopt;
        const auto found = entityGuids_.find(entityKey(entity));
        return found == entityGuids_.end() ? std::nullopt : std::optional{found->second};
    };
    const EditorCameraKeyPointState before{
        current->priority(), current->enabled(), current->channelMask(), current->blendDurationSeconds(),
        {current->fovYDegrees(), current->aspectRatio(), current->nearPlane(), current->farPlane(), 1},
        guidFor(current->followTarget()), current->followOffset(), current->followOffsetSpace(),
        guidFor(current->lookAtTarget()), current->lookAtOffset(), current->lookAtOffsetSpace(),
    };
    const auto followTarget = current->followTarget().valid()
        ? std::optional{current->followTarget()} : std::nullopt;
    const auto lookAtTarget = current->lookAtTarget().valid()
        ? std::optional{current->lookAtTarget()} : std::nullopt;
    return commit({
        "Remove Camera Key Point",
        [selected](yorengine::Scene& scene) { return scene.removeComponent<yorengine::CameraKeyPoint>(*selected); },
        [selected, before, followTarget, lookAtTarget](yorengine::Scene& scene) {
            try {
                auto& keyPoint = scene.object(*selected).add<yorengine::CameraKeyPoint>();
                applyCameraKeyPointState(keyPoint, before, followTarget, lookAtTarget);
                return true;
            } catch (...) {
                return false;
            }
        },
        {},
        {},
    });
}

bool EditorDocument::setSelectedCameraKeyPoint(EditorCameraKeyPointState state) {
    const auto selected = selection_.active();
    if (!selected || !valid(scene_, *selected)) return false;
    const auto* current = scene_.component<yorengine::CameraKeyPoint>(*selected);
    if (!current) return false;
    const auto states = entities();
    const auto stateIt = std::find_if(states.begin(), states.end(), [selected](const auto& value) {
        return value.id == *selected;
    });
    if (stateIt == states.end() || !stateIt->cameraKeyPoint) return false;
    const auto before = *stateIt->cameraKeyPoint;
    if (sameCameraKeyPointState(before, state)) return true;
    const auto resolveTarget = [this](const std::optional<std::string>& guid) -> std::optional<yorengine::EntityId> {
        if (!guid) return std::nullopt;
        return entityForGuid(*guid);
    };
    const auto followTarget = resolveTarget(state.followTargetGuid);
    const auto lookAtTarget = resolveTarget(state.lookAtTargetGuid);
    if ((state.followTargetGuid && !followTarget) || (state.lookAtTargetGuid && !lookAtTarget)) return false;
    try {
        yorengine::CameraKeyPoint validation;
        applyCameraKeyPointState(validation, state, followTarget, lookAtTarget);
    } catch (...) {
        return false;
    }
    const auto beforeFollowTarget = resolveTarget(before.followTargetGuid);
    const auto beforeLookAtTarget = resolveTarget(before.lookAtTargetGuid);
    return commit({
        "Set Camera Key Point",
        [selected, state, followTarget, lookAtTarget](yorengine::Scene& scene) {
            try {
                applyCameraKeyPointState(*scene.component<yorengine::CameraKeyPoint>(*selected), state,
                                         followTarget, lookAtTarget);
                return true;
            } catch (...) {
                return false;
            }
        },
        [selected, before, beforeFollowTarget, beforeLookAtTarget](yorengine::Scene& scene) {
            try {
                applyCameraKeyPointState(*scene.component<yorengine::CameraKeyPoint>(*selected), before,
                                         beforeFollowTarget, beforeLookAtTarget);
                return true;
            } catch (...) {
                return false;
            }
        },
        {},
        {},
    });
}

bool EditorDocument::addSelectedCameraNoise() {
    const auto selected = selection_.active();
    if (!selected || !valid(scene_, *selected) || scene_.component<yorengine::CameraNoise>(*selected)) return false;
    return commit({
        "Add Camera Noise",
        [selected](yorengine::Scene& scene) {
            try {
                scene.object(*selected).add<yorengine::CameraNoise>();
                return true;
            } catch (...) {
                return false;
            }
        },
        [selected](yorengine::Scene& scene) { return scene.removeComponent<yorengine::CameraNoise>(*selected); },
        {},
        {},
    });
}

bool EditorDocument::removeSelectedCameraNoise() {
    const auto selected = selection_.active();
    if (!selected || !valid(scene_, *selected)) return false;
    const auto* current = scene_.component<yorengine::CameraNoise>(*selected);
    if (!current) return false;
    const EditorCameraNoiseState before{
        current->positionAmplitude(), current->rotationAmplitudeDegrees(), current->frequency(), current->seed(),
    };
    return commit({
        "Remove Camera Noise",
        [selected](yorengine::Scene& scene) { return scene.removeComponent<yorengine::CameraNoise>(*selected); },
        [selected, before](yorengine::Scene& scene) {
            try {
                auto& noise = scene.object(*selected).add<yorengine::CameraNoise>();
                applyCameraNoiseState(noise, before);
                return true;
            } catch (...) {
                return false;
            }
        },
        {},
        {},
    });
}

bool EditorDocument::setSelectedCameraNoise(EditorCameraNoiseState state) {
    const auto selected = selection_.active();
    if (!selected || !valid(scene_, *selected)) return false;
    const auto* current = scene_.component<yorengine::CameraNoise>(*selected);
    if (!current) return false;
    const EditorCameraNoiseState before{
        current->positionAmplitude(), current->rotationAmplitudeDegrees(), current->frequency(), current->seed(),
    };
    if (sameCameraNoiseState(before, state)) return true;
    try {
        yorengine::CameraNoise validation;
        applyCameraNoiseState(validation, state);
    } catch (...) {
        return false;
    }
    return commit({
        "Set Camera Noise",
        [selected, state](yorengine::Scene& scene) {
            try {
                applyCameraNoiseState(*scene.component<yorengine::CameraNoise>(*selected), state);
                return true;
            } catch (...) {
                return false;
            }
        },
        [selected, before](yorengine::Scene& scene) {
            try {
                applyCameraNoiseState(*scene.component<yorengine::CameraNoise>(*selected), before);
                return true;
            } catch (...) {
                return false;
            }
        },
        {},
        {},
    });
}

void EditorDocument::load(const std::filesystem::path& path) {
    if (path.empty()) throw EditorDocumentError("scene: path must not be empty");
    std::ifstream stream(path, std::ios::binary);
    if (!stream) throw EditorDocumentError("scene: cannot open scene file");
    const std::string text((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());

    nlohmann::json data;
    try {
        data = nlohmann::json::parse(text);
    } catch (const nlohmann::json::exception& error) {
        throw EditorDocumentError(std::string("scene: invalid JSON: ") + error.what());
    }
    if (!data.is_object()) throw EditorDocumentError("scene: root must be an object");
    if (!data.contains("schema_version") || !data["schema_version"].is_number_integer() ||
        data["schema_version"].get<int>() != 1) {
        throw EditorDocumentError("scene: schema_version must be 1");
    }
    if (!data.contains("objects") || !data["objects"].is_array()) {
        throw EditorDocumentError("scene: objects must be an array");
    }

    struct Definition {
        std::string guid;
        std::string name;
        std::vector<std::string> tags;
        std::uint32_t layer = 0;
        yorengine::Transform transform;
        bool active = true;
        std::optional<std::string> parentGuid;
        nlohmann::json extensions;
    };
    std::vector<Definition> definitions;
    std::set<std::string> guids;
    std::unordered_map<std::string, std::string> parents;
    definitions.reserve(data["objects"].size());

    for (const auto& entry : data["objects"]) {
        if (!entry.is_object()) throw EditorDocumentError("scene: each object must be an object");
        if (!entry.contains("guid") || !entry["guid"].is_string() || !validGuid(entry["guid"].get<std::string>())) {
            throw EditorDocumentError("scene: object guid must be a UUID");
        }
        const std::string guid = entry["guid"].get<std::string>();
        if (!guids.insert(guid).second) throw EditorDocumentError("scene: duplicate object guid");
        if (!entry.contains("name") || !entry["name"].is_string() || entry["name"].get<std::string>().empty()) {
            throw EditorDocumentError("scene: object name must be a non-empty string");
        }

        std::vector<std::string> tags;
        if (entry.contains("tags")) {
            if (!entry["tags"].is_array()) throw EditorDocumentError("scene: tags must be an array");
            std::set<std::string> uniqueTags;
            for (const auto& tag : entry["tags"]) {
                if (!tag.is_string() || tag.get<std::string>().empty() || !uniqueTags.insert(tag.get<std::string>()).second) {
                    throw EditorDocumentError("scene: tags must contain unique non-empty strings");
                }
                tags.push_back(tag.get<std::string>());
            }
        }
        std::uint32_t layer = 0;
        if (entry.contains("layer")) {
            if (!entry["layer"].is_number_integer()) throw EditorDocumentError("scene: layer must be an integer");
            const auto value = entry["layer"].is_number_unsigned()
                ? entry["layer"].get<std::uint64_t>()
                : entry["layer"].get<std::int64_t>() < 0
                    ? std::uint64_t{yorengine::Scene::MaxLayer + 1}
                    : static_cast<std::uint64_t>(entry["layer"].get<std::int64_t>());
            if (value > yorengine::Scene::MaxLayer) {
                throw EditorDocumentError("scene: layer must be between 0 and 31");
            }
            layer = static_cast<std::uint32_t>(value);
        }

        yorengine::Transform objectTransform;
        if (entry.contains("transform")) {
            const auto& transformData = entry["transform"];
            if (!transformData.is_object()) throw EditorDocumentError("scene: transform must be an object");
            if (transformData.contains("position")) objectTransform.position = vector3(transformData["position"], "transform.position");
            if (transformData.contains("rotation")) objectTransform.rotation = quaternion(transformData["rotation"], "transform.rotation");
            if (transformData.contains("scale")) objectTransform.scale = vector3(transformData["scale"], "transform.scale");
        }
        if (entry.contains("active") && !entry["active"].is_boolean()) {
            throw EditorDocumentError("scene: active must be a boolean");
        }
        const bool active = entry.contains("active") ? entry["active"].get<bool>() : true;
        std::optional<std::string> parentGuid;
        if (entry.contains("parent_guid") && !entry["parent_guid"].is_null()) {
            if (!entry["parent_guid"].is_string() || !validGuid(entry["parent_guid"].get<std::string>())) {
                throw EditorDocumentError("scene: parent_guid must be a UUID or null");
            }
            parentGuid = entry["parent_guid"].get<std::string>();
            parents.emplace(guid, *parentGuid);
        }
        definitions.push_back({guid, entry["name"].get<std::string>(), std::move(tags), layer, objectTransform,
                               active, parentGuid, entry});
    }

    for (const auto& definition : definitions) {
        std::set<std::string> visited;
        std::string current = definition.guid;
        while (parents.contains(current)) {
            if (!visited.insert(current).second) throw EditorDocumentError("scene: parent cycle detected");
            current = parents.at(current);
            if (!guids.contains(current)) throw EditorDocumentError("scene: parent_guid references a missing object");
        }
    }

    yorengine::Scene validationScene;
    std::unordered_map<std::string, yorengine::EntityId> validationEntities;
    for (const auto& definition : definitions) {
        auto object = validationScene.createObject(definition.name);
        if (!object.setTransform(definition.transform)) throw EditorDocumentError("scene: invalid object transform");
        object.setActive(definition.active);
        for (const auto& tag : definition.tags) object.addTag(tag);
        object.setLayer(definition.layer);
        validationEntities.emplace(definition.guid, object.id());
    }
    for (const auto& definition : definitions) {
        if (!definition.parentGuid) continue;
        if (!validationScene.setParent(validationEntities.at(definition.guid), validationEntities.at(*definition.parentGuid))) {
            throw EditorDocumentError("scene: invalid parent relationship");
        }
    }
    const EntityResolver validationResolve = [&](std::string_view guid) -> std::optional<yorengine::EntityId> {
        const auto found = validationEntities.find(std::string(guid));
        return found == validationEntities.end() ? std::nullopt : std::optional{found->second};
    };
    try {
        for (const auto& definition : definitions) {
            applyKnownComponents(validationScene, validationEntities.at(definition.guid), definition.extensions, validationResolve);
        }
    } catch (const EditorDocumentError&) {
        throw;
    } catch (const std::exception& error) {
        throw EditorDocumentError(std::string("scene: invalid camera component: ") + error.what());
    }

    std::error_code pathError;
    const auto absolute = std::filesystem::absolute(path, pathError);
    if (pathError) throw EditorDocumentError("scene: cannot resolve path: " + pathError.message());

    for (const auto entity : scene_.entities()) scene_.destroyEntity(entity);
    selection_.clear();
    history_.clear();
    cursor_ = 0;
    savedCursor_ = 0;
    entityGuids_.clear();
    objectExtensions_.clear();

    std::unordered_map<std::string, yorengine::EntityId> entitiesByGuid;
    for (const auto& definition : definitions) {
        auto object = scene_.createObject(definition.name);
        if (!object.setTransform(definition.transform)) throw EditorDocumentError("scene: invalid object transform");
        object.setActive(definition.active);
        for (const auto& tag : definition.tags) object.addTag(tag);
        object.setLayer(definition.layer);
        entitiesByGuid.emplace(definition.guid, object.id());
        entityGuids_.emplace(entityKey(object.id()), definition.guid);
        objectExtensions_.emplace(definition.guid, definition.extensions);
    }
    for (const auto& definition : definitions) {
        if (!definition.parentGuid) continue;
        if (!scene_.setParent(entitiesByGuid.at(definition.guid), entitiesByGuid.at(*definition.parentGuid))) {
            throw EditorDocumentError("scene: invalid parent relationship");
        }
    }
    const EntityResolver realResolve = [&](std::string_view guid) -> std::optional<yorengine::EntityId> {
        const auto found = entitiesByGuid.find(std::string(guid));
        return found == entitiesByGuid.end() ? std::nullopt : std::optional{found->second};
    };
    for (const auto& definition : definitions) {
        applyKnownComponents(scene_, entitiesByGuid.at(definition.guid), definition.extensions, realResolve);
    }

    scenePath_ = absolute.lexically_normal();
    sceneExtensions_ = std::move(data);
}

void EditorDocument::save() {
    if (scenePath_.empty()) throw EditorDocumentError("scene: no scene path is open");
    const auto parent = scenePath_.parent_path().empty() ? std::filesystem::current_path() : scenePath_.parent_path();
    if (!std::filesystem::is_directory(parent)) throw EditorDocumentError("scene: parent directory does not exist");

    nlohmann::json data = sceneExtensions_.is_object() ? sceneExtensions_ : nlohmann::json::object();
    data["schema_version"] = 1;
    data["objects"] = nlohmann::json::array();
    const auto guidFor = [this](yorengine::EntityId entity) -> std::optional<std::string> {
        if (!entity.valid() || !scene_.isAlive(entity)) return std::nullopt;
        const auto found = entityGuids_.find(entityKey(entity));
        return found == entityGuids_.end() ? std::nullopt : std::optional{found->second};
    };
    for (const auto& state : entities()) {
        nlohmann::json object = objectExtensions_.contains(state.guid)
            ? objectExtensions_.at(state.guid)
            : nlohmann::json::object();
        object["guid"] = state.guid;
        object["name"] = state.name;
        object["tags"] = state.tags;
        object["layer"] = state.layer;
        object["active"] = state.active;
        object["transform"] = {
            {"position", vector3Json(state.transform.position)},
            {"rotation", quaternionJson(state.transform.rotation)},
            {"scale", vector3Json(state.transform.scale)},
        };
        nlohmann::json components = object.value("components", nlohmann::json::object());
        if (!components.is_object()) throw EditorDocumentError("scene: components must be an object");
        mergeKnownJson(components, knownComponents(scene_, state.id, guidFor));
        if (!state.camera) components.erase("camera");
        if (!state.cameraKeyPoint) components.erase("camera_key_point");
        if (!state.cameraNoise) components.erase("camera_noise");
        if (components.empty()) object.erase("components");
        else object["components"] = std::move(components);
        const auto parentEntity = scene_.parent(state.id);
        if (parentEntity.valid()) {
            const auto parentGuid = entityGuids_.find(entityKey(parentEntity));
            if (parentGuid == entityGuids_.end()) throw EditorDocumentError("scene: parent identity is missing");
            object["parent_guid"] = parentGuid->second;
        } else {
            object["parent_guid"] = nullptr;
        }
        data["objects"].push_back(std::move(object));
    }

    const auto temporary = temporaryPath(scenePath_);
    try {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output) throw EditorDocumentError("scene: cannot create temporary scene file");
        output << data.dump(2) << '\n';
        output.flush();
        if (!output) throw EditorDocumentError("scene: cannot write scene file");
        output.close();
        publish(temporary, scenePath_);
    } catch (...) {
        removeNoexcept(temporary);
        throw;
    }
    sceneExtensions_ = std::move(data);
    savedCursor_ = cursor_;
}

bool EditorDocument::commit(Command command) {
    if (!command.apply || !command.undo || !command.apply(scene_)) return false;
    if (cursor_ < history_.size()) history_.resize(cursor_);
    history_.push_back(std::move(command));
    ++cursor_;
    if (history_.back().afterApply) history_.back().afterApply();
    return true;
}

bool EditorDocument::undo() {
    if (cursor_ == 0) return false;
    Command& command = history_[cursor_ - 1];
    if (!command.undo(scene_)) return false;
    --cursor_;
    if (command.afterUndo) command.afterUndo();
    return true;
}

bool EditorDocument::redo() {
    if (cursor_ == history_.size()) return false;
    Command& command = history_[cursor_];
    if (!command.apply(scene_)) return false;
    ++cursor_;
    if (command.afterApply) command.afterApply();
    return true;
}

} // namespace yorstudio
