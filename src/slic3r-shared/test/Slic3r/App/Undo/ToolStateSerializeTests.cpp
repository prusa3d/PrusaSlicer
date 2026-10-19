#include <catch2/catch_test_macros.hpp>

#include "Slic3r/App/Undo/ToolStateSerialize.hpp"

using Slic3r::App::Undo::SlaSupportPointsGizmoState;
using Slic3r::App::Undo::ToolsState;
using Slic3r::Domain::SLA::SupportPoint;
using Slic3r::Domain::SLA::SupportPointType;
using Slic3r::Domain::Vec3f;

TEST_CASE("Undo tool state keeps the edited SLA support points", "[undo]")
{
    SlaSupportPointsGizmoState state;
    state.object_id = 42;
    state.dirty     = true;
    state.points    = {
        {SupportPoint{Vec3f{1.f, 2.f, 3.f}, 0.2f, SupportPointType::island}, true},
        {SupportPoint{Vec3f{4.f, 5.f, 6.f}, 0.3f, SupportPointType::manual_add}, false},
    };

    const ToolsState loaded{Slic3r::App::Undo::load_serialized_tools_state(
        Slic3r::App::Undo::serialize_tools_state(ToolsState{state})
    )};

    REQUIRE(loaded.size() == 1);
    REQUIRE(std::holds_alternative<SlaSupportPointsGizmoState>(loaded.front()));
    const auto& loaded_state = std::get<SlaSupportPointsGizmoState>(loaded.front());
    CHECK(loaded_state.object_id == 42);
    CHECK(loaded_state.dirty);
    REQUIRE(loaded_state.points.size() == 2);
    CHECK(loaded_state.points[0].support_point == state.points[0].support_point);
    CHECK(loaded_state.points[0].support_point.type == SupportPointType::island);
    CHECK(loaded_state.points[0].selected);
    CHECK(loaded_state.points[1].support_point == state.points[1].support_point);
    CHECK_FALSE(loaded_state.points[1].selected);
}

TEST_CASE("Undo tool state keeps the edited SLA drain holes", "[undo]")
{
    using Slic3r::App::Undo::SlaHollowGizmoState;
    using Slic3r::Domain::SLA::DrainHole;

    SlaHollowGizmoState state;
    state.object_id = 7;
    state.dirty     = true;
    state.holes     = {
        {DrainHole{Vec3f{1.f, 2.f, 3.f}, Vec3f{0.f, 0.f, -1.f}, 2.f, 6.f}, true},
        {DrainHole{Vec3f{4.f, 5.f, 6.f}, Vec3f{1.f, 0.f, 0.f}, 1.5f, 3.f}, false},
    };

    const ToolsState loaded{Slic3r::App::Undo::load_serialized_tools_state(
        Slic3r::App::Undo::serialize_tools_state(ToolsState{state})
    )};

    REQUIRE(loaded.size() == 1);
    REQUIRE(std::holds_alternative<SlaHollowGizmoState>(loaded.front()));
    const auto& loaded_state = std::get<SlaHollowGizmoState>(loaded.front());
    CHECK(loaded_state.object_id == 7);
    CHECK(loaded_state.dirty);
    REQUIRE(loaded_state.holes.size() == 2);
    CHECK(loaded_state.holes[0].hole == state.holes[0].hole);
    CHECK(loaded_state.holes[0].selected);
    CHECK(loaded_state.holes[1].hole == state.holes[1].hole);
    CHECK_FALSE(loaded_state.holes[1].selected);
}
