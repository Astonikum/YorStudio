#pragma once

#include "yorstudio/ui/studio_ui_port.hpp"

#include <windows.h>

#include <yorgl/api.h>

#include <optional>

namespace yorstudio {

class Win32Window;

class YorStudioViewport final {
public:
    explicit YorStudioViewport(Win32Window& parent);
    ~YorStudioViewport();

    YorStudioViewport(const YorStudioViewport&) = delete;
    YorStudioViewport& operator=(const YorStudioViewport&) = delete;

    void setFrame(const StudioUiViewportFrame& frame);
    void setBounds(int x, int y, int width, int height);
    void render();
    std::optional<StudioUiViewportSelection> takeSelection();
    static LRESULT CALLBACK windowProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam);

private:
    bool createWindow();
    void initializeCamera(const StudioUiViewportCamera& source);
    void updateCamera();
    void pick(int x, int y);
    void updateCapture();

    Win32Window& parent_;
    HWND window_ = nullptr;
    YorGLRenderer* renderer_ = nullptr;
    StudioUiViewportFrame frame_;
    StudioUiViewportCamera camera_;
    float target_[3] = {};
    float yaw_ = 3.14159265358979323846f;
    float pitch_ = 0.0f;
    float distance_ = 5.0f;
    int width_ = 1;
    int height_ = 1;
    int lastMouseX_ = 0;
    int lastMouseY_ = 0;
    int clickStartX_ = 0;
    int clickStartY_ = 0;
    bool leftDown_ = false;
    bool middleDown_ = false;
    bool rightDown_ = false;
    bool dragged_ = false;
    bool cameraInitialized_ = false;
    std::optional<StudioUiViewportSelection> pendingSelection_;
    bool ready_ = false;
};

} // namespace yorstudio
