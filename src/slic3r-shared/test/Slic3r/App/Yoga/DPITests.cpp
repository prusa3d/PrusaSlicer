
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <Slic3r/App/Yoga/Namespace.hpp>

#include "ImGuiFixture.hpp"

using namespace Slic3r::App::Yoga;
using Catch::Matchers::WithinRel;

// ---------------------------------------------------------------------------
// SizeInfo
// ---------------------------------------------------------------------------

TEST_CASE("[Yoga::SizeInfo] viewport_min and viewport_max")
{
    SizeInfo info;
    info.viewport_size_x = 1280;
    info.viewport_size_y = 720;

    REQUIRE(info.viewport_min() == 720);
    REQUIRE(info.viewport_max() == 1280);
}

TEST_CASE("[Yoga::SizeInfo] viewport_min and viewport_max portrait")
{
    SizeInfo info;
    info.viewport_size_x = 320;
    info.viewport_size_y = 480;

    REQUIRE(info.viewport_min() == 320);
    REQUIRE(info.viewport_max() == 480);
}

TEST_CASE("[Yoga::SizeInfo] equality")
{
    SizeInfo a;
    a.dpi              = 96;
    a.dpi_scale_factor = 1.0f;
    a.viewport_size_x  = 1280;
    a.viewport_size_y  = 720;

    SizeInfo b = a;
    REQUIRE(a == b);
    REQUIRE_FALSE(a != b);
}

TEST_CASE("[Yoga::SizeInfo] inequality dpi")
{
    SizeInfo a;
    a.dpi = 96;
    SizeInfo b;
    b.dpi = 72;

    REQUIRE(a != b);
    REQUIRE_FALSE(a == b);
}

TEST_CASE("[Yoga::SizeInfo] inequality viewport")
{
    SizeInfo a;
    a.viewport_size_x = 1280;
    a.viewport_size_y = 720;
    SizeInfo b;
    b.viewport_size_x = 1920;
    b.viewport_size_y = 1080;

    REQUIRE(a != b);
}

// ---------------------------------------------------------------------------
// EvaluatedUnit — Pixel
// ---------------------------------------------------------------------------

TEST_CASE("[Yoga::Unit] pixel conversion")
{
    SizeInfo info;
    EvaluatedUnit eu{Unit(5.f, Unit::Type::Pixel)};
    eu.evaluate(info);
    REQUIRE_THAT(eu.result, WithinRel(5.0, 0.0001));
}

TEST_CASE("[Yoga::Unit] pixel zero")
{
    SizeInfo info;
    EvaluatedUnit eu{Unit(0.f, Unit::Type::Pixel)};
    eu.evaluate(info);
    REQUIRE_THAT(eu.result, WithinRel(0.0, 0.0001));
}

// ---------------------------------------------------------------------------
// EvaluatedUnit — Point
// ---------------------------------------------------------------------------

// TEST_CASE("[Yoga::Unit] point at 96 DPI")
// {
//     SizeInfo info;
//     info.dpi = 96;
//     // 72pt = 1 inch = 96px at 96 DPI
//     EvaluatedUnit eu{Unit(72.f, Unit::Type::Point)};
//     eu.evaluate(info);
//     REQUIRE_THAT(eu.result, WithinRel(96.0, 0.0001));
// }

// TEST_CASE("[Yoga::Unit] point at 72 DPI")
// {
//     SizeInfo info;
//     info.dpi = 72;
//     // 72pt = 1 inch = 72px at 72 DPI
//     EvaluatedUnit eu{Unit(72.f, Unit::Type::Point)};
//     eu.evaluate(info);
//     REQUIRE_THAT(eu.result, WithinRel(72.0, 0.0001));
// }

// TEST_CASE("[Yoga::Unit] point 36pt at 96 DPI")
// {
//     SizeInfo info;
//     info.dpi = 96;
//     // 36pt = 0.5 inch = 48px at 96 DPI
//     EvaluatedUnit eu{Unit(36.f, Unit::Type::Point)};
//     eu.evaluate(info);
//     REQUIRE_THAT(eu.result, WithinRel(48.0, 0.0001));
// }

// ---------------------------------------------------------------------------
// EvaluatedUnit — Viewport units
// ---------------------------------------------------------------------------

TEST_CASE("[Yoga::Unit] viewport width 50%")
{
    SizeInfo info;
    info.viewport_size_x = 1280;
    EvaluatedUnit eu{Unit(50.f, Unit::Type::ViewportWidth)};
    eu.evaluate(info);
    REQUIRE_THAT(eu.result, WithinRel(640.0, 0.0001));
}

TEST_CASE("[Yoga::Unit] viewport width 100%")
{
    SizeInfo info;
    info.viewport_size_x = 1000;
    EvaluatedUnit eu{Unit(100.f, Unit::Type::ViewportWidth)};
    eu.evaluate(info);
    REQUIRE_THAT(eu.result, WithinRel(1000.0, 0.0001));
}

TEST_CASE("[Yoga::Unit] viewport height 25%")
{
    SizeInfo info;
    info.viewport_size_y = 720;
    EvaluatedUnit eu{Unit(25.f, Unit::Type::ViewportHeight)};
    eu.evaluate(info);
    REQUIRE_THAT(eu.result, WithinRel(180.0, 0.0001));
}

