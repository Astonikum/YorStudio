#include "yorstudio/studio_application.hpp"

#include <yorengine/render_snapshot.hpp>

#include <algorithm>
#include <exception>
#include <limits>
#include <utility>

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

yorengine::CameraOffsetSpace offsetSpace(StudioUiCameraOffsetSpace value) {
    return value == StudioUiCameraOffsetSpace::world
        ? yorengine::CameraOffsetSpace::World : yorengine::CameraOffsetSpace::TargetLocal;
}

StudioUiCameraOffsetSpace offsetSpace(yorengine::CameraOffsetSpace value) {
    return value == yorengine::CameraOffsetSpace::World
        ? StudioUiCameraOffsetSpace::world : StudioUiCameraOffsetSpace::targetLocal;
}

EditorCameraState camera(const StudioUiCamera& value) {
    return {value.fovYDegrees, value.aspectRatio, value.nearPlane, value.farPlane, value.channelMask};
}

StudioUiCamera camera(const EditorCameraState& value) {
    return {value.fovYDegrees, value.aspectRatio, value.nearPlane, value.farPlane, value.channelMask};
}

EditorCameraKeyPointState cameraKeyPoint(const StudioUiCameraKeyPoint& value) {
    EditorCameraKeyPointState result;
    result.priority = value.priority;
    result.enabled = value.enabled;
    result.channelMask = value.channelMask;
    result.blendDurationSeconds = value.blendDurationSeconds;
    result.lens = camera(value.lens);
    result.followTargetGuid = value.followTargetGuid;
    result.followOffset = {value.followOffset[0], value.followOffset[1], value.followOffset[2]};
    result.followOffsetSpace = offsetSpace(value.followOffsetSpace);
    result.lookAtTargetGuid = value.lookAtTargetGuid;
    result.lookAtOffset = {value.lookAtOffset[0], value.lookAtOffset[1], value.lookAtOffset[2]};
    result.lookAtOffsetSpace = offsetSpace(value.lookAtOffsetSpace);
    return result;
}

StudioUiCameraKeyPoint cameraKeyPoint(const EditorCameraKeyPointState& value) {
    StudioUiCameraKeyPoint result;
    result.priority = value.priority;
    result.enabled = value.enabled;
    result.channelMask = value.channelMask;
    result.blendDurationSeconds = value.blendDurationSeconds;
    result.lens = camera(value.lens);
    result.followTargetGuid = value.followTargetGuid;
    result.followOffset[0] = value.followOffset.x;
    result.followOffset[1] = value.followOffset.y;
    result.followOffset[2] = value.followOffset.z;
    result.followOffsetSpace = offsetSpace(value.followOffsetSpace);
    result.lookAtTargetGuid = value.lookAtTargetGuid;
    result.lookAtOffset[0] = value.lookAtOffset.x;
    result.lookAtOffset[1] = value.lookAtOffset.y;
    result.lookAtOffset[2] = value.lookAtOffset.z;
    result.lookAtOffsetSpace = offsetSpace(value.lookAtOffsetSpace);
    return result;
}

EditorCameraNoiseState cameraNoise(const StudioUiCameraNoise& value) {
    return {
        {value.positionAmplitude[0], value.positionAmplitude[1], value.positionAmplitude[2]},
        {value.rotationAmplitudeDegrees[0], value.rotationAmplitudeDegrees[1], value.rotationAmplitudeDegrees[2]},
        value.frequency,
        value.seed,
    };
}

StudioUiCameraNoise cameraNoise(const EditorCameraNoiseState& value) {
    StudioUiCameraNoise result;
    result.positionAmplitude[0] = value.positionAmplitude.x;
    result.positionAmplitude[1] = value.positionAmplitude.y;
    result.positionAmplitude[2] = value.positionAmplitude.z;
    result.rotationAmplitudeDegrees[0] = value.rotationAmplitudeDegrees.x;
    result.rotationAmplitudeDegrees[1] = value.rotationAmplitudeDegrees.y;
    result.rotationAmplitudeDegrees[2] = value.rotationAmplitudeDegrees.z;
    result.frequency = value.frequency;
    result.seed = value.seed;
    return result;
}

yorengine::Light::Kind lightKind(StudioUiLightKind value) {
    switch (value) {
    case StudioUiLightKind::directional: return yorengine::Light::Kind::Directional;
    case StudioUiLightKind::point: return yorengine::Light::Kind::Point;
    case StudioUiLightKind::spot: return yorengine::Light::Kind::Spot;
    }
    return yorengine::Light::Kind::Directional;
}

