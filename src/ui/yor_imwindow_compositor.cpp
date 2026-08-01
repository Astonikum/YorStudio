#include "yor_imwindow_compositor.hpp"

#include "../platform/win32_window.hpp"

#include "backends/imgui_impl_dx11.h"
#include "backends/imgui_impl_win32.h"
#include "imgui.h"
#include "imgui_internal.h"

#include "ImwConfig.h"
#include "ImwMenu.h"
#include "ImwPlatformWindow.h"
#include "ImwWindow.h"
#include "ImwWindowManager.h"

#include <algorithm>
#include <array>
#include <memory>
#include <string>
#include <utility>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace yorstudio {

namespace {

class YorPlatformWindow final : public ImWindow::ImwPlatformWindow {
public:
    explicit YorPlatformWindow(Win32Window& window)
        : ImwPlatformWindow(ImWindow::E_PLATFORM_WINDOW_TYPE_MAIN, false), window_(window) {}

    bool Init(ImWindow::ImwPlatformWindow*) override { return true; }

    ImVec2 GetPosition() const override { return ImVec2(0.0f, 0.0f); }

    ImVec2 GetSize() const override {
        RECT client{};
        if (GetClientRect(window_.handle(), &client)) {
            return ImVec2(static_cast<float>(client.right - client.left), static_cast<float>(client.bottom - client.top));
        }
        return ImGui::GetIO().DisplaySize;
    }

    void Show(bool) override {}
    void SetSize(int, int) override {}
    void SetPosition(int, int) override {}
    void SetWindowMaximized(bool) override {}
    void SetWindowMinimized(bool) override {}

protected:
    void RenderDrawLists(ImDrawData* drawData) override {
        if (window_.beginRender()) {
            ImGui_ImplDX11_RenderDrawData(drawData);
            window_.present();
        }
    }

private:
    Win32Window& window_;
};

class YorFloatingPlatformWindow final : public ImWindow::ImwPlatformWindow {
public:
    YorFloatingPlatformWindow(Win32Window& host, ImWindow::EPlatformWindowType type)
        : ImwPlatformWindow(type, true), host_(host) {}

    ~YorFloatingPlatformWindow() override {
        if (backendInitialized_) {
            SetContext(false);
            ImGui_ImplDX11_Shutdown();
            ImGui_ImplWin32_Shutdown();
            RestoreContext(false);
        }
        release(renderTarget_);
        release(swapChain_);
        if (window_) DestroyWindow(window_);
    }

    bool Init(ImWindow::ImwPlatformWindow*) override {
        if (!registerWindowClass()) return false;

        const DWORD style = GetType() == ImWindow::E_PLATFORM_WINDOW_TYPE_DRAG_PREVIEW
                                 ? WS_POPUP
                                 : WS_OVERLAPPEDWINDOW;
        const DWORD extendedStyle = GetType() == ImWindow::E_PLATFORM_WINDOW_TYPE_DRAG_PREVIEW
                                         ? WS_EX_TOOLWINDOW
                                         : WS_EX_APPWINDOW;
        RECT bounds{0, 0, 640, 480};
        AdjustWindowRectEx(&bounds, style, FALSE, extendedStyle);
        window_ = CreateWindowExW(
            extendedStyle,
            WindowClassName,
            L"YorStudio",
            style,
            CW_USEDEFAULT,
            CW_USEDEFAULT,
            bounds.right - bounds.left,
            bounds.bottom - bounds.top,
            host_.handle(),
            nullptr,
            GetModuleHandleW(nullptr),
            this);
        if (!window_ || !createSwapChain()) return false;

        SetContext(false);
        const bool win32Initialized = ImGui_ImplWin32_Init(window_);
        backendInitialized_ = win32Initialized && ImGui_ImplDX11_Init(host_.device(), host_.context());
        if (!backendInitialized_ && win32Initialized) ImGui_ImplWin32_Shutdown();
        RestoreContext(false);
        if (!backendInitialized_) return false;

        Show(GetType() != ImWindow::E_PLATFORM_WINDOW_TYPE_DRAG_PREVIEW);
        return true;
    }

