#pragma once

#include <filesystem>
#include <string_view>

#include <d3d11.h>
#include <windows.h>

namespace yorstudio {

class Win32Window {
public:
    using MessageHandler = LRESULT (*)(HWND, UINT, WPARAM, LPARAM);

    Win32Window() = default;
    ~Win32Window();

    Win32Window(const Win32Window&) = delete;
    Win32Window& operator=(const Win32Window&) = delete;

    bool create(HINSTANCE instance, std::wstring_view title, int width, int height);
    bool pumpMessages();
    bool beginRender();
    void present();
    std::filesystem::path browseForProject() const;
    void setMessageHandler(MessageHandler handler) noexcept { messageHandler_ = handler; }

    HWND handle() const noexcept { return window_; }
    ID3D11Device* device() const noexcept { return device_; }
    ID3D11DeviceContext* context() const noexcept { return context_; }

private:
    static LRESULT CALLBACK windowProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
    bool createDevice();
    bool createRenderTarget();
    bool resizeRenderTarget(UINT width, UINT height);
    void releaseRenderTarget() noexcept;

    HINSTANCE instance_ = nullptr;
    HWND window_ = nullptr;
    IDXGISwapChain* swapChain_ = nullptr;
    ID3D11Device* device_ = nullptr;
    ID3D11DeviceContext* context_ = nullptr;
    ID3D11RenderTargetView* renderTarget_ = nullptr;
    UINT pendingWidth_ = 0;
    UINT pendingHeight_ = 0;
    MessageHandler messageHandler_ = nullptr;
    bool running_ = false;
};

} // namespace yorstudio
