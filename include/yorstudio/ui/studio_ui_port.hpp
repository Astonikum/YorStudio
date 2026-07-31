#pragma once

#include <string>

namespace yorstudio {

struct StudioUiFrame {
    std::string status;
    std::string projectName;
    std::string projectRoot;
    bool projectOpen = false;
    bool readOnly = false;
};

enum class StudioUiCommand {
    none,
    chooseProject,
    closeProject,
    quit,
};

class StudioUiPort {
public:
    virtual ~StudioUiPort() = default;

    virtual void beginFrame() = 0;
    virtual StudioUiCommand draw(const StudioUiFrame& frame) = 0;
    virtual void endFrame() = 0;
};

} // namespace yorstudio