    ImVec2 GetPosition() const override {
        RECT bounds{};
        if (!window_ || !GetWindowRect(window_, &bounds)) return {};
        return {static_cast<float>(bounds.left), static_cast<float>(bounds.top)};
    }

    ImVec2 GetSize() const override {
        RECT client{};
        if (!window_ || !GetClientRect(window_, &client)) return {640.0f, 480.0f};
        return {static_cast<float>(client.right - client.left), static_cast<float>(client.bottom - client.top)};
    }

    bool IsWindowMaximized() const override { return window_ && IsZoomed(window_) != FALSE; }
    bool IsWindowMinimized() const override { return window_ && IsIconic(window_) != FALSE; }

    void Show(bool show) override {
        if (!window_) return;
        ShowWindow(window_, show ? SW_SHOW : SW_HIDE);
        if (show) UpdateWindow(window_);
    }

    void SetSize(int width, int height) override {
        if (window_) {
            SetWindowPos(window_, nullptr, 0, 0, std::max(width, 240), std::max(height, 160),
                         SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
        }
    }

    void SetPosition(int x, int y) override {
        if (window_) SetWindowPos(window_, nullptr, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    }

    void SetWindowMaximized(bool maximized) override {
        if (window_) ShowWindow(window_, maximized ? SW_MAXIMIZE : SW_RESTORE);
    }

    void SetWindowMinimized(bool minimized) override {
        if (window_) ShowWindow(window_, minimized ? SW_MINIMIZE : SW_RESTORE);
    }

    void SetTitle(const char* title) override {
        if (!window_) return;
        const std::string utf8 = title ? title : "YorStudio";
        const int length = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), static_cast<int>(utf8.size()), nullptr, 0);
        std::wstring wide(static_cast<std::size_t>(std::max(length, 1)), L'\0');
        if (length > 0) {
            MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), static_cast<int>(utf8.size()), wide.data(), length);
        }
        SetWindowTextW(window_, wide.c_str());
    }

protected:
    void PreUpdate() override {
        if (!backendInitialized_) return;
        SetContext(false);
        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        RestoreContext(false);
    }

    void RenderDrawLists(ImDrawData* drawData) override {
        if (!swapChain_ || !renderTarget_ || !drawData) return;
        if (pendingWidth_ && pendingHeight_) {
            resizeRenderTarget(pendingWidth_, pendingHeight_);
            pendingWidth_ = pendingHeight_ = 0;
        }
        const std::array clearColor{0.055f, 0.067f, 0.094f, 1.0f};
        host_.context()->OMSetRenderTargets(1, &renderTarget_, nullptr);
        host_.context()->ClearRenderTargetView(renderTarget_, clearColor.data());
        ImGui_ImplDX11_RenderDrawData(drawData);
        swapChain_->Present(1, 0);
    }

