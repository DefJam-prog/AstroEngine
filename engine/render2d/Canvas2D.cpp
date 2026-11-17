#include "astro/render2d/Canvas2D.hpp"

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>

namespace astro::render2d
{
namespace
{
core::Error invalid(std::string message)
{
    return {core::ErrorCode::ValidationError, std::move(message)};
}

bool finite(const Vec2 value)
{
    return std::isfinite(value.x) && std::isfinite(value.y);
}
}

core::Result<void> Canvas2D::addRectangle(Rect rectangle)
{
    if (!finite(rectangle.center) || !finite(rectangle.size)
        || rectangle.size.x <= 0.0 || rectangle.size.y <= 0.0
        || rectangle.size.x > 1000000.0 || rectangle.size.y > 1000000.0)
        return core::Result<void>::failure(invalid("Rectangle position and size must be finite and positive"));
    if (rectangles_.size() >= maximumRectangles)
        return core::Result<void>::failure(invalid("2D canvas rectangle limit reached"));
    rectangles_.push_back(std::move(rectangle));
    return core::Result<void>::success();
}

core::Result<void> Canvas2D::addSprite(Sprite sprite)
{
    bool geometryValid = false;
    if (sprite.hasWorldCorners)
    {
        geometryValid = true;
        for (const auto corner : sprite.worldCorners)
            geometryValid = geometryValid && finite(corner)
                && std::abs(corner.x) <= 1000000.0 && std::abs(corner.y) <= 1000000.0;
    }
    else
    {
        geometryValid = finite(sprite.center) && finite(sprite.size)
            && std::isfinite(sprite.rotationDegrees)
            && sprite.size.x > 0.0 && sprite.size.y > 0.0
            && sprite.size.x <= 1000000.0 && sprite.size.y <= 1000000.0;
    }
    if (sprite.asset.value == 0 || !geometryValid)
        return core::Result<void>::failure(invalid("Sprite asset, position, rotation and size must be valid"));
    if (sprites_.size() >= maximumSprites)
        return core::Result<void>::failure(invalid("2D canvas sprite limit reached"));
    sprites_.push_back(std::move(sprite));
    return core::Result<void>::success();
}

core::Result<ScreenRect> project(const Rect& rectangle, const Camera2D& camera, const Viewport viewport)
{
    if (!finite(camera.center) || !std::isfinite(camera.pixelsPerUnit) || !std::isfinite(camera.zoom)
        || camera.pixelsPerUnit <= 0.0 || camera.pixelsPerUnit > 10000.0
        || camera.zoom < 0.05 || camera.zoom > 64.0)
        return core::Result<ScreenRect>::failure(invalid("Camera values are outside valid finite bounds"));
    if (viewport.width == 0 || viewport.height == 0 || viewport.width > 16384 || viewport.height > 16384)
        return core::Result<ScreenRect>::failure(invalid("Viewport dimensions are outside supported bounds"));
    if (!finite(rectangle.center) || !finite(rectangle.size)
        || rectangle.size.x <= 0.0 || rectangle.size.y <= 0.0
        || rectangle.size.x > 1000000.0 || rectangle.size.y > 1000000.0)
        return core::Result<ScreenRect>::failure(invalid("Rectangle position and size must be finite and positive"));
    const double scale = camera.pixelsPerUnit * camera.zoom;
    const ScreenRect projected{
        static_cast<double>(viewport.width) * 0.5 + (rectangle.center.x - camera.center.x) * scale - rectangle.size.x * scale * 0.5,
        static_cast<double>(viewport.height) * 0.5 - (rectangle.center.y - camera.center.y) * scale - rectangle.size.y * scale * 0.5,
        rectangle.size.x * scale,
        rectangle.size.y * scale};
    if (!std::isfinite(projected.x) || !std::isfinite(projected.y)
        || !std::isfinite(projected.width) || !std::isfinite(projected.height))
        return core::Result<ScreenRect>::failure(invalid("Projected rectangle exceeds finite pixel coordinates"));
    return core::Result<ScreenRect>::success(projected);
}

core::Result<Vec2> projectPoint(const Vec2 point, const Camera2D& camera, const Viewport viewport)
{
    if (!finite(point) || !finite(camera.center) || !std::isfinite(camera.pixelsPerUnit)
        || !std::isfinite(camera.zoom) || camera.pixelsPerUnit <= 0.0
        || camera.pixelsPerUnit > 10000.0 || camera.zoom < 0.05 || camera.zoom > 64.0)
        return core::Result<Vec2>::failure(invalid("Point and camera must contain supported finite values"));
    if (viewport.width == 0 || viewport.height == 0 || viewport.width > 16384 || viewport.height > 16384)
        return core::Result<Vec2>::failure(invalid("Viewport dimensions are outside supported bounds"));
    const double scale = camera.pixelsPerUnit * camera.zoom;
    const Vec2 projected{
        static_cast<double>(viewport.width) * 0.5 + (point.x - camera.center.x) * scale,
        static_cast<double>(viewport.height) * 0.5 - (point.y - camera.center.y) * scale};
    if (!finite(projected)) return core::Result<Vec2>::failure(invalid("Projected point exceeds finite pixel coordinates"));
    return core::Result<Vec2>::success(projected);
}
}
