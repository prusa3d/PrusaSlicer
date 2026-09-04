
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <Slic3r/App/Yoga/Popup.hpp>
#include <Slic3r/App/Yoga/Item.hpp>

#include "YogaComponentFixture.hpp"

using namespace Slic3r::App::Yoga;
using Catch::Matchers::WithinAbs;

// A concrete Popup with a fixed-size content Window for use in layout tests.
struct TestPopup : public Popup
{
    explicit TestPopup(float w = 80.f, float h = 60.f)
    {
        auto win = std::make_unique<Window>("test-popup");
        win->set_width(w);
        win->set_height(h);
        win->set_flags(
            ImGuiWindowFlags_NoDecoration
            | ImGuiWindowFlags_NoInputs
            | ImGuiWindowFlags_NoMove
            | ImGuiWindowFlags_NoBringToFrontOnFocus
        );
        set_content_item(std::move(win));
    }
};

// Place an Item at an absolute position inside the fixture's window.
static Item* make_anchor(YogaComponentFixture& f, float x, float y, float w = 100.f, float h = 50.f)
{
    Item* anchor = f.window->emplace_back<Item>();
    anchor->set_position_type(YGPositionTypeAbsolute);
    anchor->set_left(x);
    anchor->set_top(y);
    anchor->set_width(w);
    anchor->set_height(h);
    return anchor;
}

// ---------------------------------------------------------------------------
// Open / close state and callbacks
// ---------------------------------------------------------------------------

TEST_CASE_METHOD(YogaComponentFixture, "Popup: opened() starts false and toggles with open/close")
{
    auto* anchor = make_anchor(*this, 400.f, 300.f);
    auto* popup  = window->emplace_back<TestPopup>();
    popup->attach_to_item(anchor);

    REQUIRE_FALSE(popup->opened());

    popup->open();
    render();
    REQUIRE(popup->opened());

    popup->close();
    render();
    REQUIRE_FALSE(popup->opened());
}

TEST_CASE_METHOD(YogaComponentFixture, "Popup: opened and closed callbacks fire exactly once each")
{
    auto* anchor = make_anchor(*this, 400.f, 300.f);
    auto* popup  = window->emplace_back<TestPopup>();
    popup->attach_to_item(anchor);

    int opened_count = 0, closed_count = 0;
    popup->callbacks().opened = [&] { ++opened_count; };
    popup->callbacks().closed = [&] { ++closed_count; };

    popup->open();
    render();
    REQUIRE(opened_count == 1);
    REQUIRE(closed_count == 0);

    popup->close();
    render();
    REQUIRE(opened_count == 1);
    REQUIRE(closed_count == 1);
}

TEST_CASE_METHOD(YogaComponentFixture, "Popup: open() is idempotent — callback fires only once")
{
    auto* anchor = make_anchor(*this, 400.f, 300.f);
    auto* popup  = window->emplace_back<TestPopup>();
    popup->attach_to_item(anchor);

    int opened_count          = 0;
    popup->callbacks().opened = [&] { ++opened_count; };

    popup->open();
    popup->open(); // duplicate — no-op
    render();
    REQUIRE(opened_count == 1);
}

TEST_CASE_METHOD(YogaComponentFixture, "Popup: close() on an already-closed popup is a no-op")
{
    auto* anchor = make_anchor(*this, 400.f, 300.f);
    auto* popup  = window->emplace_back<TestPopup>();
    popup->attach_to_item(anchor);

    int closed_count          = 0;
    popup->callbacks().closed = [&] { ++closed_count; };

    popup->open();
    render();
    popup->close();
    render();
    REQUIRE(closed_count == 1);

    // second close should be a no-op
    popup->close();
    render();
    REQUIRE(closed_count == 1);
}

// ---------------------------------------------------------------------------
// attach_to_item — all four preferred positions
//
// Anchor:  (400, 300)  100×50  center=(450, 325)
// Popup:   80×60       half_w=40  half_h=30
// Offset:  10
//
// Right:  left = anchor.right + offset = 510
// top  = max(0, center_y - half_h) = 295
//
// Left:   left = anchor.left - offset - popup_w = 310
// top  = 295
//
// Top:    left = max(0, center_x - half_w) = 410
// top  = anchor.top - offset - popup_h = 230
//
// Bottom: left = 410
// top  = anchor.bottom + offset = 360
// ---------------------------------------------------------------------------

TEST_CASE_METHOD(YogaComponentFixture, "Popup: attach_to_item places popup at all four positions")
{
    auto* anchor = make_anchor(*this, 400.f, 300.f);

    SECTION("Right")
    {
        auto* popup = window->emplace_back<TestPopup>();
        popup->attach_to_item(anchor, Position::Right, 10.f);
        popup->open();
        render();
        REQUIRE_THAT(popup->content_item()->left(), WithinAbs(510.f, 0.5f));
        REQUIRE_THAT(popup->content_item()->top(), WithinAbs(295.f, 0.5f));
    }

    SECTION("Left")
    {
        auto* popup = window->emplace_back<TestPopup>();
        popup->attach_to_item(anchor, Position::Left, 10.f);
        popup->open();
        render();
        REQUIRE_THAT(popup->content_item()->left(), WithinAbs(310.f, 0.5f));
        REQUIRE_THAT(popup->content_item()->top(), WithinAbs(295.f, 0.5f));
    }

    SECTION("Top")
    {
        auto* popup = window->emplace_back<TestPopup>();
        popup->attach_to_item(anchor, Position::Top, 10.f);
        popup->open();
        render();
        REQUIRE_THAT(popup->content_item()->left(), WithinAbs(410.f, 0.5f));
        REQUIRE_THAT(popup->content_item()->top(), WithinAbs(230.f, 0.5f));
    }

    SECTION("Bottom")
    {
        auto* popup = window->emplace_back<TestPopup>();
        popup->attach_to_item(anchor, Position::Bottom, 10.f);
        popup->open();
        render();
        REQUIRE_THAT(popup->content_item()->left(), WithinAbs(410.f, 0.5f));
        REQUIRE_THAT(popup->content_item()->top(), WithinAbs(360.f, 0.5f));
    }
}

