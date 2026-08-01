#include "win32_window.hpp"

#include <shlobj.h>
#include <shobjidl.h>

#include <array>

namespace yorstudio {

namespace {

constexpr wchar_t WindowClassName[] = L"YorStudioWindow";

template <typename T>
void release(T*& object) noexcept {
    if (object) object->Release();
    object = nullptr;
}

} // namespace

Win32Window::~Win32Window() {
    releaseRenderTarget();
    release(swapChain_);
    release(context_);
    release(device_);
    if (window_) DestroyWindow(window_);
    UnregisterClassW(WindowClassName, instance_);
}

bool Win32Window::create(HINSTANCE instance, std::wstring_view title, int width, int height) {
    instance_ = instance;
    WNDCLASSEXW windowClass{
        .cbSize = sizeof(WNDCLASSEXW),
        .style = CS_CLASSDC,
        .lpfnWndProc = &Win32Window::windowProcedure,
        .hInstance = instance_,
        .hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512)),
        .lpszClassName = WindowClassName,
    };
    if (!RegisterClassExW(&windowClass)) return false;

    RECT bounds{0, 0, width, height};
    AdjustWindowRect(&bounds, WS_OVERLAPPEDWINDOW, FALSE);
    window_ = CreateWindowW(
        WindowClassName,
        title.data(),
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        bounds.right - bounds.left,
        bounds.bottom - bounds.top,
        nullptr,
        nullptr,
        instance_,
        this);
    if (!window_ || !createDevice()) return false;

    ShowWindow(window_, SW_SHOWDEFAULT);
    UpdateWindow(window_);
    running_ = true;
    return true;
}

bool Win32Window::createDevice() {
    DXGI_SWAP_CHAIN_DESC description{};
    description.BufferCount = 2;
    description.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    description.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    description.OutputWindow = window_;
    description.SampleDesc.Count = 1;
    description.Windowed = TRUE;
    description.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    constexpr D3D_FEATURE_LEVEL featureLevels[] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0};
    D3D_FEATURE_LEVEL selectedLevel{};
    const HRESULT result = D3D11CreateDeviceAndSwapChain(
        nullptr,
        D3D_DRIVER_TYPE_HARDWARE,
        nullptr,
        0,
        featureLevels,
        static_cast<UINT>(std::size(featureLevels)),
        D3D11_SDK_VERSION,
        &description,
        &swapChain_,
        &device_,
        &selectedLevel,
        &context_);
    return SUCCEEDED(result) && createRenderTarget();
}

bool Win32Window::createRenderTarget() {
    ID3D11Texture2D* backBuffer = nullptr;
    if (FAILED(swapChain_->GetBuffer(0, IID_PPV_ARGS(&backBuffer)))) return false;
    const HRESULT result = device_->CreateRenderTargetView(backBuffer, nullptr, &renderTarget_);
    release(backBuffer);
    return SUCCEEDED(result);
}

void Win32Window::releaseRenderTarget() noexcept {
    release(renderTarget_);
}

bool Win32Window::resizeRenderTarget(UINT width, UINT height) {
    if (!width || !height) return true;
    releaseRenderTarget();
    if (FAILED(swapChain_->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0))) return false;
    return createRenderTarget();
}

bool Win32Window::pumpMessages() {
    MSG message{};
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
        if (message.message == WM_QUIT) running_ = false;
    }
    return running_;
}

bool Win32Window::beginRender() {
    if (!renderTarget_) return false;
    if (pendingWidth_ && pendingHeight_) {
        if (!resizeRenderTarget(pendingWidth_, pendingHeight_)) {
            running_ = false;
            return false;
        }
        pendingWidth_ = pendingHeight_ = 0;
    }
    constexpr std::array clearColor{0.055f, 0.067f, 0.094f, 1.0f};
    context_->OMSetRenderTargets(1, &renderTarget_, nullptr);
    context_->ClearRenderTargetView(renderTarget_, clearColor.data());
    return true;
}

void Win32Window::present() {
    if (swapChain_) swapChain_->Present(1, 0);
}

std::filesystem::path Win32Window::browseForProject() const {
    return browseForDirectory(L"Select a YOR project directory");
}

std::filesystem::path Win32Window::browseForDirectory(std::wstring_view title) const {
    IFileDialog* dialog = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)))) {
        return {};
    }

    DWORD options = 0;
    if (FAILED(dialog->GetOptions(&options)) ||
        FAILED(dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST))) {
        dialog->Release();
        return {};
    }
    const std::wstring dialogTitle(title);
    dialog->SetTitle(dialogTitle.c_str());

    std::filesystem::path selectedPath;
    if (SUCCEEDED(dialog->Show(window_))) {
        IShellItem* item = nullptr;
        if (SUCCEEDED(dialog->GetResult(&item))) {
            PWSTR path = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
                selectedPath = std::filesystem::path(path);
                CoTaskMemFree(path);
            }
            item->Release();
        }
    }
    dialog->Release();
    return selectedPath;
}

std::filesystem::path Win32Window::recentProjectsPath() const {
    std::array<wchar_t, MAX_PATH> localAppData{};
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, SHGFP_TYPE_CURRENT, localAppData.data()))) {
        return {};
    }
    return std::filesystem::path(localAppData.data()) / L"YOR" / L"YorStudio" / L"recent.yorprojects";
}

LRESULT CALLBACK Win32Window::windowProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* self = reinterpret_cast<Win32Window*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = static_cast<Win32Window*>(create->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    if (self && self->messageHandler_ && self->messageHandler_(window, message, wParam, lParam)) return 1;
    if (self && message == WM_SIZE && wParam != SIZE_MINIMIZED) {
        self->pendingWidth_ = LOWORD(lParam);
        self->pendingHeight_ = HIWORD(lParam);
    }
    if (message == WM_DESTROY) PostQuitMessage(0);
    return DefWindowProcW(window, message, wParam, lParam);
}

} // namespace yorstudio
