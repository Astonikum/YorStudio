#include "yorstudio_viewport.hpp"

#include "../platform/win32_window.hpp"
#include "yorstudio/ui/viewport_math.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iterator>
#include <limits>
#include <mutex>
#include <windowsx.h>

namespace yorstudio {

namespace {

constexpr wchar_t ViewportClassName[] = L"YorStudioYorGLViewport";

using Point = std::array<float, 3>;

Point add(const Point& a, const Point& b) noexcept {
    return {a[0] + b[0], a[1] + b[1], a[2] + b[2]};
}

Point subtract(const Point& a, const Point& b) noexcept {
    return {a[0] - b[0], a[1] - b[1], a[2] - b[2]};
}

Point multiply(const Point& value, float scalar) noexcept {
    return {value[0] * scalar, value[1] * scalar, value[2] * scalar};
}

float dot(const Point& a, const Point& b) noexcept {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

Point normalized(const Point& value, Point fallback) noexcept {
    const float lengthSquared = dot(value, value);
    if (lengthSquared <= 0.000001f) return fallback;
    const float inverseLength = 1.0f / std::sqrt(lengthSquared);
    return multiply(value, inverseLength);
}

void appendVertex(std::vector<StudioUiRenderVertex>& vertices, const Point& position,
                  const std::array<float, 4>& color) {
    StudioUiRenderVertex vertex;
    vertex.position[0] = position[0];
    vertex.position[1] = position[1];
    vertex.position[2] = position[2];
    vertex.color[0] = color[0];
    vertex.color[1] = color[1];
    vertex.color[2] = color[2];
    vertex.color[3] = color[3];
    vertices.push_back(vertex);
}

void appendLine(std::vector<StudioUiRenderVertex>& vertices, Point a, Point b, Point offset,
                const std::array<float, 4>& color) {
    appendVertex(vertices, add(a, offset), color);
    appendVertex(vertices, add(b, offset), color);
    appendVertex(vertices, subtract(b, offset), color);
    appendVertex(vertices, add(a, offset), color);
    appendVertex(vertices, subtract(b, offset), color);
    appendVertex(vertices, subtract(a, offset), color);
}

void appendGrid(std::vector<StudioUiRenderVertex>& vertices) {
    constexpr float extent = 20.0f;
    constexpr float height = 0.005f;
    constexpr float lineWidth = 0.008f;
    constexpr std::array<float, 4> gridColor{0.23f, 0.27f, 0.32f, 1.0f};
    constexpr std::array<float, 4> majorColor{0.31f, 0.35f, 0.41f, 1.0f};
    constexpr std::array<float, 4> xAxisColor{0.86f, 0.18f, 0.18f, 1.0f};
    constexpr std::array<float, 4> yAxisColor{0.22f, 0.82f, 0.30f, 1.0f};
    constexpr std::array<float, 4> zAxisColor{0.22f, 0.45f, 0.92f, 1.0f};

    for (int coordinate = -20; coordinate <= 20; ++coordinate) {
        const float value = static_cast<float>(coordinate);
        const auto& color = coordinate % 5 == 0 ? majorColor : gridColor;
        appendLine(vertices, {-extent, height, value}, {extent, height, value}, {0.0f, 0.0f, lineWidth}, color);
        appendLine(vertices, {value, height, -extent}, {value, height, extent}, {lineWidth, 0.0f, 0.0f}, color);
    }
    appendLine(vertices, {-extent, height + lineWidth, 0.0f}, {extent, height + lineWidth, 0.0f},
               {0.0f, 0.0f, lineWidth * 1.5f}, xAxisColor);
    appendLine(vertices, {0.0f, height + lineWidth, -extent}, {0.0f, height + lineWidth, extent},
               {lineWidth * 1.5f, 0.0f, 0.0f}, zAxisColor);
    appendLine(vertices, {0.0f, -extent, 0.0f}, {0.0f, extent, 0.0f},
               {lineWidth * 1.5f, 0.0f, 0.0f}, yAxisColor);
}

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
    const bool resetCamera = !cameraInitialized_ || frame.sceneKey != frame_.sceneKey;
    frame_ = frame;
    if (resetCamera) {
        initializeCamera(frame.camera);
    } else {
        camera_.fovYDegrees = std::clamp(frame.camera.fovYDegrees, 0.1f, 179.9f);
        camera_.farPlane = std::max(frame.camera.farPlane, 0.1f);
    }
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
    std::vector<StudioUiRenderVertex> renderVertices = frame_.vertices;
    appendGrid(renderVertices);
    std::vector<float> vertices;
    vertices.reserve(renderVertices.size() * 9);
    for (const auto& vertex : renderVertices) {
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
        camera_.position[0], camera_.position[1], camera_.position[2],
        camera_.direction[0], camera_.direction[1], camera_.direction[2],
        camera_.fovYDegrees, camera_.farPlane, width_, height_);
    yorglEndFrame(renderer_);
}

std::optional<StudioUiViewportSelection> YorStudioViewport::takeSelection() {
    const auto selection = pendingSelection_;
    pendingSelection_.reset();
    return selection;
}

void YorStudioViewport::initializeCamera(const StudioUiViewportCamera& source) {
    camera_ = source;
    camera_.fovYDegrees = std::clamp(source.fovYDegrees, 0.1f, 179.9f);
    camera_.farPlane = std::max(source.farPlane, 0.1f);
    const Point position{source.position[0], source.position[1], source.position[2]};
    const Point direction = normalized(
        {source.direction[0], source.direction[1], source.direction[2]}, {0.0f, 0.0f, 1.0f});
    distance_ = 5.0f;
    const Point target = add(position, multiply(direction, distance_));
    target_[0] = target[0];
    target_[1] = target[1];
    target_[2] = target[2];
    const Point offset = subtract(position, target);
    yaw_ = std::atan2(offset[0], offset[2]);
    pitch_ = std::asin(std::clamp(offset[1] / distance_, -1.0f, 1.0f));
    cameraInitialized_ = true;
    updateCamera();
}

void YorStudioViewport::updateCamera() {
    const float cosPitch = std::cos(pitch_);
    const Point offset{
        std::sin(yaw_) * cosPitch * distance_,
        std::sin(pitch_) * distance_,
        std::cos(yaw_) * cosPitch * distance_,
    };
    camera_.position[0] = target_[0] + offset[0];
    camera_.position[1] = target_[1] + offset[1];
    camera_.position[2] = target_[2] + offset[2];
    const Point direction = normalized(
        {target_[0] - camera_.position[0], target_[1] - camera_.position[1], target_[2] - camera_.position[2]},
        {0.0f, 0.0f, 1.0f});
    camera_.direction[0] = direction[0];
    camera_.direction[1] = direction[1];
    camera_.direction[2] = direction[2];
}

void YorStudioViewport::pick(int x, int y) {
    if (!cameraInitialized_ || x < 0 || y < 0 || x >= width_ || y >= height_) return;
    const auto ray = viewportRayFromScreen(camera_, static_cast<float>(x), static_cast<float>(y),
                                           static_cast<float>(width_), static_cast<float>(height_));
    float closest = std::max(camera_.farPlane, 0.1f);
    std::optional<StudioUiViewportSelection> selection;
    for (const auto& entity : frame_.entities) {
        if (entity.firstVertex > frame_.vertices.size() || entity.vertexCount > frame_.vertices.size() - entity.firstVertex) {
            continue;
        }
        const auto begin = frame_.vertices.begin() + static_cast<std::ptrdiff_t>(entity.firstVertex);
        for (std::size_t offset = 0; offset + 2 < entity.vertexCount; offset += 3) {
            const auto point = [&](std::size_t index) {
                const auto& vertex = *(begin + static_cast<std::ptrdiff_t>(index));
                return Point{vertex.position[0], vertex.position[1], vertex.position[2]};
            };
            float distance = 0.0f;
            if (viewportIntersectTriangle(ray, point(offset), point(offset + 1), point(offset + 2), distance) &&
                distance < closest) {
                closest = distance;
                selection = StudioUiViewportSelection{entity.index, entity.generation};
            }
        }
    }
    if (selection) pendingSelection_ = *selection;
}

void YorStudioViewport::updateCapture() {
    if (!window_) return;
    if (leftDown_ || middleDown_ || rightDown_) {
        SetCapture(window_);
    } else if (GetCapture() == window_) {
        ReleaseCapture();
    }
}

LRESULT CALLBACK YorStudioViewport::windowProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* self = reinterpret_cast<YorStudioViewport*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = static_cast<YorStudioViewport*>(create->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    if (message == WM_ERASEBKGND) return 1;
    if (!self) return DefWindowProcW(window, message, wParam, lParam);
    switch (message) {
    case WM_LBUTTONDOWN:
        SetFocus(window);
        self->leftDown_ = true;
        self->dragged_ = false;
        self->lastMouseX_ = self->clickStartX_ = GET_X_LPARAM(lParam);
        self->lastMouseY_ = self->clickStartY_ = GET_Y_LPARAM(lParam);
        self->updateCapture();
        return 0;
    case WM_LBUTTONUP:
        if (self->leftDown_ && !self->dragged_) self->pick(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
        self->leftDown_ = false;
        self->updateCapture();
        return 0;
    case WM_RBUTTONDOWN:
        SetFocus(window);
        self->rightDown_ = true;
        self->dragged_ = false;
        self->lastMouseX_ = GET_X_LPARAM(lParam);
        self->lastMouseY_ = GET_Y_LPARAM(lParam);
        self->updateCapture();
        return 0;
    case WM_RBUTTONUP:
        self->rightDown_ = false;
        self->updateCapture();
        return 0;
    case WM_MBUTTONDOWN:
        SetFocus(window);
        self->middleDown_ = true;
        self->dragged_ = false;
        self->lastMouseX_ = GET_X_LPARAM(lParam);
        self->lastMouseY_ = GET_Y_LPARAM(lParam);
        self->updateCapture();
        return 0;
    case WM_MBUTTONUP:
        self->middleDown_ = false;
        self->updateCapture();
        return 0;
    case WM_MOUSEMOVE: {
        const int x = GET_X_LPARAM(lParam);
        const int y = GET_Y_LPARAM(lParam);
        const int deltaX = x - self->lastMouseX_;
        const int deltaY = y - self->lastMouseY_;
        if (self->leftDown_ && (std::abs(x - self->clickStartX_) > 3 || std::abs(y - self->clickStartY_) > 3)) {
            self->dragged_ = true;
        }
        if (self->rightDown_ || self->middleDown_) {
            self->dragged_ = true;
            if (self->rightDown_) {
                self->yaw_ += static_cast<float>(deltaX) * 0.01f;
                self->pitch_ = std::clamp(self->pitch_ + static_cast<float>(deltaY) * 0.01f, -1.5f, 1.5f);
            }
            if (self->middleDown_) {
                const Point forward = normalized(
                    {self->camera_.direction[0], self->camera_.direction[1], self->camera_.direction[2]},
                    {0.0f, 0.0f, 1.0f});
                const Point right = normalized(
                    {forward[2], 0.0f, -forward[0]}, {1.0f, 0.0f, 0.0f});
                const Point up{
                    forward[1] * right[2],
                    forward[2] * right[0] - forward[0] * right[2],
                    -forward[1] * right[0],
                };
                const float scale = self->distance_ * 0.0025f;
                const Point pan = add(multiply(right, -static_cast<float>(deltaX) * scale),
                                      multiply(up, static_cast<float>(deltaY) * scale));
                self->target_[0] += pan[0];
                self->target_[1] += pan[1];
                self->target_[2] += pan[2];
            }
            self->updateCamera();
        }
        self->lastMouseX_ = x;
        self->lastMouseY_ = y;
        return 0;
    }
    case WM_MOUSEWHEEL: {
        const int wheel = GET_WHEEL_DELTA_WPARAM(wParam);
        self->distance_ = std::clamp(self->distance_ * std::pow(0.85f, static_cast<float>(wheel) / 120.0f), 0.25f, 10000.0f);
        self->updateCamera();
        return 0;
    }
    case WM_CAPTURECHANGED:
        if (reinterpret_cast<HWND>(lParam) != window) {
            self->leftDown_ = false;
            self->middleDown_ = false;
            self->rightDown_ = false;
        }
        break;
    default:
        break;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

} // namespace yorstudio
