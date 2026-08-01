#include "yor_icons.hpp"

#include <lunasvg.h>

#include <algorithm>
#include <cmath>
#include <utility>

namespace yorstudio::icons {

namespace {

bool validName(std::string_view name) noexcept {
    if (name.empty()) return false;
    return std::all_of(name.begin(), name.end(), [](char character) {
        return (character >= 'a' && character <= 'z') ||
               (character >= '0' && character <= '9') || character == '-';
    });
}

std::string cacheKey(std::string_view name, int width, int height) {
    return std::string(name) + "@" + std::to_string(width) + "x" + std::to_string(height);
}

} // namespace

YorIconStore::YorIconStore(ID3D11Device* device, std::filesystem::path iconDirectory)
    : device_(device), iconDirectory_(std::move(iconDirectory)) {}

YorIconStore::~YorIconStore() {
    clear();
}

ID3D11ShaderResourceView* YorIconStore::load(std::string_view name, int width, int height) {
    if (!device_ || !validName(name) || width <= 0 || height <= 0 || width > 512 || height > 512) return nullptr;
    const auto key = cacheKey(name, width, height);
    if (const auto cached = textures_.find(key); cached != textures_.end()) return cached->second.view;

    const auto document = lunasvg::Document::loadFromFile((iconDirectory_ / (std::string(name) + ".svg")).string());
    if (!document) return nullptr;
    auto bitmap = document->renderToBitmap(width, height);
    if (bitmap.isNull()) return nullptr;
    bitmap.convertToRGBA();
    for (int y = 0; y < bitmap.height(); ++y) {
        auto* row = static_cast<std::uint8_t*>(bitmap.data()) + y * bitmap.stride();
        for (int x = 0; x < bitmap.width(); ++x) {
            auto* pixel = row + x * 4;
            if (pixel[3] != 0) pixel[0] = pixel[1] = pixel[2] = 255;
        }
    }

    D3D11_TEXTURE2D_DESC description{};
    description.Width = static_cast<UINT>(bitmap.width());
    description.Height = static_cast<UINT>(bitmap.height());
    description.MipLevels = 1;
    description.ArraySize = 1;
    description.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    description.SampleDesc.Count = 1;
    description.Usage = D3D11_USAGE_DEFAULT;
    description.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA source{
        bitmap.data(),
        static_cast<UINT>(bitmap.stride()),
        0,
    };

    ID3D11Texture2D* texture = nullptr;
    if (FAILED(device_->CreateTexture2D(&description, &source, &texture))) return nullptr;
    ID3D11ShaderResourceView* view = nullptr;
    const HRESULT result = device_->CreateShaderResourceView(texture, nullptr, &view);
    texture->Release();
    if (FAILED(result)) return nullptr;

    textures_.emplace(key, Texture{view});
    return view;
}

bool YorIconStore::image(std::string_view name, ImVec2 size, ImVec4 tint) {
    const auto width = std::max(1, static_cast<int>(std::lround(size.x)));
    const auto height = std::max(1, static_cast<int>(std::lround(size.y)));
    const auto* view = load(name, width, height);
    if (!view) return false;
    ImGui::Image(reinterpret_cast<ImTextureID>(view), {static_cast<float>(width), static_cast<float>(height)},
                 {0.0f, 0.0f}, {1.0f, 1.0f}, tint, {0.0f, 0.0f, 0.0f, 0.0f});
    return true;
}

bool YorIconStore::button(std::string_view name, std::string_view text, ImVec2 iconSize) {
    const auto width = std::max(1, static_cast<int>(std::lround(iconSize.x)));
    const auto height = std::max(1, static_cast<int>(std::lround(iconSize.y)));
    const auto* view = load(name, width, height);
    if (!view) {
        const auto labelText = std::string(text);
        if (!labelText.empty()) return ImGui::Button(labelText.c_str());
        const auto& style = ImGui::GetStyle();
        const float buttonSize = style.FramePadding.y * 2.0f + static_cast<float>(height);
        const std::string id = "##yor-icon-fallback-" + std::string(name);
        return ImGui::Button(id.c_str(), {buttonSize, buttonSize});
    }

    const auto labelText = std::string(text);
    const bool iconOnly = labelText.empty();
    const auto textSize = ImGui::CalcTextSize(labelText.c_str());
    const auto& style = ImGui::GetStyle();
    const float buttonHeight = style.FramePadding.y * 2.0f + std::max(static_cast<float>(height), textSize.y);
    const ImVec2 buttonSize{
        iconOnly ? buttonHeight : style.FramePadding.x * 2.0f + static_cast<float>(width) + style.ItemInnerSpacing.x + textSize.x,
        buttonHeight,
    };
    const std::string id = "##yor-icon-" + std::string(name) + "-" + labelText;
    const bool pressed = ImGui::Button(id.c_str(), buttonSize);
    const auto min = ImGui::GetItemRectMin();
    const auto draw = ImGui::GetWindowDrawList();
    const ImVec2 iconPosition{
        min.x + (iconOnly ? (buttonSize.x - static_cast<float>(width)) * 0.5f : style.FramePadding.x),
        min.y + (buttonSize.y - static_cast<float>(height)) * 0.5f,
    };
    draw->AddImage(reinterpret_cast<ImTextureID>(view), iconPosition,
                   {iconPosition.x + static_cast<float>(width), iconPosition.y + static_cast<float>(height)},
                   {0.0f, 0.0f}, {1.0f, 1.0f}, IM_COL32_WHITE);
    if (!iconOnly) {
        draw->AddText({iconPosition.x + static_cast<float>(width) + style.ItemInnerSpacing.x,
                       min.y + (buttonSize.y - textSize.y) * 0.5f},
                      ImGui::GetColorU32(ImGuiCol_Text), labelText.c_str());
    }
    return pressed;
}

void YorIconStore::clear() noexcept {
    for (auto& [key, texture] : textures_) {
        if (texture.view) texture.view->Release();
    }
    textures_.clear();
}

} // namespace yorstudio::icons
