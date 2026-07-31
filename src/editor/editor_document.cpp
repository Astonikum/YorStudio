#include "yorstudio/editor/editor_document.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <random>
#include <set>
#include <sstream>
#include <memory>
#include <system_error>
#include <utility>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace yorstudio {

namespace {

bool valid(const yorengine::Scene& scene, yorengine::EntityId entity) {
    return entity.valid() && scene.isAlive(entity);
}

std::uint64_t entityKey(yorengine::EntityId entity) noexcept {
    return (static_cast<std::uint64_t>(entity.index) << 32u) | entity.generation;
}

std::string newGuid() {
    std::array<std::uint8_t, 16> bytes{};
    std::random_device random;
    for (auto& byte : bytes) byte = static_cast<std::uint8_t>(random());
    bytes[6] = static_cast<std::uint8_t>((bytes[6] & 0x0Fu) | 0x40u);
    bytes[8] = static_cast<std::uint8_t>((bytes[8] & 0x3Fu) | 0x80u);

    std::ostringstream result;
    result << std::hex << std::setfill('0');
    for (std::size_t index = 0; index < bytes.size(); ++index) {
        if (index == 4 || index == 6 || index == 8 || index == 10) result << '-';
        result << std::setw(2) << static_cast<unsigned int>(bytes[index]);
    }
    return result.str();
}

bool validGuid(const std::string& value) {
    if (value.size() != 36) return false;
    for (std::size_t index = 0; index < value.size(); ++index) {
        if (index == 8 || index == 13 || index == 18 || index == 23) {
            if (value[index] != '-') return false;
        } else if (!std::isxdigit(static_cast<unsigned char>(value[index]))) {
            return false;
        }
    }
    return true;
}

float number(const nlohmann::json& value, const char* field) {
    if (!value.is_number()) throw EditorDocumentError(std::string("scene: ") + field + " must be a number");
    const float result = value.get<float>();
    if (!std::isfinite(result)) throw EditorDocumentError(std::string("scene: ") + field + " must be finite");
    return result;
}

yorengine::Vec3 vector3(const nlohmann::json& value, const char* field) {
    if (!value.is_array() || value.size() != 3) {
        throw EditorDocumentError(std::string("scene: ") + field + " must have three values");
    }
    return {number(value[0], field), number(value[1], field), number(value[2], field)};
}

yorengine::Quaternion quaternion(const nlohmann::json& value, const char* field) {
    if (!value.is_array() || value.size() != 4) {
        throw EditorDocumentError(std::string("scene: ") + field + " must have four values");
    }
    return {number(value[0], field), number(value[1], field), number(value[2], field), number(value[3], field)};
}

nlohmann::json vector3Json(yorengine::Vec3 value) {
    return {value.x, value.y, value.z};
}

nlohmann::json quaternionJson(yorengine::Quaternion value) {
    return {value.x, value.y, value.z, value.w};
}

std::filesystem::path temporaryPath(const std::filesystem::path& destination) {
    const auto parent = destination.parent_path().empty() ? std::filesystem::current_path() : destination.parent_path();
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    std::random_device random;
    for (int attempt = 0; attempt < 32; ++attempt) {
        const auto candidate = parent / ("." + destination.filename().string() + ".tmp-" +
            std::to_string(stamp) + "-" + std::to_string(random()));
        std::error_code error;
        if (!std::filesystem::exists(candidate, error) && !error) return candidate;
    }
    throw EditorDocumentError("scene: cannot reserve temporary save path");
}

void removeNoexcept(const std::filesystem::path& path) noexcept {
    std::error_code error;
    std::filesystem::remove(path, error);
}

void publish(const std::filesystem::path& temporary, const std::filesystem::path& destination) {
#ifdef _WIN32
    if (!MoveFileExW(temporary.c_str(), destination.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        removeNoexcept(temporary);
        throw EditorDocumentError("scene: atomic replace failed");
    }
#else
    std::error_code error;
    std::filesystem::rename(temporary, destination, error);
    if (error) {
        removeNoexcept(temporary);
        throw EditorDocumentError("scene: atomic replace failed: " + error.message());
    }
#endif
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
            entityGuids_.at(entityKey(object.id())),
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
    auto guid = std::make_shared<std::string>(newGuid());
    auto removedKey = std::make_shared<std::uint64_t>(0);
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
        [created, removedKey](yorengine::Scene& scene) {
            if (!scene.isAlive(*created)) return false;
            *removedKey = entityKey(*created);
            const bool destroyed = scene.destroyEntity(*created);
            if (destroyed) *created = {};
            return destroyed;
        },
        [this, created, guid] {
            entityGuids_[entityKey(*created)] = *guid;
            selection_.replace(scene_, *created);
        },
        [this, removedKey] {
            entityGuids_.erase(*removedKey);
            selection_.prune(scene_);
        },
    });
}

