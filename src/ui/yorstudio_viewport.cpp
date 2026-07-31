#include "yorstudio_viewport.hpp"

#include "../platform/win32_window.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <iterator>
#include <mutex>

namespace yorstudio {

namespace {

constexpr wchar_t ViewportClassName[] = L"YorStudioYorGLViewport";

void registerViewportClass(HINSTANCE instance) {
    static std::once_flag once;
    std::call_once(once, [instance] {
        WNDCLASSEXW windowClass{
            .cbSize = sizeof(WNDCLASSEXW),
            .style = CS_OWNDC,
            .lpfnWndProc = &YorStudioViewport::windowProcedure,
            .hInstance = instance,
            .hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512)),
            .lpszClassName = ViewportClassName,
        };
        RegisterClassExW(&windowClass);
    });
}

} // namespace

YorStudioViewport::YorStudioViewport(Win32Window& parent) : parent_(parent) {
    if (!createWindow()) return;
    renderer_ = yorglCreate(YORGL_BACKEND_DX11);
    if (!renderer_ || yorglCreateSwapChain(renderer_, reinterpret_cast<std::int64_t>(window_), width_, height_) != YORGL_RESULT_OK) {
        if (renderer_) yorglDestroy(renderer_);
        renderer_ = nullptr;
        DestroyWindow(window_);
        window_ = nullptr;
        return;
    }
    yorglWorldSetSkyColor(renderer_, 0.055f, 0.067f, 0.094f);
    ready_ = true;
}

YorStudioViewport::~YorStudioViewport() {
    if (renderer_) yorglDestroy(renderer_);
    if (window_) DestroyWindow(window_);
}

bool YorStudioViewport::createWindow() {
    registerViewportClass(GetModuleHandleW(nullptr));
    window_ = CreateWindowExW(
        0,
        ViewportClassName,
        L"YorStudio Viewport",
        WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN,
        0,
        0,
        width_,
        height_,
        parent_.handle(),
        nullptr,
        GetModuleHandleW(nullptr),
        this);
    return window_ != nullptr;
}

void YorStudioViewport::setFrame(const StudioUiViewportFrame& frame) {
    frame_ = frame;
}

void YorStudioViewport::setBounds(int x, int y, int width, int height) {
    width = std::max(width, 1);
    height = std::max(height, 1);
    if (window_ && (width_ != width || height_ != height)) {
        width_ = width;
        height_ = height;
        SetWindowPos(window_, HWND_TOP, x, y, width_, height_, SWP_NOACTIVATE | SWP_SHOWWINDOW);
        if (ready_) yorglResize(renderer_, width_, height_);
    } else if (window_) {
        SetWindowPos(window_, HWND_TOP, x, y, width_, height_, SWP_NOACTIVATE | SWP_SHOWWINDOW);
    }
}

void YorStudioViewport::render() {
    if (!ready_ || !renderer_) return;
    std::vector<float> vertices;
    vertices.reserve(frame_.vertices.size() * 9);
    for (const auto& vertex : frame_.vertices) {
        vertices.insert(vertices.end(), std::begin(vertex.position), std::end(vertex.position));
        vertices.insert(vertices.end(), std::begin(vertex.color), std::end(vertex.color));
        vertices.insert(vertices.end(), std::begin(vertex.uv), std::end(vertex.uv));
    }
    if (vertices.empty()) {
        yorglWorldClearSections(renderer_);
    } else {
        yorglWorldUploadMesh(renderer_, vertices.data(), static_cast<int>(vertices.size()));
    }
    yorglBeginFrame(renderer_);
    yorglWorldRender(renderer_,
        frame_.camera.position[0], frame_.camera.position[1], frame_.camera.position[2],
        frame_.camera.direction[0], frame_.camera.direction[1], frame_.camera.direction[2],
        frame_.camera.fovYDegrees, frame_.camera.farPlane, width_, height_);
    yorglEndFrame(renderer_);
}

LRESULT CALLBACK YorStudioViewport::windowProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* self = reinterpret_cast<YorStudioViewport*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = static_cast<YorStudioViewport*>(create->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    if (message == WM_ERASEBKGND) return 1;
    return DefWindowProcW(window, message, wParam, lParam);
}

} // namespace yorstudio
