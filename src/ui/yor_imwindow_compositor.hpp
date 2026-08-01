#pragma once

#include <functional>
#include <memory>

namespace yorstudio {

class Win32Window;

enum class YorImWindowId {
    launcher,
    scene,
    inspector,
};

class YorImWindowCompositor final {
public:
    using DrawCallback = std::function<void(YorImWindowId)>;
    using MenuCallback = std::function<void()>;

    YorImWindowCompositor();
    ~YorImWindowCompositor();

    YorImWindowCompositor(const YorImWindowCompositor&) = delete;
    YorImWindowCompositor& operator=(const YorImWindowCompositor&) = delete;

    void initialize(Win32Window&, DrawCallback, MenuCallback);
    void setEditorWindowsVisible(bool visible);
    void draw();
    void render();
    void shutdown();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace yorstudio
