#include "astro/render2d/Canvas2D.hpp"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>

namespace
{
int failures = 0;
#define CHECK(expression) do { if (!(expression)) { std::cerr << "FAIL: " #expression "\n"; ++failures; } } while (false)
}

int main()
{
    using namespace astro::render2d;
    Canvas2D canvas;
    CHECK(canvas.rectangles().empty());
    CHECK(canvas.sprites().empty());
    CHECK(canvas.addRectangle({{0.0, 0.0}, {2.0, 1.0}, {255, 0, 0, 255}, 3}));
    CHECK(canvas.addRectangle({{1.0, -1.0}, {1.0, 1.0}, {0, 255, 0, 255}, 0}));
    CHECK(canvas.rectangles().size() == 2);
    CHECK(canvas.addSprite({{42}, {2.0, 3.0}, {0.5, 0.25}, 30.0, {}, 5, true, false}));
    CHECK(canvas.sprites().size() == 1 && canvas.sprites().front().asset.value == 42);
    CHECK(!canvas.addSprite({{}, {}, {1.0, 1.0}, 0.0, {}, 0}));
    CHECK(!canvas.addSprite({{42}, {}, {1.0, 1.0}, std::numeric_limits<double>::infinity(), {}, 0}));
    const auto centered = project(canvas.rectangles().front(), {{0.0, 0.0}, 50.0, 1.0}, {200, 100});
    CHECK(centered);
    if (centered)
    {
        CHECK(centered.value().x == 50.0);
        CHECK(centered.value().y == 25.0);
        CHECK(centered.value().width == 100.0);
        CHECK(centered.value().height == 50.0);
    }
    const auto shifted = project(canvas.rectangles().back(), {{0.0, 0.0}, 50.0, 2.0}, {200, 100});
    CHECK(shifted && shifted.value().x == 150.0 && shifted.value().y == 100.0);
    CHECK(!canvas.addRectangle({{0.0, 0.0}, {0.0, 1.0}, {}, 0}));
    CHECK(!canvas.addRectangle({{std::numeric_limits<double>::infinity(), 0.0}, {1.0, 1.0}, {}, 0}));
    CHECK(!project(canvas.rectangles().front(), {{0.0, 0.0}, 0.0, 1.0}, {200, 100}));
    CHECK(!project(canvas.rectangles().front(), {{0.0, 0.0}, 50.0, 64.1}, {200, 100}));
    CHECK(!project(canvas.rectangles().front(), {{0.0, 0.0}, 50.0, 1.0}, {0, 100}));
    for (std::size_t index = canvas.rectangles().size(); index < Canvas2D::maximumRectangles; ++index)
        CHECK(canvas.addRectangle({{0.0, 0.0}, {1.0, 1.0}, {}, 0}));
    CHECK(canvas.rectangles().size() == Canvas2D::maximumRectangles);
    CHECK(!canvas.addRectangle({{0.0, 0.0}, {1.0, 1.0}, {}, 0}));
    for (std::size_t index = 1; index < Canvas2D::maximumSprites; ++index)
        CHECK(canvas.addSprite({{static_cast<std::uint64_t>(index)}, {}, {1.0, 1.0}, 0.0, {}, 0}));
    CHECK(canvas.sprites().size() == Canvas2D::maximumSprites);
    CHECK(!canvas.addSprite({{Canvas2D::maximumSprites + 1}, {}, {1.0, 1.0}, 0.0, {}, 0}));
    canvas.clear();
    CHECK(canvas.rectangles().empty());
    CHECK(canvas.sprites().empty());
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
