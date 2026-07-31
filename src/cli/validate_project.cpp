#include "yorstudio/project_manifest.hpp"

#include <filesystem>
#include <iostream>
#include <string_view>

int main(int argc, char** argv) {
    const std::filesystem::path projectRoot = argc > 1 ? argv[1] : "templates/empty-project";
    const bool requireLayout = argc > 2 && std::string_view(argv[2]) == "--layout";
    if (argc > 3 || (argc == 3 && !requireLayout)) {
        std::cerr << "usage: yorstudio_project_validate [project-root] [--layout]\n";
        return 2;
    }

    try {
        const auto manifest = yorstudio::validateProject(projectRoot, requireLayout);
        std::cout << "YOR project is valid: " << projectRoot.string() << " (" << manifest.name() << ")\n";
        return 0;
    } catch (const yorstudio::ProjectError& error) {
        std::cerr << "YOR project validation failed: " << error.what() << '\n';
        return 1;
    }
}
