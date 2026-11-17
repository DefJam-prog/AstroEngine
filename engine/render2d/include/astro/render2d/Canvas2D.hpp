#pragma once

#include "astro/core/Result.hpp"
#include "astro/assets/AssetRegistry.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace astro::render2d
{
struct Vec2 final
{
    double x = 0.0;
    double y = 0.0;
    friend bool operator==(const Vec2&, const Vec2&) = default;
};

struct Color final
{
    std::uint8_t r = 255;
    std::uint8_t g = 255;
    std::uint8_t b = 255;
    std::uint8_t a = 255;
    friend bool operator==(const Color&, const Color&) = default;
};

struct Rect final
{
    Vec2 center{};
    Vec2 size{1.0, 1.0};
    Color color{};
    std::int32_t layer = 0;
};

struct Sprite final
{
    assets::AssetId asset{};
    Vec2 center{};
    Vec2 size{1.0, 1.0};
    double rotationDegrees = 0.0;
    Color tint{};
    std::int32_t layer = 0;
    bool flipX = false;
    bool flipY = false;
    std::array<Vec2, 4> worldCorners{};
    bool hasWorldCorners = false;
};

struct Camera2D final
{
    Vec2 center{};
    double pixelsPerUnit = 48.0;
    double zoom = 1.0;
};

struct Viewport final
{
    std::uint32_t width = 0;
    std::uint32_t height = 0;
};

struct ScreenRect final
{
    double x = 0.0;
    double y = 0.0;
    double width = 0.0;
    double height = 0.0;
};

class Canvas2D final
{
public:
    static constexpr std::size_t maximumRectangles = 16384;
    static constexpr std::size_t maximumSprites = 16384;

    core::Result<void> addRectangle(Rect rectangle);
    core::Result<void> addSprite(Sprite sprite);
    void clear() noexcept { rectangles_.clear(); sprites_.clear(); }
    [[nodiscard]] const std::vector<Rect>& rectangles() const noexcept { return rectangles_; }
    [[nodiscard]] const std::vector<Sprite>& sprites() const noexcept { return sprites_; }

private:
    std::vector<Rect> rectangles_;
    std::vector<Sprite> sprites_;
};

// Maps a world-space rectangle into a top-left-origin pixel viewport.
[[nodiscard]] core::Result<ScreenRect> project(const Rect& rectangle,
    const Camera2D& camera, Viewport viewport);
[[nodiscard]] core::Result<Vec2> projectPoint(Vec2 point,
    const Camera2D& camera, Viewport viewport);
}