std::optional<yorengine::EntityId> EditorDocument::entityForGuid(const std::string& guid) const {
    for (const auto entity : scene_.entities()) {
        const auto found = entityGuids_.find(entityKey(entity));
        if (found != entityGuids_.end() && found->second == guid) return entity;
    }
    return std::nullopt;
}

std::vector<EditorDocument::ObjectSnapshot> EditorDocument::snapshotSubtree(yorengine::EntityId root) {
    std::vector<ObjectSnapshot> result;
    bool complete = true;
    auto collect = [&](auto&& self, yorengine::EntityId entity) -> void {
        if (!complete || !valid(scene_, entity)) {
            complete = false;
            return;
        }
        const auto guid = entityGuids_.find(entityKey(entity));
        if (guid == entityGuids_.end()) {
            complete = false;
            return;
        }
        std::optional<std::string> parentGuid;
        const auto parent = scene_.parent(entity);
        if (valid(scene_, parent)) {
            const auto parentIdentity = entityGuids_.find(entityKey(parent));
            if (parentIdentity == entityGuids_.end()) {
                complete = false;
                return;
            }
            parentGuid = parentIdentity->second;
        }
        const auto object = scene_.object(entity);
        result.push_back({
            guid->second,
            object.name(),
            object.transform(),
            object.active(),
            std::move(parentGuid),
            objectExtensions_.contains(guid->second) ? objectExtensions_.at(guid->second) : nlohmann::json::object(),
        });
        for (const auto child : scene_.children(entity)) self(self, child);
    };
    collect(collect, root);
    return complete ? std::move(result) : std::vector<ObjectSnapshot>{};
}

bool EditorDocument::restoreSubtree(yorengine::Scene& scene, const std::vector<ObjectSnapshot>& snapshots,
                                    const std::vector<std::string>& guids,
                                    std::vector<yorengine::EntityId>& restored) {
    if (snapshots.empty() || snapshots.size() != guids.size()) return false;
    for (const auto& guid : guids) {
        if (entityForGuid(guid)) return false;
    }

    std::unordered_map<std::string, yorengine::EntityId> remapped;
    std::vector<yorengine::EntityId> created;
    try {
        created.reserve(snapshots.size());
        for (std::size_t index = 0; index < snapshots.size(); ++index) {
            const auto& snapshot = snapshots[index];
            auto object = scene.createObject(snapshot.name);
            created.push_back(object.id());
            if (!object.setTransform(snapshot.transform)) throw std::logic_error("invalid transform");
            object.setActive(snapshot.active);
            entityGuids_[entityKey(object.id())] = guids[index];
            objectExtensions_[guids[index]] = snapshot.extensions;
            remapped.emplace(snapshot.guid, object.id());
        }
        for (std::size_t index = 0; index < snapshots.size(); ++index) {
            const auto& parentGuid = snapshots[index].parentGuid;
            if (!parentGuid) continue;
            const auto internalParent = remapped.find(*parentGuid);
            const auto parent = internalParent != remapped.end() ? std::optional{internalParent->second}
                                                                  : entityForGuid(*parentGuid);
            if (!parent || !scene.setParent(created[index], *parent)) {
                throw std::logic_error("invalid parent relationship");
            }
        }
    } catch (...) {
        if (!created.empty()) scene.destroyEntity(created.front());
        for (std::size_t index = 0; index < created.size(); ++index) {
            entityGuids_.erase(entityKey(created[index]));
            if (index < guids.size()) objectExtensions_.erase(guids[index]);
        }
        return false;
    }
    restored = std::move(created);
    return true;
}

