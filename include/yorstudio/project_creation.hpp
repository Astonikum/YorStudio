#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace yorstudio {

struct ProjectCreationSettings {
    std::filesystem::path parentDirectory;
    std::string name;
    std::string engineVersion = "v0.1.0";
    std::string startupScene = "scenes/main.yorscene";
    std::vector<std::string> targetPlatforms = {"windows-x64"};
    bool initializeGit = true;
    bool writeGitIgnore = true;
    bool writeGitAttributes = true;
    bool initializeGitLfs = false;
};

} // namespace yorstudio
