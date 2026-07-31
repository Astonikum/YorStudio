#pragma once

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

struct StudioUiEntity {
    unsigned int index = 0;
    unsigned int generation = 0;
    unsigned int parentIndex = 0;
    unsigned int parentGeneration = 0;
    std::string name;
    StudioUiTransform transform;
    bool active = true;
    bool selected = false;
};

struct StudioUiFrame {
    std::string status;
    std::string projectName;
    std::string projectRoot;
    std::vector<StudioUiRecentProject> recentProjects;
    std::vector<StudioUiEntity> sceneEntities;
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
    renameObject,
    setTransform,
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
    bool active = true;
    StudioUiTransform transform;
};

class StudioUiPort {
public:
    virtual ~StudioUiPort() = default;

    virtual void beginFrame() = 0;
    virtual StudioUiAction draw(const StudioUiFrame& frame) = 0;
    virtual void endFrame() = 0;
};

} // namespace yorstudio