private:
    static constexpr wchar_t WindowClassName[] = L"YorStudioFloatingWindow";

    template <typename T>
    static void release(T*& object) noexcept {
        if (object) object->Release();
        object = nullptr;
    }

    static LRESULT CALLBACK windowProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
        auto* self = reinterpret_cast<YorFloatingPlatformWindow*>(GetWindowLongPtrW(window, GWLP_USERDATA));
        if (message == WM_NCCREATE) {
            const auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
            self = static_cast<YorFloatingPlatformWindow*>(create->lpCreateParams);
            SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }
        if (self) {
            if (self->backendInitialized_) {
                self->SetContext(false);
                const LRESULT handled = ImGui_ImplWin32_WndProcHandler(window, message, wParam, lParam);
                self->RestoreContext(false);
                if (handled) return handled;
            }
            if (message == WM_SIZE && wParam != SIZE_MINIMIZED) {
                self->pendingWidth_ = LOWORD(lParam);
                self->pendingHeight_ = HIWORD(lParam);
            } else if (message == WM_CLOSE) {
                self->OnClose();
                return 0;
            }
        }
        return DefWindowProcW(window, message, wParam, lParam);
    }

    bool registerWindowClass() {
        static bool registered = false;
        if (registered) return true;
        WNDCLASSEXW windowClass{
            .cbSize = sizeof(WNDCLASSEXW),
            .style = CS_CLASSDC,
            .lpfnWndProc = &YorFloatingPlatformWindow::windowProcedure,
            .hInstance = GetModuleHandleW(nullptr),
            .hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512)),
            .lpszClassName = WindowClassName,
        };
        if (!RegisterClassExW(&windowClass)) return false;
        registered = true;
        return true;
    }

    bool createSwapChain() {
        DXGI_SWAP_CHAIN_DESC description{};
        description.BufferCount = 2;
        description.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        description.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        description.OutputWindow = window_;
        description.SampleDesc.Count = 1;
        description.Windowed = TRUE;
        description.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

        IDXGIDevice* dxgiDevice = nullptr;
        IDXGIAdapter* adapter = nullptr;
        IDXGIFactory* factory = nullptr;
        const bool success = SUCCEEDED(host_.device()->QueryInterface(IID_PPV_ARGS(&dxgiDevice))) &&
                             SUCCEEDED(dxgiDevice->GetAdapter(&adapter)) &&
                             SUCCEEDED(adapter->GetParent(IID_PPV_ARGS(&factory))) &&
                             SUCCEEDED(factory->CreateSwapChain(host_.device(), &description, &swapChain_)) &&
                             createRenderTarget();
        release(factory);
        release(adapter);
        release(dxgiDevice);
        return success;
    }

    bool createRenderTarget() {
        ID3D11Texture2D* backBuffer = nullptr;
        if (FAILED(swapChain_->GetBuffer(0, IID_PPV_ARGS(&backBuffer)))) return false;
        const HRESULT result = host_.device()->CreateRenderTargetView(backBuffer, nullptr, &renderTarget_);
        release(backBuffer);
        return SUCCEEDED(result);
    }

    bool resizeRenderTarget(UINT width, UINT height) {
        if (!width || !height) return true;
        release(renderTarget_);
        if (FAILED(swapChain_->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0))) return false;
        return createRenderTarget();
    }

    Win32Window& host_;
    HWND window_ = nullptr;
    IDXGISwapChain* swapChain_ = nullptr;
    ID3D11RenderTargetView* renderTarget_ = nullptr;
    UINT pendingWidth_ = 0;
    UINT pendingHeight_ = 0;
    bool backendInitialized_ = false;
};

class YorWindowManager final : public ImWindow::ImwWindowManager {
public:
    explicit YorWindowManager(Win32Window& window) : window_(window) {}

    bool hasDraggedWindow() const { return GetDraggedWindow() != nullptr; }
    void stopDraggedWindow() { StopDragWindow(); }

protected:
    bool InternalInit() override { return true; }
    void InternalDestroy() override {}
    bool CanCreateMultipleWindow() override { return true; }

    ImWindow::ImwPlatformWindow* CreatePlatformWindow(ImWindow::EPlatformWindowType type,
                                                       ImWindow::ImwPlatformWindow* parent) override {
        if (type == ImWindow::E_PLATFORM_WINDOW_TYPE_MAIN) {
            if (parent != nullptr) return nullptr;
            auto* platformWindow = new YorPlatformWindow(window_);
            if (!platformWindow->Init(parent)) {
                delete platformWindow;
                return nullptr;
            }
            return platformWindow;
        }
        if (type != ImWindow::E_PLATFORM_WINDOW_TYPE_SECONDARY &&
            type != ImWindow::E_PLATFORM_WINDOW_TYPE_DRAG_PREVIEW) {
            return nullptr;
        }
        auto* platformWindow = new YorFloatingPlatformWindow(window_, type);
        if (!platformWindow->Init(parent)) {
            delete platformWindow;
            return nullptr;
        }
        return platformWindow;
    }

    ImVec2 GetCursorPos() override { return ImGui::GetIO().MousePos; }
    bool IsLeftClickDown() override { return ImGui::GetIO().MouseDown[0]; }

private:
    Win32Window& window_;
};

class YorPanel final : public ImWindow::ImwWindow {
public:
    YorPanel(YorImWindowId id, const char* title, const YorImWindowCompositor::DrawCallback& callback)
        : ImwWindow(), id_(id), callback_(callback) {
        SetTitle(title);
        SetClosable(id != YorImWindowId::launcher);
        SetFillingSpace(true);
    }

    ~YorPanel() override = default;

