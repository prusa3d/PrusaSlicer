#pragma once

#include "Slic3r/Domain/LayerHeightProfile.hpp"
#include "Slic3r/Domain/SLA/DrainHole.hpp"
#include "Slic3r/Domain/SLA/SupportPoint.hpp"
#include "Slic3r/Domain/Types.hpp"
#include "Slic3r/Biz/Utils/CutUtils.hpp"

#include <optional>
#include <variant>
#include <vector>

namespace Slic3r::App::Undo {

struct CutGizmoState
{
    BoundingBoxf3 bounding_box{};
    Domain::Vec3d center_offset{Domain::Vec3d::Zero()};
    Domain::Transform3d rotation_m{Domain::Transform3d::Identity()};

    bool connectors_editing{false};
    bool is_planar_mode{true};
    Biz::Cut::Groove groove;

    bool keep_as_parts{false};

    struct PartState
    {
        bool keep{true};
        bool place_on_cut{false};
        bool flip{false};
    };

    PartState part_state_upper = {.place_on_cut = true};
    PartState part_state_lower;
};

struct HeightRangeGizmoState
{
    std::optional<Domain::LayerHeightRange> selected_height_range;
};

struct SlaSupportPointsGizmoState
{
    struct Point
    {
        Domain::SLA::SupportPoint support_point; // model object coordinates
        bool selected{false};
    };

    size_t object_id{0};
    std::vector<Point> points;
    // Points edited, but not written into the model yet.
    bool dirty{false};
};

struct SlaHollowGizmoState
{
    struct Hole
    {
        Domain::SLA::DrainHole hole; // model object coordinates
        bool selected{false};
    };

    size_t object_id{0};
    std::vector<Hole> holes;
    // Holes edited, but not written into the model yet.
    bool dirty{false};
};

using ToolState = std::
    variant<CutGizmoState, HeightRangeGizmoState, SlaSupportPointsGizmoState, SlaHollowGizmoState>;
using ToolsState = std::vector<ToolState>;

} // namespace Slic3r::App::Undo