bool EditorDocument::deleteSelected() {
    const auto selected = selection_.active();
    if (!selected || !valid(scene_, *selected)) return false;
    auto snapshots = std::make_shared<std::vector<ObjectSnapshot>>(snapshotSubtree(*selected));
    if (snapshots->empty()) return false;
    auto guids = std::make_shared<std::vector<std::string>>();
    for (const auto& snapshot : *snapshots) guids->push_back(snapshot.guid);
    auto root = std::make_shared<yorengine::EntityId>(*selected);
    return commit({
        "Delete Object",
        [root](yorengine::Scene& scene) {
            if (!scene.isAlive(*root)) return false;
            return scene.destroyEntity(*root);
        },
        [this, snapshots, guids, root](yorengine::Scene& scene) {
            std::vector<yorengine::EntityId> restored;
            if (!restoreSubtree(scene, *snapshots, *guids, restored)) return false;
            *root = restored.front();
            return true;
        },
        [this, snapshots] {
            for (const auto& snapshot : *snapshots) objectExtensions_.erase(snapshot.guid);
            for (auto identity = entityGuids_.begin(); identity != entityGuids_.end();) {
                const bool removed = std::any_of(snapshots->begin(), snapshots->end(),
                    [&](const ObjectSnapshot& snapshot) { return snapshot.guid == identity->second; });
                if (removed) identity = entityGuids_.erase(identity);
                else ++identity;
            }
            selection_.clear();
        },
        [this, root] { selection_.replace(scene_, *root); },
    });
}

bool EditorDocument::duplicateSelected() {
    const auto selected = selection_.active();
    if (!selected || !valid(scene_, *selected)) return false;
    auto snapshots = std::make_shared<std::vector<ObjectSnapshot>>(snapshotSubtree(*selected));
    if (snapshots->empty()) return false;
    snapshots->front().name += " Copy";
    auto guids = std::make_shared<std::vector<std::string>>();
    for (std::size_t index = 0; index < snapshots->size(); ++index) {
        std::string guid;
        do {
            guid = newGuid();
        } while (entityForGuid(guid));
        guids->push_back(std::move(guid));
    }
    auto root = std::make_shared<yorengine::EntityId>();
    return commit({
        "Duplicate Object",
        [this, snapshots, guids, root](yorengine::Scene& scene) {
            std::vector<yorengine::EntityId> restored;
            if (!restoreSubtree(scene, *snapshots, *guids, restored)) return false;
            *root = restored.front();
            return true;
        },
        [this, snapshots, guids, root](yorengine::Scene& scene) {
            if (!scene.isAlive(*root)) return false;
            const bool destroyed = scene.destroyEntity(*root);
            if (!destroyed) return false;
            for (const auto& guid : *guids) objectExtensions_.erase(guid);
            for (auto identity = entityGuids_.begin(); identity != entityGuids_.end();) {
                if (std::find(guids->begin(), guids->end(), identity->second) != guids->end()) {
                    identity = entityGuids_.erase(identity);
                } else {
                    ++identity;
                }
            }
            *root = {};
            return true;
        },
        [this, root] { selection_.replace(scene_, *root); },
        [this] { selection_.prune(scene_); },
    });
}

