#pragma once

#include "yorstudio/ui/studio_ui_port.hpp"

#include <windows.h>

#include <yorgl/api.h>

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
    static LRESULT CALLBACK windowProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam);

private:
    bool createWindow();

    Win32Window& parent_;
    HWND window_ = nullptr;
    YorGLRenderer* renderer_ = nullptr;
    StudioUiViewportFrame frame_;
    int width_ = 1;
    int height_ = 1;
    bool ready_ = false;
};

} // namespace yorstudio
