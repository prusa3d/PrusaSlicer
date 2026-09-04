
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <Slic3r/App/Yoga/Item.hpp>

#include "YogaComponentFixture.hpp"

using namespace Slic3r::App::Yoga;
using Catch::Matchers::WithinRel;

// ---------------------------------------------------------------------------
// Viewport units in layout
// ---------------------------------------------------------------------------

TEST_CASE_METHOD(ImGuiFixture, "[Yoga::Item][DPI] viewport width min-size")
{
    default_size_info.viewport_size_x = 1000;
    default_size_info.viewport_size_y = 500;

    Item* child = root.emplace_back<Item>();
    child->set_min_width(50_ww); // 50% of 1000 → 500px

    render();

    REQUIRE_THAT(child->width(), WithinRel(500.0, 0.0001));
}

TEST_CASE_METHOD(ImGuiFixture, "[Yoga::Item][DPI] viewport height min-size")
{
    default_size_info.viewport_size_x = 1000;
    default_size_info.viewport_size_y = 500;

    root.set_orientation(Orientation::Vertical);
    Item* child = root.emplace_back<Item>();
    child->set_min_height(50_wh); // 50% of 500 → 250px

    render();

    REQUIRE_THAT(child->height(), WithinRel(250.0, 0.0001));
}

TEST_CASE_METHOD(YogaComponentFixture, "[Yoga::Item][DPI] viewport min two children")
{
    default_size_info.viewport_size_x = 1000;
    default_size_info.viewport_size_y = 600;

    // container pinned to 1000px so flex math stays predictable
    Item* container = window->emplace_back<Item>();
    container->set_width(1000.f);
    container->set_orientation(Orientation::Horizontal);

    Item* child_left = container->emplace_back<Item>();
    child_left->set_min_width(30_ww); // 30% of 1000 → 300px

    Item* child_right = container->emplace_back<Item>();
    child_right->set_flex_grow(1);

    render();

    REQUIRE_THAT(child_left->width(), WithinRel(300.0, 0.0001));
    REQUIRE_THAT(child_right->width(), WithinRel(700.0, 0.0001));
}

// ---------------------------------------------------------------------------
// Point units in layout
// ---------------------------------------------------------------------------

TEST_CASE_METHOD(YogaComponentFixture, "[Yoga::Item][DPI] point min-width at 96 DPI")
{
    // Default fixture SizeInfo already has dpi=96
    Item* child = window->emplace_back<Item>();
    child->set_min_width(72_pt); // 72pt = 1 inch = 96px at 96 DPI

    render();

    REQUIRE_THAT(child->width(), WithinRel(96.0, 0.0001));
}

TEST_CASE_METHOD(YogaComponentFixture, "[Yoga::Item][DPI] point padding")
{
    // Default fixture SizeInfo already has dpi=96
    // container with explicit size to make padding math deterministic
    Item* container = window->emplace_back<Item>();
    container->set_width(392.f);
    container->set_height(292.f);
    container->set_padding(72_pt); // 96px all sides

    Item* child = container->emplace_back<Item>();
    child->set_flex_grow(1);

    render();

    // 392 - 2*96 = 200 wide; 292 - 2*96 = 100 tall
    REQUIRE_THAT(child->width(), WithinRel(200.0, 0.0001));
    REQUIRE_THAT(child->height(), WithinRel(100.0, 0.0001));
    REQUIRE_THAT(child->left(), WithinRel(96.0, 0.0001));
    REQUIRE_THAT(child->top(), WithinRel(96.0, 0.0001));
}

// ---------------------------------------------------------------------------
// Rem units in layout
// ---------------------------------------------------------------------------

TEST_CASE_METHOD(YogaComponentFixture, "[Yoga::Item][DPI] rem min-width")
{
    Item* child = window->emplace_back<Item>();
    child->set_min_width(2_rem);

    render();

    REQUIRE_THAT(child->width(), WithinRel(2.0f * default_size_info.root_font_size, 0.0001f));
}

// ---------------------------------------------------------------------------
// SizeInfo update propagation
// ---------------------------------------------------------------------------

TEST_CASE_METHOD(
    YogaComponentFixture,
    "[Yoga::Item][DPI] viewport size_info change re-evaluates units"
)
{
    Item* child = window->emplace_back<Item>();
    child->set_min_width(50_ww); // 50% of viewport width

    default_size_info.viewport_size_x = 1000;
    render();
    REQUIRE_THAT(child->width(), WithinRel(500.0, 0.0001));

    default_size_info.viewport_size_x = 800;
    render();
    REQUIRE_THAT(child->width(), WithinRel(400.0, 0.0001));
}

// ---------------------------------------------------------------------------
// Mixed unit types in one layout
// ---------------------------------------------------------------------------

TEST_CASE_METHOD(ImGuiFixture, "[Yoga::Item][DPI] mixed units in one layout")
{
    default_size_info.dpi             = 96;
    default_size_info.viewport_size_x = 1000;

    Item* child_px = root.emplace_back<Item>();
    child_px->set_min_width(50_px); // exactly 50px

    Item* child_vw = root.emplace_back<Item>();
    child_vw->set_min_width(10_ww); // 10% of 1000 = 100px

    render();

    REQUIRE_THAT(child_px->width(), WithinRel(50.0, 0.0001));
    REQUIRE_THAT(child_vw->width(), WithinRel(100.0, 0.0001));
}
