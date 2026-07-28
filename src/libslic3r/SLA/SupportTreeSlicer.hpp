#ifndef SUPPORT_TREE_SLICER_HPP
#define SUPPORT_TREE_SLICER_HPP

#include "libslic3r/ExPolygon.hpp"
#include "libslic3r/SLA/SupportTreeTypes.hpp"

namespace Slic3r {
namespace sla {

const int analytical_slicing_steps_default = 36;

ExPolygons slice_support_tree_at_height(
    const sla::SupportTreeOutput& output,
    float height,
    int steps = analytical_slicing_steps_default
);

std::vector<ExPolygons> slice_support_tree(
    const sla::SupportTreeOutput& output,
    const std::vector<float>& heights,
    int steps = analytical_slicing_steps_default
);

double calculate_supports_area(const sla::SupportTreeOutput& output);

}
}
#endif