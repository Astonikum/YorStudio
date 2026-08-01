#include "yorstudio/studio_application.hpp"

#include <fstream>
#include <exception>
#include <cstdio>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

namespace yorstudio {

namespace {

yorengine::EntityId entityId(const StudioUiAction& action) {
    return {action.entityIndex, action.entityGeneration};
}

yorengine::EntityId parentId(const StudioUiAction& action) {
    return {action.parentIndex, action.parentGeneration};
}

yorengine::Transform transform(const StudioUiTransform& value) {
    yorengine::Transform result;
    result.position = {value.position[0], value.position[1], value.position[2]};
    result.rotation = {value.rotation[0], value.rotation[1], value.rotation[2], value.rotation[3]};
    result.scale = {value.scale[0], value.scale[1], value.scale[2]};
    return result;
}

StudioUiTransform transform(const yorengine::Transform& value) {
    StudioUiTransform result;
    result.position[0] = value.position.x;
    result.position[1] = value.position.y;
    result.position[2] = value.position.z;
    result.rotation[0] = value.rotation.x;
    result.rotation[1] = value.rotation.y;
    result.rotation[2] = value.rotation.z;
    result.rotation[3] = value.rotation.w;
    result.scale[0] = value.scale.x;
    result.scale[1] = value.scale.y;
    result.scale[2] = value.scale.z;
    return result;
}

void writeTextFile(const std::filesystem::path& path, std::string_view text) {
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    if (!stream) throw ProjectError("project: cannot create " + path.string());
    stream << text;
    if (!stream) throw ProjectError("project: cannot write " + path.string());
}

std::string fileDate(const std::filesystem::path& path, bool creation) {
#ifdef _WIN32
    WIN32_FILE_ATTRIBUTE_DATA attributes{};
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &attributes)) return {};
    const FILETIME& value = creation ? attributes.ftCreationTime : attributes.ftLastWriteTime;
    SYSTEMTIME time{};
    if (!FileTimeToSystemTime(&value, &time)) return {};
    char result[32]{};
    std::snprintf(result, sizeof(result), "%04u-%02u-%02u", time.wYear, time.wMonth, time.wDay);
    return result;
#else
    (void)path;
    (void)creation;
    return {};
#endif
}

void runGit(const std::filesystem::path& workingDirectory, std::wstring command, std::string_view operation) {
#ifdef _WIN32
    STARTUPINFOW startupInfo{.cb = sizeof(STARTUPINFOW)};
    PROCESS_INFORMATION processInfo{};
    std::vector<wchar_t> commandLine(command.begin(), command.end());
    commandLine.push_back(L'\0');
    if (!CreateProcessW(nullptr, commandLine.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr,
                        workingDirectory.c_str(), &startupInfo, &processInfo)) {
        throw ProjectError("git: cannot start " + std::string(operation));
    }
    WaitForSingleObject(processInfo.hProcess, INFINITE);
    DWORD exitCode = 1;
    GetExitCodeProcess(processInfo.hProcess, &exitCode);
    CloseHandle(processInfo.hThread);
    CloseHandle(processInfo.hProcess);
    if (exitCode != 0) throw ProjectError("git: " + std::string(operation) + " failed");
#else
    (void)workingDirectory;
    (void)command;
    throw ProjectError("git: setup is only available on the Windows desktop host");
#endif
}

void configureVersionControl(const std::filesystem::path& root, const ProjectCreationSettings& settings) {
    if (settings.initializeGit || settings.initializeGitLfs) runGit(root, L"git init", "initialization");
    if (settings.writeGitIgnore) {
        writeTextFile(root / ".gitignore",
            "# YorStudio generated and local files\n"
            "/build/\n"
            "/.yor/cache/\n"
            "/.yor/derived/\n"
            "/.yor/logs/\n"
            "/.yor/generated/\n"
            "/.yor/recovery/\n"
            "/.yor/editor/\n"
            ".vs/\n"
            ".vscode/\n"
            "*.user\n"
            "*.suo\n");
    }
    if (settings.writeGitAttributes || settings.initializeGitLfs) {
        std::string attributes =
            "* text=auto eol=lf\n"
            "*.png -text\n"
            "*.jpg -text\n"
            "*.jpeg -text\n"
            "*.webp -text\n"
            "*.ktx -text\n"
            "*.dds -text\n"
            "*.wav -text\n"
            "*.mp3 -text\n"
            "*.ogg -text\n"
            "*.fbx -text\n"
            "*.glb -text\n"
            "*.gltf -text\n"
            "*.zip -text\n"
            "*.7z -text\n"
            "*.rar -text\n";
        if (settings.initializeGitLfs) {
            for (const char* pattern : {
                     "*.png", "*.jpg", "*.jpeg", "*.webp", "*.ktx", "*.dds", "*.wav", "*.mp3", "*.ogg",
                     "*.fbx", "*.glb", "*.gltf", "*.zip", "*.7z", "*.rar"}) {
                attributes += pattern;
                attributes += " filter=lfs diff=lfs merge=lfs -text\n";
            }
        }
        writeTextFile(root / ".gitattributes", attributes);
    }
    if (settings.initializeGitLfs) runGit(root, L"git lfs install --local", "Git LFS initialization");
}

