#include <catch2/catch_test_macros.hpp>

#include <Slic3r/App/Yoga/AbstractButton.hpp>

#include "YogaComponentFixture.hpp"

using namespace Slic3r::App::Yoga;

// A point clearly outside any normally positioned button.
static constexpr float pos_out_x = 10.f;
static constexpr float pos_out_y = 10.f;

// Helper: parent a new AbstractButton to the fixture's window with a fixed size
// and margin so that Yoga places it at (200, 200) with size (200 x 40).
static AbstractButton* make_button(YogaComponentFixture& f)
{
    AbstractButton* btn = f.window->emplace_back<AbstractButton>();
    btn->set_width(200.f);
    btn->set_height(40.f);
    btn->set_margin(Margins(200.f, 200.f, 0.f, 0.f));
    return btn;
}

TEST_CASE_METHOD(YogaComponentFixture, "AbstractButton: action callback fires on left click")
{
    AbstractButton* btn = make_button(*this);

    int call_count         = 0;
    btn->callbacks().action = [&] { ++call_count; };

    render(); // compute Yoga layout after adding button
    const Vec2f pos = btn->get_global_pos();
    const float cx  = pos.x() + btn->width() * 0.5f;
    const float cy  = pos.y() + btn->height() * 0.5f;

    simulate_click(cx, cy);

    REQUIRE(call_count == 1);
}

TEST_CASE_METHOD(YogaComponentFixture, "AbstractButton: secondary_action fires on right click")
{
    AbstractButton* btn = make_button(*this);

    int call_count                   = 0;
    btn->callbacks().secondary_action = [&] { ++call_count; };

    render();
    const Vec2f pos = btn->get_global_pos();
    const float cx  = pos.x() + btn->width() * 0.5f;
    const float cy  = pos.y() + btn->height() * 0.5f;

    simulate_click(cx, cy, ImGuiMouseButton_Right);

    REQUIRE(call_count == 1);
}

TEST_CASE_METHOD(YogaComponentFixture, "AbstractButton: action does not fire when disabled")
{
    AbstractButton* btn = make_button(*this);

    int call_count         = 0;
    btn->callbacks().action = [&] { ++call_count; };
    btn->set_enabled(false);

    render();
    const Vec2f pos = btn->get_global_pos();
    const float cx  = pos.x() + btn->width() * 0.5f;
    const float cy  = pos.y() + btn->height() * 0.5f;

    simulate_click(cx, cy);

    REQUIRE(call_count == 0);
}

TEST_CASE_METHOD(YogaComponentFixture, "AbstractButton: hovered is true when mouse is over button")
{
    AbstractButton* btn = make_button(*this);

    render();
    const Vec2f pos = btn->get_global_pos();
    const float cx  = pos.x() + btn->width() * 0.5f;
    const float cy  = pos.y() + btn->height() * 0.5f;

    mouse_move(cx, cy);
    render();

    REQUIRE(btn->hovered());
}

TEST_CASE_METHOD(YogaComponentFixture, "AbstractButton: hovered is false when mouse leaves button")
{
    AbstractButton* btn = make_button(*this);

    render();
    const Vec2f pos = btn->get_global_pos();
    const float cx  = pos.x() + btn->width() * 0.5f;
    const float cy  = pos.y() + btn->height() * 0.5f;

    mouse_move(cx, cy);
    render();
    REQUIRE(btn->hovered());

    mouse_move(pos_out_x, pos_out_y);
    render();
    REQUIRE_FALSE(btn->hovered());
}

TEST_CASE_METHOD(YogaComponentFixture, "AbstractButton: hovered_changed fires with correct values")
{
    AbstractButton* btn = make_button(*this);

    std::vector<bool> events;
    btn->callbacks().hovered_changed = [&](bool v) { events.push_back(v); };

    render();
    const Vec2f pos = btn->get_global_pos();
    const float cx  = pos.x() + btn->width() * 0.5f;
    const float cy  = pos.y() + btn->height() * 0.5f;

    mouse_move(cx, cy);
    render(); // hover in
    mouse_move(pos_out_x, pos_out_y);
    render(); // hover out

    REQUIRE(events.size() == 2);
    REQUIRE(events[0] == true);
    REQUIRE(events[1] == false);
}

TEST_CASE_METHOD(YogaComponentFixture, "AbstractButton: checkable button toggles checked on click")
{
    AbstractButton* btn = make_button(*this);
    btn->set_checkable(true);
    REQUIRE_FALSE(btn->checked());

    render();
    const Vec2f pos = btn->get_global_pos();
    const float cx  = pos.x() + btn->width() * 0.5f;
    const float cy  = pos.y() + btn->height() * 0.5f;

    simulate_click(cx, cy);
    REQUIRE(btn->checked());

    render();
    simulate_click(cx, cy);
    REQUIRE_FALSE(btn->checked());
}

TEST_CASE_METHOD(YogaComponentFixture, "AbstractButton: checked_changed fires with correct value")
{
    AbstractButton* btn = make_button(*this);
    btn->set_checkable(true);

    std::vector<bool> events;
    btn->callbacks().checked_changed = [&](bool v) { events.push_back(v); };

    render();
    const Vec2f pos = btn->get_global_pos();
    const float cx  = pos.x() + btn->width() * 0.5f;
    const float cy  = pos.y() + btn->height() * 0.5f;

    simulate_click(cx, cy); // check
    render();
    simulate_click(cx, cy); // uncheck

    REQUIRE(events.size() == 2);
    REQUIRE(events[0] == true);
    REQUIRE(events[1] == false);
}

TEST_CASE_METHOD(YogaComponentFixture, "AbstractButton: action does not fire when click is outside button")
{
    AbstractButton* btn = make_button(*this);

    int call_count         = 0;
    btn->callbacks().action = [&] { ++call_count; };

    render();
    simulate_click(pos_out_x, pos_out_y); // click outside button

    REQUIRE(call_count == 0);
}
