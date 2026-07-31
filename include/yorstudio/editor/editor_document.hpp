#pragma once

#include <yorengine/scene.hpp>

#include <nlohmann/json.hpp>

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace yorstudio {

class EditorDocumentError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

class SelectionModel {
public:
    bool replace(const yorengine::Scene& scene, yorengine::EntityId entity);
    bool add(const yorengine::Scene& scene, yorengine::EntityId entity);
    bool toggle(const yorengine::Scene& scene, yorengine::EntityId entity);
    void clear() noexcept;
    void prune(const yorengine::Scene& scene);

    bool contains(yorengine::EntityId entity) const noexcept;
    const std::vector<yorengine::EntityId>& entities() const noexcept { return entities_; }
    std::optional<yorengine::EntityId> active() const noexcept { return active_; }

private:
    std::vector<yorengine::EntityId> entities_;
    std::optional<yorengine::EntityId> active_;
};

struct EditorCameraState {
    float fovYDegrees = 70.0f;
    float aspectRatio = 16.0f / 9.0f;
    float nearPlane = 0.05f;
    float farPlane = 512.0f;
    std::uint32_t channelMask = 1;
};

struct EditorCameraKeyPointState {
    int priority = 0;
    bool enabled = true;
    std::uint32_t channelMask = 1;
    float blendDurationSeconds = 0.0f;
    EditorCameraState lens{};
    std::optional<std::string> followTargetGuid;
    yorengine::Vec3 followOffset{};
    yorengine::CameraOffsetSpace followOffsetSpace = yorengine::CameraOffsetSpace::TargetLocal;
    std::optional<std::string> lookAtTargetGuid;
    yorengine::Vec3 lookAtOffset{};
    yorengine::CameraOffsetSpace lookAtOffsetSpace = yorengine::CameraOffsetSpace::TargetLocal;
};

struct EditorCameraNoiseState {
    yorengine::Vec3 positionAmplitude{};
    yorengine::Vec3 rotationAmplitudeDegrees{};
    float frequency = 1.0f;
    std::uint32_t seed = 0;
};

struct EditorLightState {
    yorengine::Light::Kind kind = yorengine::Light::Kind::Directional;
    yorengine::Vec3 color{1.0f, 1.0f, 1.0f};
    float intensity = 1.0f;
    float range = 10.0f;
    float innerConeDegrees = 15.0f;
    float outerConeDegrees = 45.0f;
};

struct EditorMeshState {
    std::vector<yorengine::MeshVertex> vertices;
};

struct EditorEntityState {
    yorengine::EntityId id{};
    std::string guid;
    std::string name;
    std::vector<std::string> tags;
    std::uint32_t layer = 0;
    yorengine::Transform transform{};
    bool active = true;
    bool selected = false;
    std::optional<EditorCameraState> camera;
    std::optional<EditorCameraKeyPointState> cameraKeyPoint;
    std::optional<EditorCameraNoiseState> cameraNoise;
    std::optional<EditorLightState> light;
    std::optional<EditorMeshState> mesh;
};

class EditorDocument {
public:
    EditorDocument() = default;
    EditorDocument(const EditorDocument&) = delete;
    EditorDocument& operator=(const EditorDocument&) = delete;

    yorengine::Scene& scene() noexcept { return scene_; }
    const yorengine::Scene& scene() const noexcept { return scene_; }

    std::vector<EditorEntityState> entities();
    const SelectionModel& selection() const noexcept { return selection_; }

    bool select(yorengine::EntityId entity);
    bool createObject(std::string name);
    bool deleteSelected();
    bool duplicateSelected();
    bool setSelectedParent(std::optional<yorengine::EntityId> parent);
    bool setSelectedActive(bool active);
    bool addSelectedTag(std::string tag);
    bool removeSelectedTag(std::string tag);
    bool setSelectedLayer(std::uint32_t layer);
    bool renameSelected(std::string name);
    bool setSelectedTransform(yorengine::Transform transform);
    bool addSelectedCamera();
    bool removeSelectedCamera();
    bool setSelectedCamera(EditorCameraState state);
    bool addSelectedCameraKeyPoint();
    bool removeSelectedCameraKeyPoint();
    bool setSelectedCameraKeyPoint(EditorCameraKeyPointState state);
    bool addSelectedCameraNoise();
    bool removeSelectedCameraNoise();
    bool setSelectedCameraNoise(EditorCameraNoiseState state);
    bool addSelectedLight();
    bool removeSelectedLight();
    bool setSelectedLight(EditorLightState state);
    bool addSelectedMesh();
    bool addSelectedTriangle();
    bool removeSelectedMesh();
    void load(const std::filesystem::path& path);
    void save();
    const std::filesystem::path& scenePath() const noexcept { return scenePath_; }

    bool undo();
    bool redo();
    std::size_t undoCount() const noexcept { return cursor_; }
    std::size_t redoCount() const noexcept { return history_.size() - cursor_; }
    bool dirty() const noexcept { return cursor_ != savedCursor_; }

private:
    struct ObjectSnapshot {
        std::string guid;
        std::string name;
        std::vector<std::string> tags;
        std::uint32_t layer = 0;
        yorengine::Transform transform{};
        bool active = true;
        std::optional<std::string> parentGuid;
        nlohmann::json extensions = nlohmann::json::object();
    };

    struct Command {
        std::string label;
        std::function<bool(yorengine::Scene&)> apply;
        std::function<bool(yorengine::Scene&)> undo;
        std::function<void()> afterApply;
        std::function<void()> afterUndo;
    };

    std::vector<ObjectSnapshot> snapshotSubtree(yorengine::EntityId root);
    std::optional<yorengine::EntityId> entityForGuid(const std::string& guid) const;
    bool restoreSubtree(yorengine::Scene& scene, const std::vector<ObjectSnapshot>& snapshots,
                        const std::vector<std::string>& guids, std::vector<yorengine::EntityId>& restored);
    bool commit(Command command);

    yorengine::Scene scene_;
    SelectionModel selection_;
    std::vector<Command> history_;
    std::size_t cursor_ = 0;
    std::size_t savedCursor_ = 0;
    std::filesystem::path scenePath_;
    nlohmann::json sceneExtensions_ = nlohmann::json::object();
    std::unordered_map<std::uint64_t, std::string> entityGuids_;
    std::unordered_map<std::string, nlohmann::json> objectExtensions_;
};

} // namespace yorstudio
