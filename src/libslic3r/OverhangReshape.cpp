#include "OverhangReshape.hpp"

#include "libslic3r/Layer.hpp"
#include "libslic3r/ClipperUtils.hpp"
#include "libslic3r/Surface.hpp"

#include <algorithm>
#include <cmath>

namespace Slic3r {

void make_overhangs_printable(const std::vector<Layer*> &layers,
                              double                     max_overhang_angle_deg,
                              double                     max_hole_area_mm2)
{
    if (layers.size() < 2)
        return;

    static const double pi = 3.14159265358979323846;
    const double angle    = std::clamp(max_overhang_angle_deg, 0.0, 89.0) * pi / 180.0;
    const double tan_cone = std::tan(angle);

    // Outline of the layer above, already grown into the cone. `layers` is
    // ordered bottom..top, so we walk it top-down.
    ExPolygons grown_above = layers.back()->lslices;
    for (int i = int(layers.size()) - 2; i >= 0; -- i) {
        Layer       *layer = layers[i];
        const double dz    = layers[i + 1]->print_z - layer->print_z;
        if (dz <= 0.) {
            grown_above = layer->lslices;
            continue;
        }

        // Erode the grown outline above by the max printable horizontal step
        // and union it with this layer's own outline. Eroding (not dilating)
        // makes the cone narrow going downward at exactly the overhang angle;
        // where the model shrinks faster than that, the eroded outline sticks
        // out and becomes added support. Printable regions (model wider below)
        // already contain the eroded outline, so nothing is added there.
        const float step    = float(scale_(dz * tan_cone));
        Polygons    all     = offset(grown_above, -step);
        append(all, to_polygons(layer->lslices));
        ExPolygons  grown   = union_ex(all);

        // Clean up before carrying the outline to the next layer: the repeated
        // erode/union accumulates arc vertices and spawns sub-extrusion-width
        // slivers (especially where holes grow and pinch off). Left unchecked
        // these explode the island count and choke G-code ordering. Drop tiny
        // islands and simplify contours to slicing resolution.
        {
            const double min_area = scale_(0.3) * scale_(0.3);
            ExPolygons   cleaned;
            cleaned.reserve(grown.size());
            for (const ExPolygon &ex : grown)
                if (std::abs(ex.area()) > min_area)
                    append(cleaned, ex.simplify(scale_(0.02)));
            grown = std::move(cleaned);
        }

        // Keep large internal cavities open: the top-down union fills any hole
        // whose ceiling is solid above, which would solidify designed cavities.
        // Subtract the model's holes that exceed the threshold back out of the
        // grown outline so only small holes (< threshold) get filled/supported.
        {
            Polygons keep_open;
            for (const ExPolygon &ex : layer->lslices)
                for (const Polygon &hole : ex.holes)
                    if (std::abs(hole.area()) * SCALING_FACTOR * SCALING_FACTOR > max_hole_area_mm2)
                        keep_open.emplace_back(hole);
            if (! keep_open.empty())
                grown = diff_ex(grown, keep_open);
        }

        // Merge the cone into the layer outline. For a single region we replace
        // its slices with the unioned outline so the model and the cone form ONE
        // contour (one shell) instead of two abutting ones - abutting contours
        // would produce a doubled shell and a mass of sliver perimeters that
        // choke G-code generation. Multi-region keeps other regions and gives
        // the extra material to the first region.
        if (! layer->regions().empty()) {
            LayerRegion *lr = layer->regions().front();
            if (layer->regions().size() == 1) {
                lr->m_slices.clear();
                lr->m_slices.append(grown, stInternal);
            } else {
                ExPolygons added = diff_ex(grown, layer->lslices);
                if (! added.empty())
                    lr->m_slices.append(std::move(added), stInternal);
            }
            // Rebuild lslices AND lslice_indices_sorted_by_print_order from the
            // region slices. Overwriting lslices by hand left the print-order
            // index vector stale, which corrupted the G-code island ordering.
            layer->make_slices();
        }

        grown_above = std::move(grown);
    }
}

} // namespace Slic3r
