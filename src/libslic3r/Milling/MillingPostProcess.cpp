#include "MillingPostProcess.hpp"
#include "../Layer.hpp"
#include "../ClipperUtils.hpp"
#include "../BoundingBox.hpp"

namespace Slic3r {

    void MillingPostProcess::getExtrusionLoop(const Layer* layer, Polygon& poly, Polylines& entrypoints, ExtrusionEntityCollection& out_coll) {


        const double milling_diameter = scale_(this->print_config->milling_diameter.get_at(0));

        //get the longest polyline
        Polyline best_polyline;
        for (Polyline& polyline : entrypoints)
            if (polyline.size() > best_polyline.size())
                best_polyline = polyline;

        if (!entrypoints.empty() && best_polyline.size() > 3 ) {
        

            //get two pair of points that are at less than max_dist.
            int32_t first_point_extract_idx = 1;
            int32_t first_point_idx = -1;
            const double dist_max_square = milling_diameter * milling_diameter / 4;
            double best_dist = dist_max_square;
            for (int32_t idx = 0; idx < poly.points.size(); idx++) {
                if (poly.points[idx].distance_to_square(best_polyline.points[first_point_extract_idx]) < best_dist) {
                    best_dist = poly.points[idx].distance_to_square(best_polyline.points[first_point_extract_idx]);
                    first_point_idx = idx;
                }
            }
            if (first_point_idx > -1) {
                //now search the second one
                int32_t second_point_idx = -1;
                for (int32_t idx = first_point_idx +1; idx < poly.points.size(); idx++) {
                    if (poly.points[idx].distance_to_square(poly.points[first_point_idx]) > dist_max_square) {
                        second_point_idx = idx;
                        break;
                    }
                }
                if(second_point_idx == -1)
                    for (int32_t idx = 0; idx < first_point_idx; idx++) {
                        if (poly.points[idx].distance_to_square(poly.points[first_point_idx]) > dist_max_square) {
                            second_point_idx = idx;
                            break;
                        }
                    }

                int32_t second_point_extract_idx = first_point_extract_idx;
                if (second_point_idx == -1) {
                    second_point_idx = first_point_idx;
                } else {
                    //now see if an other extract point is nearer
                    best_dist = poly.points[second_point_idx].distance_to_square(best_polyline.points[second_point_extract_idx]);
                    for (int32_t idx = 0; idx < best_polyline.points.size(); idx++) {
                        if (poly.points[second_point_idx].distance_to_square(best_polyline.points[idx]) < best_dist) {
                            best_dist = poly.points[second_point_idx].distance_to_square(best_polyline.points[idx]);
                            second_point_extract_idx = idx;
                        }
                    }
                }

                ExtrusionPath contour(erMilling);
                contour.mm3_per_mm = 0;
                contour.width = (float)this->print_config->milling_diameter.get_at(0);
                contour.height = (float)layer->height;
                contour.polyline.points.push_back(best_polyline.points[first_point_extract_idx]);
                for (int32_t idx = first_point_idx; idx < poly.points.size(); idx++) {
                    contour.polyline.points.push_back(poly.points[idx]);
                }
                if (second_point_idx <= first_point_idx) {
                    for (int32_t idx = 0; idx < poly.points.size(); idx++) {
                        contour.polyline.points.push_back(poly.points[idx]);
                    }
                }
                for (int32_t idx = 0; idx < second_point_idx + 1; idx++) {
                    contour.polyline.points.push_back(poly.points[idx]);
                }
                contour.polyline.points.push_back(best_polyline.points[second_point_extract_idx]);

                out_coll.append(std::move(contour));
                return;
                

            }
        }
        //default path, without safe-guard up-down.
        ExtrusionPath contour(erMilling);
        contour.polyline = poly.split_at_first_point();
        if (contour.polyline.points.size() > 3)
            contour.polyline.points.push_back(contour.polyline.points[1]);
        contour.mm3_per_mm = 0;
        contour.width = this->print_config->milling_diameter.get_at(0);
        contour.height = (float)layer->height;
        out_coll.append(std::move(contour));
        return;

    }

