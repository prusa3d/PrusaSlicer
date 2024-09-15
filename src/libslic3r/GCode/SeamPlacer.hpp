///|/ Copyright (c) Prusa Research 2020 - 2022 Pavel Mikuš @Godrak, Lukáš Matěna @lukasmatena,
/// Vojtěch Bubník @bubnikv
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef libslic3r_SeamPlacer_hpp_
#define libslic3r_SeamPlacer_hpp_

#include <optional>
#include <vector>
#include <memory>
#include <atomic>

#include "libslic3r/GCode/SeamAligned.hpp"
#include "libslic3r/Polygon.hpp"
#include "libslic3r/Print.hpp"
#include "libslic3r/Point.hpp"
#include "libslic3r/GCode/SeamPerimeters.hpp"
#include "libslic3r/GCode/SeamChoice.hpp"
#include "libslic3r/GCode/ModelVisibility.hpp"

namespace Slic3r::Seams {

using ObjectSeams =
    std::unordered_map<const PrintObject *, std::vector<std::vector<SeamPerimeterChoice>>>;
using ObjectLayerPerimeters = std::unordered_map<const PrintObject *, Perimeters::LayerPerimeters>;

struct Params
{
    double max_nearest_detour;
    double rear_tolerance;
    double rear_y_offset;
    Aligned::Params aligned;
    double max_distance{};
    unsigned random_seed{};
    double convex_visibility_modifier{};
    double concave_visibility_modifier{};
    Perimeters::PerimeterParams perimeter;
    Slic3r::ModelInfo::Visibility::Params visibility;
    bool staggered_inner_seams;
};

std::ostream& operator<<(std::ostream& os, const Params& params);

class Placer
{
public:
    static Params get_params(const DynamicPrintConfig &config);

    void init(
        SpanOfConstPtrs<PrintObject> objects,
        const Params &params,
        const std::function<void(void)> &throw_if_canceled
    );

    Point place_seam(const Layer *layer, const ExtrusionLoop &loop, const Point &last_pos) const;

private:
    Params params;
    ObjectSeams seams_per_object;
    ObjectLayerPerimeters perimeters_per_layer;
};

} // namespace Slic3r::Seams

#endif // libslic3r_SeamPlacer_hpp_
