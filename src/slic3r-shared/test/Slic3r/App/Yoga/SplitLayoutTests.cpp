
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <Slic3r/App/Yoga/Item.hpp>
#include <Slic3r/App/Yoga/RootItem.hpp>
#include <Slic3r/App/Yoga/SplitLayout.hpp>

#include "ImGuiFixture.hpp"

using namespace Slic3r;
using namespace Slic3r::App::Yoga;
using Catch::Matchers::WithinRel;


TEST_CASE_METHOD(ImGuiFixture, "[Yoga::SplitLayout] one item")
{
    SplitLayout* split = root.emplace_back<SplitLayout>();

    Item* left = split->emplace_back<Item>();
    left->set_min_width(30);

    render();

    REQUIRE_THAT(left->width(), WithinRel(30, 0.0001));
    REQUIRE(split->object_count() == 1);
}

TEST_CASE_METHOD(ImGuiFixture, "[Yoga::SplitLayout] two items")
{
    SplitLayout* split = root.emplace_back<SplitLayout>();

    Item* left = split->emplace_back<Item>();
    left->set_min_width(30);

    Item* right = split->emplace_back<Item>();
    split->set_flex_child(right, true);

    render();
    render();

    REQUIRE(split->object_count() == 3);
}

TEST_CASE_METHOD(ImGuiFixture, "[Yoga::SplitLayout] dynamic")
{
    SplitLayout* split = root.emplace_back<SplitLayout>();

    Item* left = split->emplace_back<Item>();

    REQUIRE(split->object_count() == 1);

    Item* right = split->emplace_back<Item>();

    REQUIRE(split->object_count() == 3);

    split->remove(left);

    REQUIRE(split->object_count() == 1);

    split->remove(right);

    REQUIRE(split->object_count() == 0);
}
