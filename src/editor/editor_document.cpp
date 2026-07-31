#include "yorstudio/editor/editor_document.hpp"

#include <algorithm>
#include <memory>
#include <utility>

namespace yorstudio {

namespace {

bool valid(const yorengine::Scene& scene, yorengine::EntityId entity) {
    return entity.valid() && scene.isAlive(entity);
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
    for (const auto& object : objects) {
        result.push_back({
            object.id(),
            object.name(),
            object.transform(),
            object.active(),
            selection_.contains(object.id()),
        });
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
        [created](yorengine::Scene& scene) {
            if (!scene.isAlive(*created)) return false;
            const bool destroyed = scene.destroyEntity(*created);
            if (destroyed) *created = {};
            return destroyed;
        },
        [this, created] { selection_.replace(scene_, *created); },
        [this] { selection_.prune(scene_); },
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
