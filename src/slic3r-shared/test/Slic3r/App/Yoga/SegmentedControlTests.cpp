#include <catch2/catch_test_macros.hpp>
#include "Slic3r/App/Yoga/ImGuiFixture.hpp"
#include "Slic3r/App/Yoga/RootItem.hpp"
#include "Slic3r/App/Yoga/SegmentedControl.hpp"

using Slic3r::App::Render::Icon;
using Slic3r::App::Yoga::RootItem;
using Slic3r::App::Yoga::SegmentedControl;
using Segment = Slic3r::App::Yoga::SegmentedControl::Segment;

TEST_CASE_METHOD(ImGuiFixture, "Segmented control", "[Yoga]")
{
    RootItem tree;

    auto segments = {
        Segment{.icon = Icon::Circle, .tooltip = "a"},
        Segment{
            .icon               = Icon::Sphere,
            .tooltip            = "b",
            .initially_selected = true,
        },
    };

    const auto segmented_control{tree.emplace_back<SegmentedControl>(segments)};
    CHECK(segmented_control->selected_index() == 1);
}
