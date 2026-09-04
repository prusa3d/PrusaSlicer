
#include <catch2/catch_test_macros.hpp>

#include <Slic3r/App/Yoga/Item.hpp>
#include <Slic3r/App/Yoga/ScrollArea.hpp>

#include "YogaComponentFixture.hpp"

using namespace Slic3r::App::Yoga;

TEST_CASE_METHOD(YogaComponentFixture, "[Yoga::ScrollArea] scroll_pos and scroll_size start at zero when content fits")
{
    ScrollArea* scroll_area = window->emplace_back<ScrollArea>();
    scroll_area->set_width(300.f);
    scroll_area->set_height(300.f);

    Item* child = scroll_area->emplace_back<Item>();
    child->set_width(100.f);
    child->set_height(100.f);
    child->set_flex_shrink(0.f);

    render();

    REQUIRE(scroll_area->scroll_pos().x() == 0.f);
    REQUIRE(scroll_area->scroll_pos().y() == 0.f);
    REQUIRE(scroll_area->scroll_size().x() == 0.f);
    REQUIRE(scroll_area->scroll_size().y() == 0.f);
}

TEST_CASE_METHOD(YogaComponentFixture, "[Yoga::ScrollArea] vertical scrollbar appears when child does not shrink")
{
    ScrollArea* scroll_area = window->emplace_back<ScrollArea>();
    scroll_area->set_width(200.f);
    scroll_area->set_height(100.f);

    Item* child = scroll_area->emplace_back<Item>();
    child->set_height(300.f);
    child->set_flex_shrink(0.f);

    // scroll_max reflects the previous frame's content size; two renders required
    render();
    render();

    REQUIRE(scroll_area->scroll_size().y() > 0.f);
}

TEST_CASE_METHOD(YogaComponentFixture, "[Yoga::ScrollArea] horizontal scrollbar appears when child does not shrink")
{
    ScrollArea* scroll_area = window->emplace_back<ScrollArea>();
    scroll_area->set_width(100.f);
    scroll_area->set_height(200.f);
    scroll_area->set_orientation(Orientation::Horizontal);

    Item* child = scroll_area->emplace_back<Item>();
    child->set_width(300.f);
    child->set_flex_shrink(0.f);

    // scroll_max reflects the previous frame's content size; two renders required
    render();
    render();

    REQUIRE(scroll_area->scroll_size().x() > 0.f);
}

TEST_CASE_METHOD(YogaComponentFixture, "[Yoga::ScrollArea] both scrollbars appear when child overflows both axes")
{
    ScrollArea* scroll_area = window->emplace_back<ScrollArea>();
    scroll_area->set_width(100.f);
    scroll_area->set_height(100.f);

    Item* child = scroll_area->emplace_back<Item>();
    child->set_width(300.f);
    child->set_height(300.f);
    child->set_flex_shrink(0.f);

    // scroll_max reflects the previous frame's content size; two renders required
    render();
    render();

    REQUIRE(scroll_area->scroll_size().x() > 0.f);
    REQUIRE(scroll_area->scroll_size().y() > 0.f);
}

TEST_CASE_METHOD(YogaComponentFixture, "[Yoga::ScrollArea] no scrollbar when child is allowed to shrink")
{
    ScrollArea* scroll_area = window->emplace_back<ScrollArea>();
    scroll_area->set_width(200.f);
    scroll_area->set_height(100.f);

    // flex_shrink defaults to 1 — Yoga compresses the child to fit the ScrollArea
    Item* child = scroll_area->emplace_back<Item>();
    child->set_height(300.f);

    render();

    REQUIRE(scroll_area->scroll_size().y() == 0.f);
}

TEST_CASE_METHOD(YogaComponentFixture, "[Yoga::ScrollArea] multiple non-shrinking children causing overflow")
{
    ScrollArea* scroll_area = window->emplace_back<ScrollArea>();
    scroll_area->set_width(200.f);
    scroll_area->set_height(150.f);
    scroll_area->set_orientation(Orientation::Vertical);

    for (int i = 0; i < 3; ++i) {
        Item* child = scroll_area->emplace_back<Item>();
        child->set_height(80.f);
        child->set_flex_shrink(0.f);
    }

    // scroll_max reflects the previous frame's content size; two renders required
    render();
    render();

    // 3 × 80 = 240 px > 150 px
    REQUIRE(scroll_area->scroll_size().y() > 0.f);
}

TEST_CASE_METHOD(YogaComponentFixture, "[Yoga::ScrollArea] multiple non-shrinking children fitting without scrollbar")
{
    ScrollArea* scroll_area = window->emplace_back<ScrollArea>();
    scroll_area->set_width(200.f);
    scroll_area->set_height(200.f);
    scroll_area->set_orientation(Orientation::Vertical);

    for (int i = 0; i < 2; ++i) {
        Item* child = scroll_area->emplace_back<Item>();
        child->set_height(80.f);
        child->set_flex_shrink(0.f);
    }

    render();

    // 2 × 80 = 160 px < 200 px
    REQUIRE(scroll_area->scroll_size().y() == 0.f);
}

TEST_CASE_METHOD(YogaComponentFixture, "[Yoga::ScrollArea] bottom padding counts toward scrollable content size")
{
    ScrollArea* scroll_area = window->emplace_back<ScrollArea>();
    scroll_area->set_width(200.f);
    scroll_area->set_height(100.f);
    scroll_area->set_padding(Paddings(0.f, 0.f, 0.f, 20.f)); // bottom = 20 px

    Item* child = scroll_area->emplace_back<Item>();
    child->set_height(90.f);
    child->set_flex_shrink(0.f);

    // scroll_max reflects the previous frame's content size; two renders required
    render();
    render();

    // child(90) + bottom_padding(20) = 110 > 100
    REQUIRE(scroll_area->scroll_size().y() > 0.f);
}