TEST_CASE("[Yoga::Unit] viewport min 50% portrait")
{
    SizeInfo info;
    info.viewport_size_x = 320;
    info.viewport_size_y = 480;
    // min(320, 480) = 320; 50% = 160
    EvaluatedUnit eu{Unit(50.f, Unit::Type::ViewportMin)};
    eu.evaluate(info);
    REQUIRE_THAT(eu.result, WithinRel(160.0, 0.0001));
}

TEST_CASE("[Yoga::Unit] viewport max 50% portrait")
{
    SizeInfo info;
    info.viewport_size_x = 320;
    info.viewport_size_y = 480;
    // max(320, 480) = 480; 50% = 240
    EvaluatedUnit eu{Unit(50.f, Unit::Type::ViewportMax)};
    eu.evaluate(info);
    REQUIRE_THAT(eu.result, WithinRel(240.0, 0.0001));
}

TEST_CASE("[Yoga::Unit] viewport min 50% landscape")
{
    SizeInfo info;
    info.viewport_size_x = 1280;
    info.viewport_size_y = 720;
    // min(1280, 720) = 720; 50% = 360
    EvaluatedUnit eu{Unit(50.f, Unit::Type::ViewportMin)};
    eu.evaluate(info);
    REQUIRE_THAT(eu.result, WithinRel(360.0, 0.0001));
}

TEST_CASE("[Yoga::Unit] viewport max 50% landscape")
{
    SizeInfo info;
    info.viewport_size_x = 1280;
    info.viewport_size_y = 720;
    // max(1280, 720) = 1280; 50% = 640
    EvaluatedUnit eu{Unit(50.f, Unit::Type::ViewportMax)};
    eu.evaluate(info);
    REQUIRE_THAT(eu.result, WithinRel(640.0, 0.0001));
}

// ---------------------------------------------------------------------------
// EvaluatedUnit — Rem (requires ImGui context)
// ---------------------------------------------------------------------------

TEST_CASE_METHOD(ImGuiFixture, "[Yoga::Unit] rem unit")
{
    SizeInfo info;
    info.root_font_size = 13;
    EvaluatedUnit eu{Unit(2.f, Unit::Type::Rem)};
    eu.evaluate(info);
    REQUIRE_THAT(eu.result, WithinRel(2.0f * info.root_font_size, 0.0001f));
}

TEST_CASE_METHOD(ImGuiFixture, "[Yoga::Unit] rem unit 1x")
{
    SizeInfo info;
    info.root_font_size = 13;
    EvaluatedUnit eu{Unit(1.f, Unit::Type::Rem)};
    eu.evaluate(info);
    REQUIRE_THAT(eu.result, WithinRel(info.root_font_size, 0.0001f));
}

// ---------------------------------------------------------------------------
// EvaluatedSides
// ---------------------------------------------------------------------------

TEST_CASE("[Yoga::EvaluatedSides] uniform pixel padding")
{
    SizeInfo info;
    EvaluatedSides es;
    es.source = Sides{Unit(10.f)};
    es.evaluate(info);
    REQUIRE_THAT(es.left, WithinRel(10.0, 0.0001));
    REQUIRE_THAT(es.right, WithinRel(10.0, 0.0001));
    REQUIRE_THAT(es.top, WithinRel(10.0, 0.0001));
    REQUIRE_THAT(es.bottom, WithinRel(10.0, 0.0001));
}

TEST_CASE("[Yoga::EvaluatedSides] per-edge pixel values")
{
    SizeInfo info;
    EvaluatedSides es;
    // Sides(left, top, right, bottom)
    es.source = Sides{Unit(5.f), Unit(10.f), Unit(15.f), Unit(20.f)};
    es.evaluate(info);
    REQUIRE_THAT(es.left, WithinRel(5.0, 0.0001));
    REQUIRE_THAT(es.top, WithinRel(10.0, 0.0001));
    REQUIRE_THAT(es.right, WithinRel(15.0, 0.0001));
    REQUIRE_THAT(es.bottom, WithinRel(20.0, 0.0001));
}

TEST_CASE("[Yoga::EvaluatedSides] mixed units")
{
    SizeInfo info;
    info.dpi             = 96;
    info.viewport_size_x = 1000;
    info.viewport_size_y = 500;

    EvaluatedSides es;
    es.source.left = Unit(10.f, Unit::Type::Pixel);
    // es.source.top    = Unit(25.4f, Unit::Type::Milimeter); // → 96px at 96 DPI
    es.source.right  = Unit(50.f, Unit::Type::ViewportWidth); // → 500px
    es.source.bottom = Unit(72.f, Unit::Type::Point); // → 96px at 96 DPI
    es.evaluate(info);

    REQUIRE_THAT(es.left, WithinRel(10.0, 0.0001));
    // REQUIRE_THAT(es.top, WithinRel(96.0, 0.0001));
    REQUIRE_THAT(es.right, WithinRel(500.0, 0.0001));
    REQUIRE_THAT(es.bottom, WithinRel(96.0, 0.0001));
}

TEST_CASE("[Yoga::EvaluatedSides] viewport height edges")
{
    SizeInfo info;
    info.viewport_size_y = 400;

    EvaluatedSides es;
    es.source.top    = Unit(25.f, Unit::Type::ViewportHeight); // → 100px
    es.source.bottom = Unit(25.f, Unit::Type::ViewportHeight); // → 100px
    es.evaluate(info);

    REQUIRE_THAT(es.top, WithinRel(100.0, 0.0001));
    REQUIRE_THAT(es.bottom, WithinRel(100.0, 0.0001));
}
