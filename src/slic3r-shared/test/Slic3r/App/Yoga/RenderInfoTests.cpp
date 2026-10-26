#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <Slic3r/App/Yoga/Popup.hpp>
#include <Slic3r/App/Yoga/Item.hpp>

#include "YogaComponentFixture.hpp"

#include <cmath>

using namespace Slic3r::App::Yoga;
using Catch::Matchers::WithinAbs;

namespace {

struct RenderInfoPopup : public Popup
{
    RenderInfoPopup()
    {
        auto win = std::make_unique<Window>("RenderInfoPopup");
        win->set_width(80.f);
        win->set_height(60.f);
        win->set_flags(
            ImGuiWindowFlags_NoDecoration
            | ImGuiWindowFlags_NoInputs
            | ImGuiWindowFlags_NoMove
            | ImGuiWindowFlags_NoBringToFrontOnFocus
        );
        set_content_item(std::move(win));
    }
};

Item* make_absolute_item(Window* parent, float x, float y, float w, float h)
{
    Item* item = parent->emplace_back<Item>();
    item->set_position_type(YGPositionTypeAbsolute);
    item->set_left(x);
    item->set_top(y);
    item->set_width(w);
    item->set_height(h);
    return item;
}

} // namespace

TEST_CASE_METHOD(
    YogaComponentFixture,
    "RenderInfo: rendered item records its rect and the root frame"
)
{
    Item* item = make_absolute_item(window, 100.f, 50.f, 40.f, 30.f);

    REQUIRE(item->last_render_frame() == -1);
    REQUIRE(std::isnan(item->last_render_pos().x()));

    render();

    REQUIRE_THAT(item->last_render_pos().x(), WithinAbs(100.f, 0.01f));
    REQUIRE_THAT(item->last_render_pos().y(), WithinAbs(50.f, 0.01f));
    REQUIRE_THAT(item->last_render_size().x(), WithinAbs(40.f, 0.01f));
    REQUIRE_THAT(item->last_render_size().y(), WithinAbs(30.f, 0.01f));
    REQUIRE(root.last_rendered_frame() == ImGui::GetFrameCount());
    REQUIRE(item->last_render_frame() == root.last_rendered_frame());
    REQUIRE(window->last_render_frame() == root.last_rendered_frame());
}

TEST_CASE_METHOD(YogaComponentFixture, "RenderInfo: hidden item keeps an older frame stamp")
{
    Item* item = make_absolute_item(window, 10.f, 10.f, 20.f, 20.f);

    render();
    const int visible_frame = item->last_render_frame();
    REQUIRE(visible_frame == root.last_rendered_frame());

    item->set_visible(false);
    render();
    render();

    REQUIRE(item->last_render_frame() == visible_frame);
    REQUIRE(root.last_rendered_frame() > visible_frame);
}

TEST_CASE_METHOD(
    YogaComponentFixture,
    "RenderInfo: popup content window is stamped only while opened"
)
{
    Item* anchor = make_absolute_item(window, 400.f, 300.f, 100.f, 50.f);
    auto* popup  = window->emplace_back<RenderInfoPopup>();
    popup->attach_to_item(anchor);

    render();
    REQUIRE(popup->content_item()->last_render_frame() == -1);

    popup->open();
    render();
    REQUIRE(popup->opened());
    const int opened_frame = popup->content_item()->last_render_frame();
    REQUIRE(opened_frame == root.last_rendered_frame());
    REQUIRE_FALSE(std::isnan(popup->content_item()->last_render_pos().x()));

    popup->close();
    render();
    render();
    REQUIRE_FALSE(popup->opened());
    REQUIRE(popup->content_item()->last_render_frame() < root.last_rendered_frame());
}
