#pragma once

#include <string>
#include <vector>

namespace yorstudio {

struct StudioUiRecentProject {
    std::string name;
    std::string root;
};

struct StudioUiFrame {
    std::string status;
    std::string projectName;
    std::string projectRoot;
    std::vector<StudioUiRecentProject> recentProjects;
    bool projectOpen = false;
    bool readOnly = false;
};

enum class StudioUiCommand {
    none,
    chooseProject,
    openRecentProject,
    newProject,
    closeProject,
    quit,
};

struct StudioUiAction {
    StudioUiCommand command = StudioUiCommand::none;
    std::string projectRoot;
    std::string projectName;
};

class StudioUiPort {
public:
    virtual ~StudioUiPort() = default;

    virtual void beginFrame() = 0;
    virtual StudioUiAction draw(const StudioUiFrame& frame) = 0;
    virtual void endFrame() = 0;
};

} // namespace yorstudio
