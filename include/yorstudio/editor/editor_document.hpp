#pragma once

#include <yorengine/scene.hpp>

#include <nlohmann/json.hpp>

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace yorstudio {

class EditorDocumentError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

class SelectionModel {
public:
    bool replace(const yorengine::Scene& scene, yorengine::EntityId entity);
    bool add(const yorengine::Scene& scene, yorengine::EntityId entity);
    bool toggle(const yorengine::Scene& scene, yorengine::EntityId entity);
    void clear() noexcept;
    void prune(const yorengine::Scene& scene);

    bool contains(yorengine::EntityId entity) const noexcept;
    const std::vector<yorengine::EntityId>& entities() const noexcept { return entities_; }
    std::optional<yorengine::EntityId> active() const noexcept { return active_; }

private:
    std::vector<yorengine::EntityId> entities_;
    std::optional<yorengine::EntityId> active_;
};

struct EditorEntityState {
    yorengine::EntityId id{};
    std::string guid;
    std::string name;
    yorengine::Transform transform{};
    bool active = true;
    bool selected = false;
};

class EditorDocument {
public:
    EditorDocument() = default;
    EditorDocument(const EditorDocument&) = delete;
    EditorDocument& operator=(const EditorDocument&) = delete;

    yorengine::Scene& scene() noexcept { return scene_; }
    const yorengine::Scene& scene() const noexcept { return scene_; }

    std::vector<EditorEntityState> entities();
    const SelectionModel& selection() const noexcept { return selection_; }

    bool select(yorengine::EntityId entity);
    bool createObject(std::string name);
    bool renameSelected(std::string name);
    bool setSelectedTransform(yorengine::Transform transform);
    void load(const std::filesystem::path& path);
    void save();
    const std::filesystem::path& scenePath() const noexcept { return scenePath_; }

    bool undo();
    bool redo();
    std::size_t undoCount() const noexcept { return cursor_; }
    std::size_t redoCount() const noexcept { return history_.size() - cursor_; }
    bool dirty() const noexcept { return cursor_ != savedCursor_; }

private:
    struct Command {
        std::string label;
        std::function<bool(yorengine::Scene&)> apply;
        std::function<bool(yorengine::Scene&)> undo;
        std::function<void()> afterApply;
        std::function<void()> afterUndo;
    };

    bool commit(Command command);

    yorengine::Scene scene_;
    SelectionModel selection_;
    std::vector<Command> history_;
    std::size_t cursor_ = 0;
    std::size_t savedCursor_ = 0;
    std::filesystem::path scenePath_;
    nlohmann::json sceneExtensions_ = nlohmann::json::object();
    std::unordered_map<std::uint64_t, std::string> entityGuids_;
    std::unordered_map<std::string, nlohmann::json> objectExtensions_;
};

} // namespace yorstudio
