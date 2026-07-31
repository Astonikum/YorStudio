#pragma once

#include "studio_ui_port.hpp"

#include <array>

namespace yorstudio {

struct StudioUiViewportRay {
    std::array<float, 3> origin{};
    std::array<float, 3> direction{};
};

StudioUiViewportRay viewportRayFromScreen(const StudioUiViewportCamera& camera,
                                          float x, float y, float width, float height) noexcept;

bool viewportIntersectTriangle(const StudioUiViewportRay& ray,
                               const std::array<float, 3>& a,
                               const std::array<float, 3>& b,
                               const std::array<float, 3>& c,
                               float& distance) noexcept;

} // namespace yorstudio
