#pragma once

#include "imgui.h"

#include <d3d11.h>

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_map>

namespace yorstudio::icons {

inline constexpr std::uint32_t folderOpen = 0xF0770;
inline constexpr std::uint32_t folderPlus = 0xF0257;
inline constexpr std::uint32_t contentSave = 0xF0193;
inline constexpr std::uint32_t close = 0xF0156;
inline constexpr std::uint32_t plus = 0xF0415;
inline constexpr std::uint32_t deleteObject = 0xF01B4;
inline constexpr std::uint32_t contentDuplicate = 0xF0191;
inline constexpr std::uint32_t vectorSquare = 0xF0001;
inline constexpr std::uint32_t undo = 0xF054C;
inline constexpr std::uint32_t redo = 0xF044E;
inline constexpr std::uint32_t cube = 0xF01A6;

inline std::string utf8(std::uint32_t codepoint) {
    std::string result;
    if (codepoint <= 0x7Fu) {
        result.push_back(static_cast<char>(codepoint));
    } else if (codepoint <= 0x7FFu) {
        result.push_back(static_cast<char>(0xC0u | (codepoint >> 6u)));
        result.push_back(static_cast<char>(0x80u | (codepoint & 0x3Fu)));
    } else if (codepoint <= 0xFFFFu) {
        result.push_back(static_cast<char>(0xE0u | (codepoint >> 12u)));
        result.push_back(static_cast<char>(0x80u | ((codepoint >> 6u) & 0x3Fu)));
        result.push_back(static_cast<char>(0x80u | (codepoint & 0x3Fu)));
    } else {
        result.push_back(static_cast<char>(0xF0u | (codepoint >> 18u)));
        result.push_back(static_cast<char>(0x80u | ((codepoint >> 12u) & 0x3Fu)));
        result.push_back(static_cast<char>(0x80u | ((codepoint >> 6u) & 0x3Fu)));
        result.push_back(static_cast<char>(0x80u | (codepoint & 0x3Fu)));
    }
    return result;
}

inline std::string label(std::uint32_t codepoint, std::string_view text) {
    auto result = utf8(codepoint);
    result.push_back(' ');
    result.append(text);
    return result;
}

class YorIconStore {
public:
    YorIconStore(ID3D11Device* device, std::filesystem::path iconDirectory);
    ~YorIconStore();

    YorIconStore(const YorIconStore&) = delete;
    YorIconStore& operator=(const YorIconStore&) = delete;

    bool image(std::string_view name, ImVec2 size, ImVec4 tint = {1.0f, 1.0f, 1.0f, 1.0f});
    bool button(std::string_view name, std::string_view text, ImVec2 iconSize = {20.0f, 20.0f});

private:
    struct Texture {
        ID3D11ShaderResourceView* view = nullptr;
    };

    ID3D11ShaderResourceView* load(std::string_view name, int width, int height);
    void clear() noexcept;

    ID3D11Device* device_ = nullptr;
    std::filesystem::path iconDirectory_;
    std::unordered_map<std::string, Texture> textures_;
};

} // namespace yorstudio::icons
