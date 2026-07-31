#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace yorstudio {

struct StudioUiRecentProject {
    std::string name;
    std::string root;
};

struct StudioUiTransform {
    float position[3] = {};
    float rotation[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    float scale[3] = {1.0f, 1.0f, 1.0f};
};

struct StudioUiCamera {
    float fovYDegrees = 70.0f;
    float aspectRatio = 16.0f / 9.0f;
    float nearPlane = 0.05f;
    float farPlane = 512.0f;
    unsigned int channelMask = 1;
};

enum class StudioUiCameraOffsetSpace {
    world,
    targetLocal,
};

struct StudioUiCameraKeyPoint {
    int priority = 0;
    bool enabled = true;
    unsigned int channelMask = 1;
    float blendDurationSeconds = 0.0f;
    StudioUiCamera lens;
    std::optional<std::string> followTargetGuid;
    float followOffset[3] = {};
    StudioUiCameraOffsetSpace followOffsetSpace = StudioUiCameraOffsetSpace::targetLocal;
    std::optional<std::string> lookAtTargetGuid;
    float lookAtOffset[3] = {};
    StudioUiCameraOffsetSpace lookAtOffsetSpace = StudioUiCameraOffsetSpace::targetLocal;
};

struct StudioUiCameraNoise {
    float positionAmplitude[3] = {};
    float rotationAmplitudeDegrees[3] = {};
    float frequency = 1.0f;
    unsigned int seed = 0;
};

enum class StudioUiLightKind {
    directional,
    point,
    spot,
};

struct StudioUiLight {
    StudioUiLightKind kind = StudioUiLightKind::directional;
    float color[3] = {1.0f, 1.0f, 1.0f};
    float intensity = 1.0f;
    float range = 10.0f;
    float innerConeDegrees = 15.0f;
    float outerConeDegrees = 45.0f;
};

struct StudioUiRenderVertex {
    float position[3] = {};
    float color[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    float uv[2] = {};
};

struct StudioUiViewportCamera {
    float position[3] = {0.0f, 0.0f, -5.0f};
    float direction[3] = {0.0f, 0.0f, 1.0f};
    float fovYDegrees = 70.0f;
    float farPlane = 512.0f;
};

struct StudioUiViewportFrame {
    std::uint64_t sourceVersion = 0;
    std::vector<StudioUiRenderVertex> vertices;
    StudioUiViewportCamera camera;
};

struct StudioUiEntity {
    unsigned int index = 0;
    unsigned int generation = 0;
    unsigned int parentIndex = 0;
    unsigned int parentGeneration = 0;
    std::string guid;
    std::vector<std::string> tags;
    unsigned int layer = 0;
    std::string name;
    StudioUiTransform transform;
    bool active = true;
    bool selected = false;
    std::optional<StudioUiCamera> camera;
    std::optional<StudioUiCameraKeyPoint> cameraKeyPoint;
    std::optional<StudioUiCameraNoise> cameraNoise;
    std::optional<StudioUiLight> light;
};

struct StudioUiFrame {
    std::string status;
    std::string projectName;
    std::string projectRoot;
    std::vector<StudioUiRecentProject> recentProjects;
    std::vector<StudioUiEntity> sceneEntities;
    StudioUiViewportFrame viewport;
    bool editorOpen = false;
    bool sceneDirty = false;
    bool projectOpen = false;
    bool readOnly = false;
};

enum class StudioUiCommand {
    none,
    chooseProject,
    openRecentProject,
    newProject,
    createObject,
    selectObject,
    deleteObject,
    duplicateObject,
    setParent,
    clearParent,
    setActive,
    addTag,
    removeTag,
    setLayer,
    renameObject,
    setTransform,
    addCamera,
    removeCamera,
    setCamera,
    addCameraKeyPoint,
    removeCameraKeyPoint,
    setCameraKeyPoint,
    addCameraNoise,
    removeCameraNoise,
    setCameraNoise,
    addLight,
    removeLight,
    setLight,
    undo,
    redo,
    saveScene,
    closeProject,
    quit,
};

struct StudioUiAction {
    StudioUiCommand command = StudioUiCommand::none;
    std::string projectRoot;
    std::string projectName;
    std::string objectName;
    unsigned int entityIndex = 0;
    unsigned int entityGeneration = 0;
    unsigned int parentIndex = 0;
    unsigned int parentGeneration = 0;
    unsigned int layer = 0;
    std::string tag;
    bool active = true;
    StudioUiTransform transform;
    std::optional<StudioUiCamera> camera;
    std::optional<StudioUiCameraKeyPoint> cameraKeyPoint;
    std::optional<StudioUiCameraNoise> cameraNoise;
    std::optional<StudioUiLight> light;
};

class StudioUiPort {
public:
    virtual ~StudioUiPort() = default;

    virtual void beginFrame() = 0;
    virtual StudioUiAction draw(const StudioUiFrame& frame) = 0;
    virtual void endFrame() = 0;
};

} // namespace yorstudio