void validateCreationSettings(const ProjectCreationSettings& settings) {
    if (settings.parentDirectory.empty() || !std::filesystem::is_directory(settings.parentDirectory)) {
        throw ProjectError("project: parent directory must be an existing folder");
    }
    if (settings.name.empty() || settings.name == "." || settings.name == ".." ||
        std::filesystem::path(settings.name).filename().string() != settings.name) {
        throw ProjectError("project: name must be a single folder name");
    }
    if (settings.startupScene.empty()) throw ProjectError("project: startup scene must not be empty");
}

} // namespace

StudioApplication::StudioApplication(std::filesystem::path recentProjectsPath)
    : recentProjectsPath_(std::move(recentProjectsPath)) {
    if (recentProjectsPath_.empty()) return;
    try {
        recentProjects_ = RecentProjects::read(recentProjectsPath_);
    } catch (const std::exception& error) {
        status_ = std::string("Recent projects unavailable: ") + error.what();
    }
}

StudioApplication::~StudioApplication() {
    closeProject();
}

void StudioApplication::openProject(const std::filesystem::path& projectRoot) {
    try {
        const auto root = std::filesystem::absolute(projectRoot).lexically_normal();
        WorkspaceRoots roots({root.parent_path()});
        auto session = roots.openProject(root, ProjectAccess::readWrite);
        const ProjectManifest manifest = session.manifest();
        project_ = std::move(session);
        editor_ = std::make_unique<EditorDocument>();
        std::string sceneStatus = "Project opened.";
        try {
            editor_->load(root / manifest.startupScene());
        } catch (const EditorDocumentError& error) {
            sceneStatus = std::string("Project opened; scene unavailable: ") + error.what();
        }
        if (recordRecent(manifest, root)) status_ = std::move(sceneStatus);
    } catch (const std::exception& error) {
        status_ = error.what();
    }
}

void StudioApplication::closeProject() noexcept {
    editor_.reset();
    if (project_) project_->close();
    project_.reset();
    if (running_) status_ = "Choose a YOR project to open.";
}

void StudioApplication::handle(const StudioUiAction& action, const std::filesystem::path& selectedProject) {
    switch (action.command) {
    case StudioUiCommand::chooseProject:
        if (!selectedProject.empty()) openProject(selectedProject);
        break;
    case StudioUiCommand::chooseProjectParent:
        break;
    case StudioUiCommand::openRecentProject:
        if (!action.projectRoot.empty()) openProject(action.projectRoot);
        break;
    case StudioUiCommand::setRecentPinned:
        if (!action.projectRoot.empty() && recentProjects_.setPinned(action.projectRoot, action.active)) persistRecent();
        break;
    case StudioUiCommand::moveRecentProjectUp:
        if (!action.projectRoot.empty() && recentProjects_.move(action.projectRoot, -1)) persistRecent();
        break;
    case StudioUiCommand::moveRecentProjectDown:
        if (!action.projectRoot.empty() && recentProjects_.move(action.projectRoot, 1)) persistRecent();
        break;
    case StudioUiCommand::moveRecentProjectBefore:
        if (!action.projectRoot.empty() && !action.targetProjectRoot.empty() &&
            recentProjects_.moveBefore(action.projectRoot, action.targetProjectRoot)) {
            persistRecent();
        }
        break;
    case StudioUiCommand::removeRecentProject:
        if (!action.projectRoot.empty()) {
            recentProjects_.remove(action.projectRoot);
            persistRecent();
        }
        break;
    case StudioUiCommand::newProject:
        if (!action.projectSettings.name.empty()) {
            createProject(action.projectSettings);
        } else if (!selectedProject.empty() && !action.projectName.empty()) {
            ProjectCreationSettings legacy;
            legacy.parentDirectory = selectedProject;
            legacy.name = action.projectName;
            legacy.initializeGit = false;
            legacy.writeGitIgnore = false;
            legacy.writeGitAttributes = false;
            createProject(legacy);
        }
        break;
    case StudioUiCommand::createObject:
        if (!editor_ || !editor_->createObject(action.objectName)) status_ = "Cannot create object.";
        break;
    case StudioUiCommand::deleteObject:
        if (!editor_ || !editor_->deleteSelected()) status_ = "Cannot delete selected object.";
        break;
    case StudioUiCommand::duplicateObject:
        if (!editor_ || !editor_->duplicateSelected()) status_ = "Cannot duplicate selected object.";
        break;
    case StudioUiCommand::setParent:
        if (!editor_ || !editor_->setSelectedParent(parentId(action))) status_ = "Cannot set object parent.";
        break;
    case StudioUiCommand::clearParent:
        if (!editor_ || !editor_->setSelectedParent(std::nullopt)) status_ = "Cannot clear object parent.";
        break;
    case StudioUiCommand::setActive:
        if (!editor_ || !editor_->setSelectedActive(action.active)) status_ = "Cannot change object active state.";
        break;
    case StudioUiCommand::addTag:
        if (!editor_ || !editor_->addSelectedTag(action.tag)) status_ = "Cannot add object tag.";
        break;
    case StudioUiCommand::removeTag:
        if (!editor_ || !editor_->removeSelectedTag(action.tag)) status_ = "Cannot remove object tag.";
        break;
    case StudioUiCommand::setLayer:
        if (!editor_ || !editor_->setSelectedLayer(action.layer)) status_ = "Cannot change object layer.";
        break;
    case StudioUiCommand::selectObject:
        if (!editor_ || !editor_->select(entityId(action))) status_ = "Cannot select object.";
        break;
    case StudioUiCommand::renameObject:
        if (!editor_ || !editor_->renameSelected(action.objectName)) status_ = "Cannot rename selected object.";
        break;
    case StudioUiCommand::setTransform:
        if (!editor_ || !editor_->setSelectedTransform(transform(action.transform))) {
            status_ = "Cannot change selected object transform.";
        }
        break;
    case StudioUiCommand::undo:
        if (!editor_ || !editor_->undo()) status_ = "Nothing to undo.";
        break;
    case StudioUiCommand::redo:
        if (!editor_ || !editor_->redo()) status_ = "Nothing to redo.";
        break;
    case StudioUiCommand::saveScene:
        if (!editor_) {
            status_ = "No scene is open.";
            break;
        }
        try {
            editor_->save();
            status_ = "Scene saved.";
        } catch (const EditorDocumentError& error) {
            status_ = error.what();
        }
        break;
    case StudioUiCommand::closeProject:
        closeProject();
        break;
    case StudioUiCommand::quit:
        closeProject();
        requestQuit();
        break;
    case StudioUiCommand::none:
        break;
    }
}