bool EditorDocument::setSelectedParent(std::optional<yorengine::EntityId> parent) {
    const auto selected = selection_.active();
    if (!selected || !valid(scene_, *selected)) return false;
    if (parent && (!valid(scene_, *parent) || *parent == *selected)) return false;
    const auto previousParent = scene_.parent(*selected);
    const std::optional previous = valid(scene_, previousParent) ? std::optional{previousParent} : std::nullopt;
    if (previous == parent) return true;
    return commit({
        "Set Parent",
        [selected, parent](yorengine::Scene& scene) {
            return parent ? scene.setParent(*selected, *parent) : scene.clearParent(*selected);
        },
        [selected, previous](yorengine::Scene& scene) {
            return previous ? scene.setParent(*selected, *previous) : scene.clearParent(*selected);
        },
        {},
        {},
    });
}

bool EditorDocument::setSelectedActive(bool active) {
    const auto selected = selection_.active();
    if (!selected || !valid(scene_, *selected)) return false;
    const bool previous = scene_.active(*selected);
    if (previous == active) return true;
    return commit({
        "Set Active",
        [selected, active](yorengine::Scene& scene) { return scene.setActive(*selected, active); },
        [selected, previous](yorengine::Scene& scene) { return scene.setActive(*selected, previous); },
        {},
        {},
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

void EditorDocument::load(const std::filesystem::path& path) {
    if (path.empty()) throw EditorDocumentError("scene: path must not be empty");
    std::ifstream stream(path, std::ios::binary);
    if (!stream) throw EditorDocumentError("scene: cannot open scene file");
    const std::string text((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());

    nlohmann::json data;
    try {
        data = nlohmann::json::parse(text);
    } catch (const nlohmann::json::exception& error) {
        throw EditorDocumentError(std::string("scene: invalid JSON: ") + error.what());
    }
    if (!data.is_object()) throw EditorDocumentError("scene: root must be an object");
    if (!data.contains("schema_version") || !data["schema_version"].is_number_integer() ||
        data["schema_version"].get<int>() != 1) {
        throw EditorDocumentError("scene: schema_version must be 1");
    }
    if (!data.contains("objects") || !data["objects"].is_array()) {
        throw EditorDocumentError("scene: objects must be an array");
    }

    struct Definition {
        std::string guid;
        std::string name;
        yorengine::Transform transform;
        bool active = true;
        std::optional<std::string> parentGuid;
        nlohmann::json extensions;
    };
    std::vector<Definition> definitions;
    std::set<std::string> guids;
    std::unordered_map<std::string, std::string> parents;
    definitions.reserve(data["objects"].size());

    for (const auto& entry : data["objects"]) {
        if (!entry.is_object()) throw EditorDocumentError("scene: each object must be an object");
        if (!entry.contains("guid") || !entry["guid"].is_string() || !validGuid(entry["guid"].get<std::string>())) {
            throw EditorDocumentError("scene: object guid must be a UUID");
        }
        const std::string guid = entry["guid"].get<std::string>();
        if (!guids.insert(guid).second) throw EditorDocumentError("scene: duplicate object guid");
        if (!entry.contains("name") || !entry["name"].is_string() || entry["name"].get<std::string>().empty()) {
            throw EditorDocumentError("scene: object name must be a non-empty string");
        }

        yorengine::Transform objectTransform;
        if (entry.contains("transform")) {
            const auto& transformData = entry["transform"];
            if (!transformData.is_object()) throw EditorDocumentError("scene: transform must be an object");
            if (transformData.contains("position")) objectTransform.position = vector3(transformData["position"], "transform.position");
            if (transformData.contains("rotation")) objectTransform.rotation = quaternion(transformData["rotation"], "transform.rotation");
            if (transformData.contains("scale")) objectTransform.scale = vector3(transformData["scale"], "transform.scale");
        }
        if (entry.contains("active") && !entry["active"].is_boolean()) {
            throw EditorDocumentError("scene: active must be a boolean");
        }
        const bool active = entry.contains("active") ? entry["active"].get<bool>() : true;
        std::optional<std::string> parentGuid;
        if (entry.contains("parent_guid") && !entry["parent_guid"].is_null()) {
            if (!entry["parent_guid"].is_string() || !validGuid(entry["parent_guid"].get<std::string>())) {
                throw EditorDocumentError("scene: parent_guid must be a UUID or null");
            }
            parentGuid = entry["parent_guid"].get<std::string>();
            parents.emplace(guid, *parentGuid);
        }
        definitions.push_back({guid, entry["name"].get<std::string>(), objectTransform, active, parentGuid, entry});
    }

    for (const auto& definition : definitions) {
        std::set<std::string> visited;
        std::string current = definition.guid;
        while (parents.contains(current)) {
            if (!visited.insert(current).second) throw EditorDocumentError("scene: parent cycle detected");
            current = parents.at(current);
            if (!guids.contains(current)) throw EditorDocumentError("scene: parent_guid references a missing object");
        }
    }

    std::error_code pathError;
    const auto absolute = std::filesystem::absolute(path, pathError);
    if (pathError) throw EditorDocumentError("scene: cannot resolve path: " + pathError.message());

    for (const auto entity : scene_.entities()) scene_.destroyEntity(entity);
    selection_.clear();
    history_.clear();
    cursor_ = 0;
    savedCursor_ = 0;
    entityGuids_.clear();
    objectExtensions_.clear();

    std::unordered_map<std::string, yorengine::EntityId> entitiesByGuid;
    for (const auto& definition : definitions) {
        auto object = scene_.createObject(definition.name);
        if (!object.setTransform(definition.transform)) throw EditorDocumentError("scene: invalid object transform");
        object.setActive(definition.active);
        entitiesByGuid.emplace(definition.guid, object.id());
        entityGuids_.emplace(entityKey(object.id()), definition.guid);
        objectExtensions_.emplace(definition.guid, definition.extensions);
    }
    for (const auto& definition : definitions) {
        if (!definition.parentGuid) continue;
        if (!scene_.setParent(entitiesByGuid.at(definition.guid), entitiesByGuid.at(*definition.parentGuid))) {
            throw EditorDocumentError("scene: invalid parent relationship");
        }
    }

    scenePath_ = absolute.lexically_normal();
    sceneExtensions_ = std::move(data);
}

void EditorDocument::save() {
    if (scenePath_.empty()) throw EditorDocumentError("scene: no scene path is open");
    const auto parent = scenePath_.parent_path().empty() ? std::filesystem::current_path() : scenePath_.parent_path();
    if (!std::filesystem::is_directory(parent)) throw EditorDocumentError("scene: parent directory does not exist");

    nlohmann::json data = sceneExtensions_.is_object() ? sceneExtensions_ : nlohmann::json::object();
    data["schema_version"] = 1;
    data["objects"] = nlohmann::json::array();
    for (const auto& state : entities()) {
        nlohmann::json object = objectExtensions_.contains(state.guid)
            ? objectExtensions_.at(state.guid)
            : nlohmann::json::object();
        object["guid"] = state.guid;
        object["name"] = state.name;
        object["active"] = state.active;
        object["transform"] = {
            {"position", vector3Json(state.transform.position)},
            {"rotation", quaternionJson(state.transform.rotation)},
            {"scale", vector3Json(state.transform.scale)},
        };
        const auto parentEntity = scene_.parent(state.id);
        if (parentEntity.valid()) {
            const auto parentGuid = entityGuids_.find(entityKey(parentEntity));
            if (parentGuid == entityGuids_.end()) throw EditorDocumentError("scene: parent identity is missing");
            object["parent_guid"] = parentGuid->second;
        } else {
            object["parent_guid"] = nullptr;
        }
        data["objects"].push_back(std::move(object));
    }

    const auto temporary = temporaryPath(scenePath_);
    try {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output) throw EditorDocumentError("scene: cannot create temporary scene file");
        output << data.dump(2) << '\n';
        output.flush();
        if (!output) throw EditorDocumentError("scene: cannot write scene file");
        output.close();
        publish(temporary, scenePath_);
    } catch (...) {
        removeNoexcept(temporary);
        throw;
    }
    sceneExtensions_ = std::move(data);
    savedCursor_ = cursor_;
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