StudioUiLightKind lightKind(yorengine::Light::Kind value) {
    switch (value) {
    case yorengine::Light::Kind::Directional: return StudioUiLightKind::directional;
    case yorengine::Light::Kind::Point: return StudioUiLightKind::point;
    case yorengine::Light::Kind::Spot: return StudioUiLightKind::spot;
    }
    return StudioUiLightKind::directional;
}

EditorLightState light(const StudioUiLight& value) {
    return {
        lightKind(value.kind),
        {value.color[0], value.color[1], value.color[2]},
        value.intensity,
        value.range,
        value.innerConeDegrees,
        value.outerConeDegrees,
    };
}

StudioUiLight light(const EditorLightState& value) {
    StudioUiLight result;
    result.kind = lightKind(value.kind);
    result.color[0] = value.color.x;
    result.color[1] = value.color.y;
    result.color[2] = value.color.z;
    result.intensity = value.intensity;
    result.range = value.range;
    result.innerConeDegrees = value.innerConeDegrees;
    result.outerConeDegrees = value.outerConeDegrees;
    return result;
}

StudioUiViewportFrame viewport(const yorengine::Scene& scene, std::string sceneKey,
                               const std::vector<EditorEntityState>& editorEntities) {
    StudioUiViewportFrame result;
    result.sceneKey = std::move(sceneKey);
    const auto snapshot = scene.captureRenderSnapshot();
    result.sourceVersion = snapshot.sourceVersion();
    bool cameraFound = false;
    for (const auto& entity : snapshot.entities()) {
        if (!cameraFound && entity.camera) {
            const auto position = entity.worldMatrix.transformPoint({});
            const auto direction = (entity.worldMatrix.transformPoint({0.0f, 0.0f, 1.0f}) - position).normalized();
            result.camera.position[0] = position.x;
            result.camera.position[1] = position.y;
            result.camera.position[2] = position.z;
            result.camera.direction[0] = direction.x;
            result.camera.direction[1] = direction.y;
            result.camera.direction[2] = direction.z;
            result.camera.fovYDegrees = entity.camera->fovYDegrees;
            result.camera.farPlane = entity.camera->farPlane;
            cameraFound = true;
        }
        if (!entity.meshVertices.empty()) {
            result.entities.push_back({
                entity.entity.index,
                entity.entity.generation,
                result.vertices.size(),
                entity.meshVertices.size(),
            });
        }
        for (const auto& vertex : entity.meshVertices) {
            const auto position = entity.worldMatrix.transformPoint(vertex.position);
            StudioUiRenderVertex output;
            output.position[0] = position.x;
            output.position[1] = position.y;
            output.position[2] = position.z;
            output.color[0] = vertex.r;
            output.color[1] = vertex.g;
            output.color[2] = vertex.b;
            output.color[3] = vertex.a;
            output.uv[0] = vertex.u;
            output.uv[1] = vertex.v;
            result.vertices.push_back(output);
        }
    }
    for (const auto& editorEntity : editorEntities) {
        const auto range = std::find_if(result.entities.begin(), result.entities.end(), [&](const auto& value) {
            return value.index == editorEntity.id.index && value.generation == editorEntity.id.generation;
        });
        if (range == result.entities.end()) {
            result.entities.push_back({
                editorEntity.id.index,
                editorEntity.id.generation,
                0,
                0,
                transform(editorEntity.transform),
                editorEntity.selected,
            });
        } else {
            range->transform = transform(editorEntity.transform);
            range->selected = editorEntity.selected;
        }
    }
    return result;
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
    case StudioUiCommand::openRecentProject:
        if (!action.projectRoot.empty()) openProject(action.projectRoot);
        break;
    case StudioUiCommand::newProject:
        if (!selectedProject.empty() && !action.projectName.empty()) createProject(selectedProject, action.projectName);
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
    case StudioUiCommand::addCamera:
        if (!editor_ || !editor_->addSelectedCamera()) status_ = "Cannot add camera.";
        break;
    case StudioUiCommand::removeCamera:
        if (!editor_ || !editor_->removeSelectedCamera()) status_ = "Cannot remove camera.";
        break;
    case StudioUiCommand::setCamera:
        if (!editor_ || !action.camera || !editor_->setSelectedCamera(camera(*action.camera))) {
            status_ = "Cannot change camera.";
        }
        break;
    case StudioUiCommand::addCameraKeyPoint:
        if (!editor_ || !editor_->addSelectedCameraKeyPoint()) status_ = "Cannot add camera key point.";
        break;
    case StudioUiCommand::removeCameraKeyPoint:
        if (!editor_ || !editor_->removeSelectedCameraKeyPoint()) status_ = "Cannot remove camera key point.";
        break;
    case StudioUiCommand::setCameraKeyPoint:
        if (!editor_ || !action.cameraKeyPoint || !editor_->setSelectedCameraKeyPoint(cameraKeyPoint(*action.cameraKeyPoint))) {
            status_ = "Cannot change camera key point.";
        }
        break;
    case StudioUiCommand::addCameraNoise:
        if (!editor_ || !editor_->addSelectedCameraNoise()) status_ = "Cannot add camera noise.";
        break;
    case StudioUiCommand::removeCameraNoise:
        if (!editor_ || !editor_->removeSelectedCameraNoise()) status_ = "Cannot remove camera noise.";
        break;
    case StudioUiCommand::setCameraNoise:
        if (!editor_ || !action.cameraNoise || !editor_->setSelectedCameraNoise(cameraNoise(*action.cameraNoise))) {
            status_ = "Cannot change camera noise.";
        }
        break;
    case StudioUiCommand::addLight:
        if (!editor_ || !editor_->addSelectedLight()) status_ = "Cannot add light.";
        break;
    case StudioUiCommand::removeLight:
        if (!editor_ || !editor_->removeSelectedLight()) status_ = "Cannot remove light.";
        break;
    case StudioUiCommand::setLight:
        if (!editor_ || !action.light || !editor_->setSelectedLight(light(*action.light))) {
            status_ = "Cannot change light.";
        }
        break;
    case StudioUiCommand::addMesh:
        if (!editor_ || !editor_->addSelectedMesh()) status_ = "Cannot add mesh.";
        break;
    case StudioUiCommand::addTriangle:
        if (!editor_ || !editor_->addSelectedTriangle()) status_ = "Cannot add triangle mesh.";
        break;
    case StudioUiCommand::removeMesh:
        if (!editor_ || !editor_->removeSelectedMesh()) status_ = "Cannot remove mesh.";
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
        const auto editorEntities = editor_->entities();
        result.viewport = viewport(editor_->scene(), editor_->scenePath().string(), editorEntities);
        for (const auto& entity : editorEntities) {
            StudioUiEntity item;
            item.index = entity.id.index;
            item.generation = entity.id.generation;
            const auto parent = editor_->scene().parent(entity.id);
            if (parent.valid()) {
                item.parentIndex = parent.index;
                item.parentGeneration = parent.generation;
            }
            item.guid = entity.guid;
            item.tags = entity.tags;
            item.layer = entity.layer;
            item.name = entity.name;
            item.transform = transform(entity.transform);
            item.active = entity.active;
            item.selected = entity.selected;
            if (entity.camera) item.camera = camera(*entity.camera);
            if (entity.cameraKeyPoint) item.cameraKeyPoint = cameraKeyPoint(*entity.cameraKeyPoint);
            if (entity.cameraNoise) item.cameraNoise = cameraNoise(*entity.cameraNoise);
            if (entity.light) item.light = light(*entity.light);
            if (entity.mesh) {
                item.mesh = StudioUiMesh{static_cast<unsigned int>(std::min<std::size_t>(
                    entity.mesh->vertices.size(), (std::numeric_limits<unsigned int>::max)()))};
            }
            result.sceneEntities.push_back(std::move(item));
        }
    }
    result.recentProjects.reserve(recentProjects_.entries().size());
    for (const auto& recent : recentProjects_.entries()) {
        result.recentProjects.push_back({recent.name, recent.root.string()});
    }
    return result;
}

bool StudioApplication::recordRecent(const ProjectManifest& manifest, const std::filesystem::path& projectRoot) {
    try {
        recentProjects_.record(projectRoot, manifest);
        if (!recentProjectsPath_.empty()) {
            const auto parent = recentProjectsPath_.parent_path();
            if (!parent.empty()) std::filesystem::create_directories(parent);
            recentProjects_.writeAtomic(recentProjectsPath_);
        }
    } catch (const std::exception& error) {
        status_ = std::string("Project opened; recent registry unavailable: ") + error.what();
        return false;
    }
    return true;
}

void StudioApplication::createProject(const std::filesystem::path& parentRoot, std::string name) {
    try {
        const auto parent = std::filesystem::absolute(parentRoot).lexically_normal();
        const auto root = parent / name;
        WorkspaceRoots roots({parent});
        const ProjectManifest manifest = ProjectManifest::create(std::move(name));
        newProject(roots, root, manifest);
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
        status_ = error.what();
    }
}

} // namespace yorstudio