    ExtrusionEntityCollection MillingPostProcess::process(const Layer* layer)
    {
        if (!can_be_milled(layer)) return ExtrusionEntityCollection();

        const double milling_diameter = scale_(this->print_config->milling_diameter.get_at(0));

        ExPolygons milling_lines;
        for (const Surface& surf : slices->surfaces) {
            ExPolygons surf_milling = offset_ex(surf.expolygon, milling_diameter/2, ClipperLib::jtRound);
            for (const ExPolygon& expoly : surf_milling)
//                expoly.simplify(SCALED_RESOLUTION, &milling_lines); // should already be done
                milling_lines.push_back(expoly);
        }
        milling_lines = union_ex(milling_lines);

        ExPolygons secured_points = offset_ex(milling_lines, milling_diameter / 3);
        ExPolygons entrypoints;
        for (const ExPolygon& expoly : secured_points)
//            expoly.simplify(SCALED_RESOLUTION, &entrypoints); // should already be done
            entrypoints.push_back(expoly);
        entrypoints = union_ex(entrypoints);
        Polygons entrypoints_poly;
        for (const ExPolygon& expoly : secured_points)
            entrypoints_poly.emplace_back(expoly);

        ExtrusionEntityCollection all_milling;
        for (ExPolygon &ex_poly : milling_lines) {
            Polylines good_entry_point = intersection_pl(offset(ex_poly.contour, milling_diameter / 4), entrypoints_poly);
            getExtrusionLoop(layer, ex_poly.contour, good_entry_point, all_milling);
            for (Polygon& hole : ex_poly.holes) {
                good_entry_point = intersection_pl(offset(hole, milling_diameter / 3), entrypoints_poly);
                getExtrusionLoop(layer, hole, good_entry_point, all_milling);
            }
        }

        return all_milling;
    }

    bool MillingPostProcess::can_be_milled(const Layer* layer) {
        return !print_config->milling_diameter.values.empty() && config->milling_post_process
            && layer->bottom_z() >= config->milling_after_z.get_abs_value(this->object_config->first_layer_height.get_abs_value(this->print_config->nozzle_diameter.values.front()));
    }

    ExPolygons MillingPostProcess::get_unmillable_areas(const Layer* layer)
    {
        if (!can_be_milled(layer)) return ExPolygons();

        const coord_t milling_radius = scale_(this->print_config->milling_diameter.get_at(0)) / 2;

        ExPolygons milling_lines;
        ExPolygons surfaces;
        for (const Surface& surf : slices->surfaces) {
            ExPolygons surf_milling = offset_ex(surf.expolygon, milling_radius, ClipperLib::jtRound);
            for (const ExPolygon& expoly : surf_milling)
//                expoly.simplify(SCALED_RESOLUTION, &milling_lines); // should already be done
                milling_lines.push_back(expoly);
            surfaces.push_back(surf.expolygon);
        }
        union_ex(milling_lines, true);
        union_ex(surfaces, true);


        ExPolygons exact_unmillable_area = diff_ex(offset_ex(milling_lines, -milling_radius, ClipperLib::jtRound), surfaces, true);
        if (exact_unmillable_area.empty())
            return exact_unmillable_area;

        //increae a bit the computed unmillable_area to be sure the mill will mill all the plastic
        coord_t safety_offset = milling_radius / 2;
        ExPolygons safe_umillable = diff_ex(offset_ex(exact_unmillable_area, safety_offset), surfaces, true);
        ExPolygons safe_umillable_simplified;
        for (const ExPolygon& expoly : safe_umillable)
//            expoly.simplify(SCALED_RESOLUTION, &safe_umillable_simplified); // should already be done
            safe_umillable_simplified.push_back(expoly);
        return  union_ex(safe_umillable_simplified, true);
    }

}
