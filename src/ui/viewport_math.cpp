#include "yorstudio/ui/viewport_math.hpp"

#include <algorithm>
#include <cmath>

namespace yorstudio {

namespace {

using Vec3 = std::array<float, 3>;

Vec3 subtract(const Vec3& a, const Vec3& b) noexcept {
    return {a[0] - b[0], a[1] - b[1], a[2] - b[2]};
}

Vec3 add(const Vec3& a, const Vec3& b) noexcept {
    return {a[0] + b[0], a[1] + b[1], a[2] + b[2]};
}

Vec3 multiply(const Vec3& value, float scalar) noexcept {
    return {value[0] * scalar, value[1] * scalar, value[2] * scalar};
}

float dot(const Vec3& a, const Vec3& b) noexcept {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

Vec3 cross(const Vec3& a, const Vec3& b) noexcept {
    return {
        a[1] * b[2] - a[2] * b[1],
        a[2] * b[0] - a[0] * b[2],
        a[0] * b[1] - a[1] * b[0],
    };
}

Vec3 normalized(const Vec3& value, Vec3 fallback) noexcept {
    const float lengthSquared = dot(value, value);
    if (lengthSquared <= 0.000001f) return fallback;
    const float inverseLength = 1.0f / std::sqrt(lengthSquared);
    return multiply(value, inverseLength);
}

} // namespace

StudioUiViewportRay viewportRayFromScreen(const StudioUiViewportCamera& camera,
                                          float x, float y, float width, float height) noexcept {
    width = std::max(width, 1.0f);
    height = std::max(height, 1.0f);
    const Vec3 forward = normalized(
        {camera.direction[0], camera.direction[1], camera.direction[2]}, {0.0f, 0.0f, 1.0f});
    const Vec3 right = normalized(cross({0.0f, 1.0f, 0.0f}, forward), {1.0f, 0.0f, 0.0f});
    const Vec3 up = cross(forward, right);
    const float aspect = width / height;
    const float halfFov = std::clamp(camera.fovYDegrees, 0.1f, 179.9f) * 0.5f * 0.01745329251994329577f;
    const float tangent = std::tan(halfFov);
    const float normalizedX = (2.0f * (x + 0.5f) / width - 1.0f) * aspect * tangent;
    const float normalizedY = (1.0f - 2.0f * (y + 0.5f) / height) * tangent;
    const Vec3 direction = normalized(
        add(add(forward, multiply(right, normalizedX)), multiply(up, normalizedY)), forward);
    return {
        {camera.position[0], camera.position[1], camera.position[2]},
        direction,
    };
}

bool viewportIntersectTriangle(const StudioUiViewportRay& ray,
                               const std::array<float, 3>& a,
                               const std::array<float, 3>& b,
                               const std::array<float, 3>& c,
                               float& distance) noexcept {
    constexpr float epsilon = 0.000001f;
    const Vec3 edge1 = subtract(b, a);
    const Vec3 edge2 = subtract(c, a);
    const Vec3 p = cross(ray.direction, edge2);
    const float determinant = dot(edge1, p);
    if (std::abs(determinant) <= epsilon) return false;
    const float inverseDeterminant = 1.0f / determinant;
    const Vec3 originToA = subtract(ray.origin, a);
    const float u = dot(originToA, p) * inverseDeterminant;
    if (u < 0.0f || u > 1.0f) return false;
    const Vec3 q = cross(originToA, edge1);
    const float v = dot(ray.direction, q) * inverseDeterminant;
    if (v < 0.0f || u + v > 1.0f) return false;
    const float hitDistance = dot(edge2, q) * inverseDeterminant;
    if (hitDistance <= epsilon) return false;
    distance = hitDistance;
    return true;
}

} // namespace yorstudio