StudioUiFrame StudioApplication::frame() const {
    StudioUiFrame result;
    result.status = status_;
    if (project_) {
        result.projectOpen = true;
        result.projectName = project_->manifest().name();
        result.projectRoot = project_->root().string();
        result.readOnly = project_->isReadOnly();
    }
    if (editor_) {
        result.editorOpen = true;
        result.sceneDirty = editor_->dirty();
        for (const auto& entity : editor_->entities()) {
            StudioUiEntity item;
            item.index = entity.id.index;
            item.generation = entity.id.generation;
            const auto parent = editor_->scene().parent(entity.id);
            if (parent.valid()) {
                item.parentIndex = parent.index;
                item.parentGeneration = parent.generation;
            }
            item.tags = entity.tags;
            item.layer = entity.layer;
            item.name = entity.name;
            item.transform = transform(entity.transform);
            item.active = entity.active;
            item.selected = entity.selected;
            result.sceneEntities.push_back(std::move(item));
        }
    }
    result.recentProjects.reserve(recentProjects_.entries().size());
    for (const auto& recent : recentProjects_.entries()) {
        result.recentProjects.push_back({
            recent.name,
            recent.root.string(),
            recent.pinned,
            fileDate(recent.root, true),
            fileDate(recent.root, false),
        });
    }
    return result;
}

bool StudioApplication::recordRecent(const ProjectManifest& manifest, const std::filesystem::path& projectRoot) {
    try {
        recentProjects_.record(projectRoot, manifest);
        return persistRecent();
    } catch (const std::exception& error) {
        status_ = std::string("Project opened; recent registry unavailable: ") + error.what();
        return false;
    }
}

bool StudioApplication::persistRecent() {
    if (recentProjectsPath_.empty()) return true;
    try {
        const auto parent = recentProjectsPath_.parent_path();
        if (!parent.empty()) std::filesystem::create_directories(parent);
        recentProjects_.writeAtomic(recentProjectsPath_);
        return true;
    } catch (const std::exception& error) {
        status_ = std::string("Recent projects unavailable: ") + error.what();
        return false;
    }
}

void StudioApplication::createProject(const ProjectCreationSettings& settings) {
    std::filesystem::path root;
    bool published = false;
    try {
        validateCreationSettings(settings);
        const auto parent = std::filesystem::absolute(settings.parentDirectory).lexically_normal();
        root = parent / settings.name;
        WorkspaceRoots roots({parent});
        const ProjectManifest manifest = ProjectManifest::create(
            settings.name, settings.engineVersion, {}, settings.startupScene, settings.targetPlatforms);
        newProject(roots, root, manifest);
        published = true;
        configureVersionControl(root, settings);
        auto session = roots.openProject(root, ProjectAccess::readWrite);
        const ProjectManifest actual = session.manifest();
        project_ = std::move(session);
        editor_ = std::make_unique<EditorDocument>();
        std::string sceneStatus = "Project created and opened.";
        try {
            editor_->load(root / actual.startupScene());
        } catch (const EditorDocumentError& error) {
            sceneStatus = std::string("Project created; scene unavailable: ") + error.what();
        }
        if (recordRecent(actual, root)) status_ = std::move(sceneStatus);
    } catch (const std::exception& error) {
        if (published) {
            std::error_code cleanupError;
            std::filesystem::remove_all(root, cleanupError);
        }
        status_ = error.what();
    }
}

} // namespace yorstudio