    void OnGui() override {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(20.0f, 20.0f));
        ImGui::BeginChild(
            "##YorPanelContent", ImVec2(0.0f, 0.0f), ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NoBackground);
        callback_(id_);
        ImGui::EndChild();
        ImGui::PopStyleVar();
    }

private:
    YorImWindowId id_;
    const YorImWindowCompositor::DrawCallback& callback_;
};

class YorMenu final : public ImWindow::ImwMenu {
public:
    explicit YorMenu(const YorImWindowCompositor::MenuCallback& callback) : ImwMenu(0, true), callback_(callback) {}

    void OnMenu() override { callback_(); }

private:
    const YorImWindowCompositor::MenuCallback& callback_;
};

} // namespace

struct YorImWindowCompositor::Impl {
    Win32Window* window = nullptr;
    std::unique_ptr<YorWindowManager> manager;
    YorPanel* launcher = nullptr;
    YorPanel* scene = nullptr;
    YorPanel* inspector = nullptr;
    YorMenu* menu = nullptr;
    DrawCallback drawCallback;
    YorImWindowCompositor::MenuCallback menuCallback;
    bool initialized = false;
};

YorImWindowCompositor::YorImWindowCompositor() : impl_(std::make_unique<Impl>()) {}

YorImWindowCompositor::~YorImWindowCompositor() {
    shutdown();
}

void YorImWindowCompositor::initialize(
    Win32Window& window,
    DrawCallback drawCallback,
    MenuCallback menuCallback) {
    if (impl_->initialized) return;
    impl_->window = &window;
    impl_->drawCallback = std::move(drawCallback);
    impl_->manager = std::make_unique<YorWindowManager>(window);
    if (!impl_->manager->Init()) {
        impl_->manager.reset();
        impl_->window = nullptr;
        return;
    }

    auto& windowConfig = impl_->manager->GetConfig();
    windowConfig.m_eTabColorMode = ImWindow::ImwWindowManager::E_TABCOLORMODE_TITLE;
    windowConfig.m_bShowTabBorder = true;
    windowConfig.m_bShowTabShadows = false;

    impl_->menuCallback = std::move(menuCallback);
    impl_->menu = new YorMenu(impl_->menuCallback);
    impl_->launcher = new YorPanel(YorImWindowId::launcher, "Hub", impl_->drawCallback);
    impl_->manager->Dock(impl_->launcher, ImWindow::E_DOCK_ORIENTATION_CENTER);
    impl_->initialized = true;
}

void YorImWindowCompositor::setEditorWindowsVisible(bool visible) {
    if (!impl_->initialized) return;
    const bool currentlyVisible = impl_->scene != nullptr || impl_->inspector != nullptr;
    if (visible == currentlyVisible) return;

    if (visible) {
        impl_->scene = new YorPanel(YorImWindowId::scene, "Scene", impl_->drawCallback);
        impl_->inspector = new YorPanel(YorImWindowId::inspector, "Inspector", impl_->drawCallback);
        impl_->manager->Dock(impl_->scene, ImWindow::E_DOCK_ORIENTATION_LEFT, 0.25f);
        impl_->manager->Dock(impl_->inspector, ImWindow::E_DOCK_ORIENTATION_RIGHT, 0.25f);
        return;
    }

    if (impl_->scene != nullptr) impl_->scene->Destroy();
    if (impl_->inspector != nullptr) impl_->inspector->Destroy();
    impl_->scene = nullptr;
    impl_->inspector = nullptr;
}

void YorImWindowCompositor::draw() {
    if (!impl_->initialized) return;
    impl_->manager->Run(false);
    if (!ImGui::GetIO().MouseDown[0] && impl_->manager->hasDraggedWindow()) {
        impl_->manager->stopDraggedWindow();
        ImGui::ClearActiveID();
    }
}

void YorImWindowCompositor::render() {
    if (impl_->initialized) {
        impl_->manager->Run(true);
    }
}

void YorImWindowCompositor::shutdown() {
    if (!impl_ || !impl_->initialized) return;
    impl_->manager.reset();
    impl_->launcher = nullptr;
    impl_->scene = nullptr;
    impl_->inspector = nullptr;
    impl_->menu = nullptr;
    impl_->window = nullptr;
    impl_->initialized = false;
}

} // namespace yorstudio