// ---------------------------------------------------------------------------
// attach_to_item — viewport clamping when all positions overflow
//
// A 80×730 popup on a 720-high screen overflows in the Y axis for every
// candidate position, so the preferred position is chosen anyway and then
// clamped by move_window_to_bounds.
//
// With preferred=Bottom and anchor at (400, 300):
// Bottom rect: Min.y=360, Max.y=1090 — doesn't fit (730 > 720)
// Top rect:    Min.y=-410            — doesn't fit
// Right rect:  top=max(0,325-365)=0, Max.y=730 > 720 — doesn't fit
// Left rect:   same y → doesn't fit
//
// All fail → use preferred (Bottom) then clamp:
// After size-clamp  Max.y = 1090 - (730-720) = 1080
// After translate   Min.y = 360 - (1080-720) = 0
// Expected: top = 0
// ---------------------------------------------------------------------------

TEST_CASE_METHOD(
    YogaComponentFixture,
    "Popup: attach_to_item clamps to viewport when all positions overflow"
)
{
    auto* anchor = make_anchor(*this, 400.f, 300.f);
    auto* popup  = window->emplace_back<TestPopup>(80.f, 730.f);
    popup->attach_to_item(anchor, Position::Bottom, 10.f);

    popup->open();
    render();

    REQUIRE_THAT(popup->content_item()->top(), WithinAbs(0.f, 0.5f));
}

// ---------------------------------------------------------------------------
// attach_to_center — popup sits at the centre of the 1280×720 viewport
//
// Expected: left = 1280/2 - 80/2 = 600
// top  = 720/2  - 60/2 = 330
// ---------------------------------------------------------------------------

TEST_CASE_METHOD(YogaComponentFixture, "Popup: attach_to_center positions popup at screen centre")
{
    auto* popup = window->emplace_back<TestPopup>();
    popup->attach_to_center();

    popup->open();
    render();

    REQUIRE_THAT(popup->content_item()->left(), WithinAbs(600.f, 0.5f));
    REQUIRE_THAT(popup->content_item()->top(), WithinAbs(330.f, 0.5f));
}

// ---------------------------------------------------------------------------
// FreeStanding — open_at
// ---------------------------------------------------------------------------

TEST_CASE_METHOD(YogaComponentFixture, "Popup: open_at opens the popup in free-standing mode")
{
    auto* popup = window->emplace_back<TestPopup>();
    popup->open_at({200.f, 150.f});
    render();

    REQUIRE(popup->opened());
    REQUIRE_THAT(popup->content_item()->left(), WithinAbs(200.f, 0.5f));
    REQUIRE_THAT(popup->content_item()->top(), WithinAbs(150.f, 0.5f));
}

// ---------------------------------------------------------------------------
// Multiple popups open simultaneously
// ---------------------------------------------------------------------------

TEST_CASE_METHOD(YogaComponentFixture, "Popup: two popups can be open at the same time")
{
    auto* anchor_a = make_anchor(*this, 200.f, 200.f);
    auto* anchor_b = make_anchor(*this, 600.f, 400.f);

    auto* popup_a = window->emplace_back<TestPopup>();
    auto* popup_b = window->emplace_back<TestPopup>();
    popup_a->attach_to_item(anchor_a, Position::Right, 10.f);
    popup_b->attach_to_item(anchor_b, Position::Bottom, 10.f);

    popup_a->open();
    popup_b->open();
    render();

    REQUIRE(popup_a->opened());
    REQUIRE(popup_b->opened());

    // popup_a: right of (200,200) 100×50 → left=310, top=max(0,225-30)=195
    REQUIRE_THAT(popup_a->content_item()->left(), WithinAbs(310.f, 0.5f));
    REQUIRE_THAT(popup_a->content_item()->top(), WithinAbs(195.f, 0.5f));

    // popup_b: below (600,400) 100×50 → left=max(0,650-40)=610, top=460
    REQUIRE_THAT(popup_b->content_item()->left(), WithinAbs(610.f, 0.5f));
    REQUIRE_THAT(popup_b->content_item()->top(), WithinAbs(460.f, 0.5f));
}

// ---------------------------------------------------------------------------
// Repositioning when the anchor moves
// ---------------------------------------------------------------------------

TEST_CASE_METHOD(YogaComponentFixture, "Popup: repositions when the anchor moves to a new location")
{
    auto* anchor = make_anchor(*this, 400.f, 300.f);
    auto* popup  = window->emplace_back<TestPopup>();
    popup->attach_to_item(anchor, Position::Right, 10.f);

    popup->open();
    render();

    // Initial position: right of (400,300) 100×50
    REQUIRE_THAT(popup->content_item()->left(), WithinAbs(510.f, 0.5f));

    // Move anchor to the left side of the screen and render — popup repositions automatically
    anchor->set_left(100.f);
    anchor->set_top(100.f);
    render();

    // New position: right of (100,100) 100×50 → left=210, top=max(0,125-30)=95
    REQUIRE_THAT(popup->content_item()->left(), WithinAbs(210.f, 0.5f));
    REQUIRE_THAT(popup->content_item()->top(), WithinAbs(95.f, 0.5f));
}
