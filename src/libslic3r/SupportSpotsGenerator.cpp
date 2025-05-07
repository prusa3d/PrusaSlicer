///|/ Copyright (c) Prusa Research 2022 - 2023 Pavel Mikuš @Godrak, Vojtěch Bubník @bubnikv
///|/ Copyright (c) SuperSlicer 2023 Remi Durand @supermerill
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#include "SupportSpotsGenerator.hpp"

#include <boost/log/trivial.hpp>
#include <oneapi/tbb/concurrent_vector.h>
#include <oneapi/tbb/parallel_for.h>
#include <oneapi/tbb/blocked_range.h>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>
#include <iostream>
#include <stdexcept>
#include <cassert>

#include "BoundingBox.hpp"
#include "ExPolygon.hpp"
#include "ExtrusionEntity.hpp"
#include "ExtrusionEntityCollection.hpp"
#include "libslic3r/GCode/ExtrusionProcessor.hpp"
#include "Line.hpp"
#include "Point.hpp"
#include "Polygon.hpp"
#include "PrincipalComponents2D.hpp"
#include "Print.hpp"
#include "PrintBase.hpp"
#include "PrintConfig.hpp"
#include "libslic3r.h"
#include "AABBTreeLines.hpp"
#include "KDTreeIndirect.hpp"
#include "libslic3r/Layer.hpp"
#include "libslic3r/ClipperUtils.hpp"
#include "Geometry/ConvexHull.hpp"
#include "libslic3r/ExtrusionRole.hpp"
#include "libslic3r/Flow.hpp"
#include "libslic3r/LayerRegion.hpp"

// #define DETAILED_DEBUG_LOGS
// #define DEBUG_FILES

#ifdef DEBUG_FILES
#include <boost/nowide/cstdio.hpp>

#include "libslic3r/Color.hpp"

constexpr bool debug_files = true;
#else
constexpr bool debug_files = false;
#endif


namespace Slic3r::SupportSpotsGenerator {

ExtrusionLine::ExtrusionLine() : a(Vec2f::Zero()), b(Vec2f::Zero()), len(0.0), origin_entity(nullptr) {}
ExtrusionLine::ExtrusionLine(const Vec2f &a, const Vec2f &b, float len, const ExtrusionEntity *origin_entity)
    : a(a), b(b), len(len), origin_entity(origin_entity)
{}

ExtrusionLine::ExtrusionLine(const Vec2f &a, const Vec2f &b)
    : a(a), b(b), len((a-b).norm()), origin_entity(nullptr)
{}

bool ExtrusionLine::is_external_perimeter() const
{
    assert(origin_entity != nullptr);
    return origin_entity->role().is_external_perimeter();
}

using LD = AABBTreeLines::LinesDistancer<ExtrusionLine>;

struct SupportGridFilter
{
private:
    Vec3f cell_size;
    Vec3f origin;
    Vec3f size;
    Vec3i cell_count;

    std::unordered_set<size_t> taken_cells{};

public:
    SupportGridFilter(const PrintObject *po, float voxel_size)
    {
        cell_size = Vec3f(voxel_size, voxel_size, voxel_size);

        Vec2crd size_half = po->size().head<2>().cwiseQuotient(Vec2crd(2, 2)) + Vec2crd::Ones();
        Vec3f   min       = unscale(Vec3crd(-size_half.x(), -size_half.y(), 0)).cast<float>() - cell_size;
        Vec3f   max       = unscale(Vec3crd(size_half.x(), size_half.y(), po->height())).cast<float>() + cell_size;

        origin     = min;
        size       = max - min;
        cell_count = size.cwiseQuotient(cell_size).cast<int>() + Vec3i::Ones();
    }

    Vec3i to_cell_coords(const Vec3f &position) const
    {
        Vec3i cell_coords = (position - this->origin).cwiseQuotient(this->cell_size).cast<int>();
        return cell_coords;
    }

    size_t to_cell_index(const Vec3i &cell_coords) const
    {
#ifdef DETAILED_DEBUG_LOGS
        assert(cell_coords.x() >= 0);
        assert(cell_coords.x() < cell_count.x());
        assert(cell_coords.y() >= 0);
        assert(cell_coords.y() < cell_count.y());
        assert(cell_coords.z() >= 0);
        assert(cell_coords.z() < cell_count.z());
#endif
        return cell_coords.z() * cell_count.x() * cell_count.y() + cell_coords.y() * cell_count.x() + cell_coords.x();
    }

    Vec3f get_cell_center(const Vec3i &cell_coords) const
    {
        return origin + cell_coords.cast<float>().cwiseProduct(this->cell_size) + this->cell_size.cwiseQuotient(Vec3f(2.0f, 2.0f, 2.0f));
    }

    void take_position(const Vec3f &position) { taken_cells.insert(to_cell_index(to_cell_coords(position))); }

    bool position_taken(const Vec3f &position) const
    {
        return taken_cells.find(to_cell_index(to_cell_coords(position))) != taken_cells.end();
    }
};

void SliceConnection::add(const SliceConnection &other)
{
    this->area += other.area;
    this->centroid_accumulator += other.centroid_accumulator;
    this->second_moment_of_area_accumulator += other.second_moment_of_area_accumulator;
    this->second_moment_of_area_covariance_accumulator += other.second_moment_of_area_covariance_accumulator;
}

void SliceConnection::print_info(const std::string &tag) const
{
    Vec3f centroid   = centroid_accumulator / area;
    Vec2f variance   = (second_moment_of_area_accumulator / area - centroid.head<2>().cwiseProduct(centroid.head<2>()));
    float covariance = second_moment_of_area_covariance_accumulator / area - centroid.x() * centroid.y();
    std::cout << tag << std::endl;
    std::cout << "area: " << area << std::endl;
    std::cout << "centroid: " << centroid.x() << " " << centroid.y() << " " << centroid.z() << std::endl;
    std::cout << "variance: " << variance.x() << " " << variance.y() << std::endl;
    std::cout << "covariance: " << covariance << std::endl;
}

Integrals::Integrals(const Polygon &polygon)
{
    if (polygon.points.size() < 3) {
        assert(false && "Polygon is expected to have non-zero area!");
        *this = Integrals{};
        return;
    }
    Vec2f p0 = unscaled(polygon.first_point()).cast<float>();
    for (size_t i = 2; i < polygon.points.size(); i++) {
        Vec2f p1 = unscaled(polygon.points[i - 1]).cast<float>();
        Vec2f p2 = unscaled(polygon.points[i]).cast<float>();

        float sign = cross2(p1 - p0, p2 - p1) > 0 ? 1.0f : -1.0f;

        auto [area, first_moment_of_area, second_moment_area,
              second_moment_of_area_covariance] = compute_moments_of_area_of_triangle(p0, p1, p2);

        this->area += sign * area;
        this->x_i += sign * first_moment_of_area;
        this->x_i_squared += sign * second_moment_area;
        this->xy += sign * second_moment_of_area_covariance;
    }
}

Integrals::Integrals(const Polygons &polygons)
{
    for (const Polygon &polygon : polygons) {
        *this = *this + Integrals{polygon};
    }
}

Integrals::Integrals(const Polylines& polylines, const std::vector<float>& widths) {
    assert(polylines.size() == widths.size());
    for (size_t i = 0; i < polylines.size(); ++i) {
        Lines polyline{polylines[i].lines()};
        float width{widths[i]};
        for (const Line& line : polyline) {
            Vec2f line_direction = unscaled(line.vector()).cast<float>();
            Vec2f normal{line_direction.y(), -line_direction.x()};
            normal.normalize();

            Vec2f line_a = unscaled(line.a).cast<float>();
            Vec2f line_b = unscaled(line.b).cast<float>();
            Vec2crd a = scaled(Vec2f{line_a + normal * width/2});
            Vec2crd b = scaled(Vec2f{line_b + normal * width/2});
            Vec2crd c = scaled(Vec2f{line_b - normal * width/2});
            Vec2crd d = scaled(Vec2f{line_a - normal * width/2});

            const Polygon ractangle({a, b, c, d});
            Integrals integrals{ractangle};
            *this = *this + integrals;
        }
    }
}

Integrals::Integrals(float area, Vec2f x_i, Vec2f x_i_squared, float xy)
    : area(area), x_i(std::move(x_i)), x_i_squared(std::move(x_i_squared)), xy(xy)
{}

Integrals operator+(const Integrals &a, const Integrals &b)
{
    return Integrals{a.area + b.area, a.x_i + b.x_i, a.x_i_squared + b.x_i_squared, a.xy + b.xy};
}

SliceConnection estimate_slice_connection(size_t slice_idx, const Layer *layer)
{
    SliceConnection connection;

    const LayerSlice   &slice       = layer->lslices_ex[slice_idx];
    Polygons           slice_polys  = to_polygons(layer->lslices[slice_idx]);
    BoundingBox slice_bb = get_extents(slice_polys);
    const Layer        *lower_layer = layer->lower_layer;

    std::unordered_set<size_t> linked_slices_below;
    for (const auto &link : slice.overlaps_below) { linked_slices_below.insert(link.slice_idx); }

    ExPolygons below{};
    for (const auto &linked_slice_idx_below : linked_slices_below) { below.push_back(lower_layer->lslices[linked_slice_idx_below]); }
    Polygons below_polys = to_polygons(below);

    BoundingBox below_bb = get_extents(below_polys);

    Polygons overlap = intersection(ClipperUtils::clip_clipper_polygons_with_subject_bbox(slice_polys, below_bb),
                                    ClipperUtils::clip_clipper_polygons_with_subject_bbox(below_polys, slice_bb));

    const Integrals integrals{overlap};
    connection.area += integrals.area;
    connection.centroid_accumulator += Vec3f(integrals.x_i.x(), integrals.x_i.y(), layer->print_z * integrals.area);
    connection.second_moment_of_area_accumulator += integrals.x_i_squared;
    connection.second_moment_of_area_covariance_accumulator += integrals.xy;

    return connection;
};

using PrecomputedSliceConnections = std::vector<std::vector<SliceConnection>>;
PrecomputedSliceConnections precompute_slices_connections(const PrintObject *po)
{
    PrecomputedSliceConnections result{};
    for (size_t lidx = 0; lidx < po->layer_count(); lidx++) {
        result.emplace_back(std::vector<SliceConnection>{});
        for (size_t slice_idx = 0; slice_idx < po->get_layer(lidx)->lslices_ex.size(); slice_idx++) {
            result[lidx].push_back(SliceConnection{});
        }
    }

    tbb::parallel_for(tbb::blocked_range<size_t>(0, po->layers().size()), [po, &result](tbb::blocked_range<size_t> r) {
        for (size_t lidx = r.begin(); lidx < r.end(); lidx++) {
            const Layer *l = po->get_layer(lidx);
            tbb::parallel_for(tbb::blocked_range<size_t>(0, l->lslices_ex.size()), [lidx, l, &result](tbb::blocked_range<size_t> r2) {
                for (size_t slice_idx = r2.begin(); slice_idx < r2.end(); slice_idx++) {
                    result[lidx][slice_idx] = estimate_slice_connection(slice_idx, l);
                }
            });
        }
    });

    return result;
};

float get_flow_width(const LayerRegion *region, ExtrusionRole role)
{
    if (role == ExtrusionRole::BridgeInfill) return region->flow(FlowRole::frExternalPerimeter).width();
    if (role == ExtrusionRole::ExternalPerimeter) return region->flow(FlowRole::frExternalPerimeter).width();
    if (role == ExtrusionRole::GapFill) return region->flow(FlowRole::frInfill).width();
    if (role == ExtrusionRole::Perimeter) return region->flow(FlowRole::frPerimeter).width();
    if (role == ExtrusionRole::SolidInfill) return region->flow(FlowRole::frSolidInfill).width();
    if (role == ExtrusionRole::InfillOverBridge) return region->flow(FlowRole::frSolidInfill).width();
    if (role == ExtrusionRole::InternalInfill) return region->flow(FlowRole::frInfill).width();
    if (role == ExtrusionRole::TopSolidInfill) return region->flow(FlowRole::frTopSolidInfill).width();
    // default
    return region->flow(FlowRole::frPerimeter).width();
}

float estimate_curled_up_height(
    float distance, float curvature, float layer_height, float flow_width, float prev_line_curled_height, Params params)
{
    float curled_up_height = 0;
    if (fabs(distance) < 3.0 * flow_width) {
        curled_up_height = std::max(prev_line_curled_height - layer_height * 0.75f, 0.0f);
    }

    if (distance > params.malformation_distance_factors.first * flow_width &&
        distance < params.malformation_distance_factors.second * flow_width) {
        // imagine the extrusion profile. The part that has been glued (melted) with the previous layer will be called anchored section
        // and the rest will be called curling section
        // float anchored_section = flow_width - point.distance;
        float curling_section = distance;

        // after extruding, the curling (floating) part of the extrusion starts to shrink back to the rounded shape of the nozzle
        // The anchored part not, because the melted material holds to the previous layer well.
        // We can assume for simplicity perfect equalization of layer height and raising part width, from which:
        float swelling_radius = (layer_height + curling_section) / 2.0f;
        curled_up_height += std::max(0.f, (swelling_radius - layer_height) / 2.0f);

        // On convex turns, there is larger tension on the floating edge of the extrusion then on the middle section.
        // The tension is caused by the shrinking tendency of the filament, and on outer edge of convex trun, the expansion is greater and
        // thus shrinking force is greater. This tension will cause the curling section to curle up
        if (curvature > 0.01) {
            float radius    = (1.0 / curvature);
            float curling_t = sqrt(radius / 100);
            float b         = curling_t * flow_width;
            float a         = curling_section;
            float c         = sqrt(std::max(0.0f, a * a - b * b));

            curled_up_height += c;
        }
        curled_up_height = std::min(curled_up_height, params.max_curled_height_factor * layer_height);
    }

    return curled_up_height;
}

std::vector<ExtrusionLine> check_extrusion_entity_stability(const ExtrusionEntity                      *entity,
                                                            const LayerRegion                          *layer_region,
                                                            const LD                                   &prev_layer_lines,
                                                            const AABBTreeLines::LinesDistancer<Linef> &prev_layer_boundary,
                                                            const Params                               &params)
{
    assert(!entity->is_collection());
    if (entity->role().is_bridge() && !entity->role().is_perimeter()) {
        // pure bridges are handled separately, beacuse we need to align the forward and backward direction support points
        if (entity->length() < scale_(params.min_distance_to_allow_local_supports)) {
            return {};
        }
        const float                                    flow_width = get_flow_width(layer_region, entity->role());

        ExtrusionProcessor::PropertiesEstimationConfig config{};
        config.add_corners = true;
        config.prev_layer_boundary_offset = true;
        config.max_line_length = params.bridge_distance;
        config.flow_width = flow_width;

        std::vector<ExtrusionProcessor::ExtendedPoint> annotated_points =
            ExtrusionProcessor::estimate_points_properties<
                true>(entity->as_polyline().points, prev_layer_boundary, config);

        std::vector<ExtrusionLine> lines_out;
        lines_out.reserve(annotated_points.size());
        float bridged_distance = 0.0f;

        std::optional<Vec2d> bridging_dir{};

        for (size_t i = 0; i < annotated_points.size(); ++i) {
            ExtrusionProcessor::ExtendedPoint       &curr_point = annotated_points[i];
            const ExtrusionProcessor::ExtendedPoint &prev_point = i > 0 ? annotated_points[i - 1] : annotated_points[i];

            SupportPointCause potential_cause = std::abs(curr_point.curvature) > 0.1 ? SupportPointCause::FloatingBridgeAnchor :
                                                                                       SupportPointCause::LongBridge;
            float             line_len        = (prev_point.position - curr_point.position).norm();
            Vec2d line_dir = line_len > EPSILON ? Vec2d((curr_point.position - prev_point.position) / double(line_len)) : Vec2d::Zero();

            ExtrusionLine line_out{prev_point.position.cast<float>(), curr_point.position.cast<float>(), line_len, entity};

            float max_bridge_len = std::max(params.support_points_interface_radius * 2.0f,
                                            params.bridge_distance /
                                                ((1.0f + std::abs(curr_point.curvature)) * (1.0f + std::abs(curr_point.curvature)) *
                                                 (1.0f + std::abs(curr_point.curvature))));

            if (!bridging_dir.has_value() && curr_point.distance > flow_width && line_len > params.bridge_distance * 0.6) {
                bridging_dir = line_dir;
            }

            if (curr_point.distance > flow_width && potential_cause == SupportPointCause::LongBridge && bridging_dir.has_value() &&
                bridging_dir->dot(line_dir) < 0.8) { // skip backward direction of bridge - supported by forward points enough
                bridged_distance += line_len;
            } else if (curr_point.distance > flow_width) {
                bridged_distance += line_len;
                if (bridged_distance > max_bridge_len) {
                    bridged_distance                 = 0.0f;
                    line_out.support_point_generated = potential_cause;
                }
            } else {
                bridged_distance = 0.0f;
            }

            lines_out.push_back(line_out);
        }
        return lines_out;

    } else { // single extrusion path, with possible varying parameters
        if (entity->length() < scale_(params.min_distance_to_allow_local_supports)) {
            return {};
        }

        const float flow_width = get_flow_width(layer_region, entity->role());
        // Compute only unsigned distance - prev_layer_lines can contain unconnected paths, thus the sign of the distance is unreliable

        ExtrusionProcessor::PropertiesEstimationConfig config{};
        config.max_line_length = params.bridge_distance;
        config.flow_width = flow_width;
        std::vector<ExtrusionProcessor::ExtendedPoint> annotated_points =
            ExtrusionProcessor::estimate_points_properties<false>(entity->as_polyline().points, prev_layer_lines, config);

        std::vector<ExtrusionLine> lines_out;
        lines_out.reserve(annotated_points.size());
        float bridged_distance = annotated_points.front().position != annotated_points.back().position ? (params.bridge_distance + 1.0f) :
                                                                                                         0.0f;
        for (size_t i = 0; i < annotated_points.size(); ++i) {
            ExtrusionProcessor::ExtendedPoint       &curr_point = annotated_points[i];
            const ExtrusionProcessor::ExtendedPoint &prev_point = i > 0 ? annotated_points[i - 1] : annotated_points[i];
            float                                    line_len   = (prev_point.position - curr_point.position).norm();
            ExtrusionLine line_out{prev_point.position.cast<float>(), curr_point.position.cast<float>(), line_len, entity};

            Vec2f middle                               = 0.5 * (line_out.a + line_out.b);
            auto [middle_distance, bottom_line_idx, x] = prev_layer_lines.distance_from_lines_extra<false>(middle);
            ExtrusionLine bottom_line = prev_layer_lines.get_lines().empty() ? ExtrusionLine{} : prev_layer_lines.get_line(bottom_line_idx);

            // correctify the distance sign using slice polygons
            float sign = (prev_layer_boundary.distance_from_lines<true>(curr_point.position) + 0.5f * flow_width) < 0.0f ? -1.0f : 1.0f;
            curr_point.distance *= sign;

            SupportPointCause potential_cause = SupportPointCause::FloatingExtrusion;
            // Bridges are now separated. While long overhang perimeter is technically bridge, it would confuse the users
            // if (bridged_distance + line_len > params.bridge_distance * 0.8 && std::abs(curr_point.curvature) < 0.1) {
            //     potential_cause = SupportPointCause::FloatingExtrusion;
            // }

            float max_bridge_len = std::max(params.support_points_interface_radius * 2.0f,
                                            params.bridge_distance /
                                                ((1.0f + std::abs(curr_point.curvature)) * (1.0f + std::abs(curr_point.curvature)) *
                                                 (1.0f + std::abs(curr_point.curvature))));

            if (curr_point.distance > 1.2f * flow_width) {
                line_out.form_quality = 0.8f;
                bridged_distance += line_len;
                if (bridged_distance > max_bridge_len) {
                    line_out.support_point_generated = potential_cause;
                    bridged_distance                 = 0.0f;
                }
            } else if (curr_point.distance > flow_width * 0.8f) {
                bridged_distance += line_len;
                line_out.form_quality = bottom_line.form_quality - 0.3f;
                if (line_out.form_quality < 0 && bridged_distance > max_bridge_len) {
                    line_out.support_point_generated = potential_cause;
                    line_out.form_quality            = 0.5f;
                    bridged_distance                 = 0.0f;
                }
            } else {
                bridged_distance = 0.0f;
            }

            line_out.curled_up_height = estimate_curled_up_height(middle_distance, 0.5 * (prev_point.curvature + curr_point.curvature),
                                                                  layer_region->layer()->height, flow_width, bottom_line.curled_up_height,
                                                                  params);

            lines_out.push_back(line_out);
        }

        return lines_out;
    }
}

/**
 * Calculates the second moment of area over an arbitrary polygon.
 *
 * Important note: The calculated moment is for an axis with origin at
 * the polygon centroid!
 *
 * @param integrals Integrals over the polygon area.
 * @param axis_direction Direction of the rotation axis going through centroid.
 */
float compute_second_moment(
    const Integrals& integrals,
    const Vec2f& axis_direction
) {
    // Second moment of area for any axis intersecting coordinate system origin
    // can be evaluated using the second moments of area calculated for the coordinate
    // system axis and the moment product (int xy).
    // The equation is derived appling known formulas for the moment of inertia
    // to a plannar problem. One can reason about second moment
    // of area by by setting density to 1 in the moment of inertia formulas.
    const auto area = integrals.area;
    const auto I_xx = integrals.x_i_squared.y();
    const auto I_yy = integrals.x_i_squared.x();
    const auto I_xy = -integrals.xy;

    const Vec2f centroid = integrals.x_i / area;

    Matrix2f moment_tensor{};
    moment_tensor <<
        I_xx, I_xy,
        I_xy, I_yy;

    const float moment_at_0_0 = axis_direction.transpose() * moment_tensor * axis_direction;

    // Apply parallel axis theorem to move the moment to centroid
    using line_alg::distance_to_infinite_squared;

    const Linef axis_at_0_0 = {{0, 0}, axis_direction.cast<double>()};

    const double distance = distance_to_infinite_squared(axis_at_0_0, centroid.cast<double>());
    return moment_at_0_0 - area * distance;
}

ObjectPart::ObjectPart(
    const std::vector<const ExtrusionEntityCollection*>& extrusion_collections,
    const bool connected_to_bed,
    const coordf_t print_head_z,
    const coordf_t layer_height,
    const std::optional<Polygons>& brim
) {
    if (connected_to_bed) {
        this->connected_to_bed = true;
    }

    const auto bottom_z = print_head_z - layer_height;
    const auto center_z = print_head_z - layer_height / 2;

    for (const ExtrusionEntityCollection* collection : extrusion_collections) {
        if (collection->empty()) {
            continue;
        }

        for (const ExtrusionEntity* entity: collection->flatten()) {
            Polylines polylines;
            std::vector<float> widths;

            if (
                const auto* path = dynamic_cast<const ExtrusionPath*>(entity);
                path != nullptr
            ) {
                polylines.push_back(path->as_polyline());
                widths.push_back(path->width());
            } else if (
                const auto* loop = dynamic_cast<const ExtrusionLoop*>(entity);
                loop != nullptr
            ) {
                for (const ExtrusionPath& path : loop->paths) {
                    polylines.push_back(path.as_polyline());
                    widths.push_back(path.width());
                }
            } else if (
                const auto* multi_path = dynamic_cast<const ExtrusionMultiPath*>(entity);
                multi_path != nullptr
            ) {
                for (const ExtrusionPath& path : multi_path->paths) {
                    polylines.push_back(path.as_polyline());
                    widths.push_back(path.width());
                }
            } else {
                throw std::runtime_error(
                    "Failed to construct object part from extrusions!"
                    " Unknown extrusion type."
                );
            }

            const Integrals integrals{polylines, widths};
            const float volume = integrals.area * layer_height;
            this->volume += volume;
            this->volume_centroid_accumulator += to_3d(integrals.x_i, center_z * integrals.area) / integrals.area * volume;

            if (this->connected_to_bed) {
                this->sticking_area += integrals.area;
                this->sticking_centroid_accumulator += to_3d(integrals.x_i, bottom_z * integrals.area);
                this->sticking_second_moment_of_area_accumulator += integrals.x_i_squared;
                this->sticking_second_moment_of_area_covariance_accumulator += integrals.xy;
            }
        }
    }

    if (brim) {
        Integrals integrals{*brim};
        this->sticking_area += integrals.area;
        this->sticking_centroid_accumulator += to_3d(integrals.x_i, bottom_z * integrals.area);
        this->sticking_second_moment_of_area_accumulator += integrals.x_i_squared;
        this->sticking_second_moment_of_area_covariance_accumulator += integrals.xy;
    }
}

void ObjectPart::add(const ObjectPart &other)
{
    this->connected_to_bed = this->connected_to_bed || other.connected_to_bed;
    this->volume_centroid_accumulator += other.volume_centroid_accumulator;
    this->volume += other.volume;
    this->sticking_area += other.sticking_area;
    this->sticking_centroid_accumulator += other.sticking_centroid_accumulator;
    this->sticking_second_moment_of_area_accumulator += other.sticking_second_moment_of_area_accumulator;
    this->sticking_second_moment_of_area_covariance_accumulator += other.sticking_second_moment_of_area_covariance_accumulator;
}

void ObjectPart::add_support_point(const Vec3f &position, float sticking_area)
{
    this->sticking_area += sticking_area;
    this->sticking_centroid_accumulator += sticking_area * position;
    this->sticking_second_moment_of_area_accumulator += sticking_area * position.head<2>().cwiseProduct(position.head<2>());
    this->sticking_second_moment_of_area_covariance_accumulator += sticking_area * position.x() * position.y();
}


float ObjectPart::compute_elastic_section_modulus(
    const Vec2f &line_dir,
    const Vec3f &extreme_point,
    const Integrals& integrals
) const {
    float second_moment_of_area = compute_second_moment(integrals, Vec2f{-line_dir.y(), line_dir.x()});

    if (second_moment_of_area < EPSILON) { return 0.0f; }

    Vec2f centroid                = integrals.x_i / integrals.area;
    float extreme_fiber_dist      = line_alg::distance_to(Linef(centroid.head<2>().cast<double>(),
                                                                (centroid.head<2>() + Vec2f(line_dir.y(), -line_dir.x())).cast<double>()),
                                                          extreme_point.head<2>().cast<double>());

    float elastic_section_modulus = second_moment_of_area / extreme_fiber_dist;

#ifdef DETAILED_DEBUG_LOGS
    BOOST_LOG_TRIVIAL(debug) << "extreme_fiber_dist: " << extreme_fiber_dist;
    BOOST_LOG_TRIVIAL(debug) << "elastic_section_modulus: " << elastic_section_modulus;
#endif

    return elastic_section_modulus;
}

std::tuple<float, SupportPointCause> ObjectPart::is_stable_while_extruding(const SliceConnection &connection,
                                const ExtrusionLine   &extruded_line,
                                const Vec3f           &extreme_point,
                                float                  layer_z,
                                const Params          &params) const
{
    // Note that exteme point is calculated for the current layer, while it should
    // be computed for the first layer. The shape of the first layer however changes a lot,
    // during support points additions (for organic supports it is not even clear how)
    // and during merging. Using the current layer is heuristics and also small optimization,
    // as the AABB tree for it is calculated anyways. This heuristic should usually be
    // on the safe side.
    Vec2f        line_dir      = (extruded_line.b - extruded_line.a).normalized();
    const Vec3f &mass_centroid = this->volume_centroid_accumulator / this->volume;
    float        mass          = this->volume * params.filament_density;
    float        weight        = mass * params.gravity_constant;

    float movement_force = params.max_acceleration * mass;

    float extruder_conflict_force = params.standard_extruder_conflict_force +
                                    std::min(extruded_line.curled_up_height, 1.0f) * params.malformations_additive_conflict_extruder_force;

    // section for bed calculations
    {
        if (this->sticking_area < EPSILON) return {1.0f, SupportPointCause::UnstableFloatingPart};

        Integrals integrals;
        integrals.area = this->sticking_area;
        integrals.x_i = this->sticking_centroid_accumulator.head<2>();
        integrals.x_i_squared = this->sticking_second_moment_of_area_accumulator;
        integrals.xy = this->sticking_second_moment_of_area_covariance_accumulator;

        Vec3f bed_centroid     = this->sticking_centroid_accumulator / this->sticking_area;
        float bed_yield_torque = -compute_elastic_section_modulus(line_dir, extreme_point, integrals) * params.get_bed_adhesion_yield_strength();

        Vec2f bed_weight_arm             = (mass_centroid.head<2>() - bed_centroid.head<2>());
        float bed_weight_arm_len         = bed_weight_arm.norm();

        float bed_weight_dir_xy_variance = compute_second_moment(integrals, {-bed_weight_arm.y(), bed_weight_arm.x()}) / this->sticking_area;
        float bed_weight_sign            = bed_weight_arm_len < 2.0f * sqrt(bed_weight_dir_xy_variance) ? -1.0f : 1.0f;
        float bed_weight_torque          = bed_weight_sign * bed_weight_arm_len * weight;

        float bed_movement_arm    = std::max(0.0f, mass_centroid.z() - bed_centroid.z());
        float bed_movement_torque = movement_force * bed_movement_arm;

        float bed_conflict_torque_arm      = layer_z - bed_centroid.z();
        float bed_extruder_conflict_torque = extruder_conflict_force * bed_conflict_torque_arm;

        float bed_total_torque = bed_movement_torque + bed_extruder_conflict_torque + bed_weight_torque + bed_yield_torque;

#ifdef DETAILED_DEBUG_LOGS
        BOOST_LOG_TRIVIAL(debug) << "bed_centroid: " << bed_centroid.x() << "  " << bed_centroid.y() << "  " << bed_centroid.z();
        BOOST_LOG_TRIVIAL(debug) << "SSG: bed_yield_torque: " << bed_yield_torque;
        BOOST_LOG_TRIVIAL(debug) << "SSG: bed_weight_arm: " << bed_weight_arm_len;
        BOOST_LOG_TRIVIAL(debug) << "SSG: bed_weight_torque: " << bed_weight_torque;
        BOOST_LOG_TRIVIAL(debug) << "SSG: bed_movement_arm: " << bed_movement_arm;
        BOOST_LOG_TRIVIAL(debug) << "SSG: bed_movement_torque: " << bed_movement_torque;
        BOOST_LOG_TRIVIAL(debug) << "SSG: bed_conflict_torque_arm: " << bed_conflict_torque_arm;
        BOOST_LOG_TRIVIAL(debug) << "SSG: extruded_line.curled_up_height: " << extruded_line.curled_up_height;
        BOOST_LOG_TRIVIAL(debug) << "SSG: extruded_line.form_quality: " << extruded_line.form_quality;
        BOOST_LOG_TRIVIAL(debug) << "SSG: extruder_conflict_force: " << extruder_conflict_force;
        BOOST_LOG_TRIVIAL(debug) << "SSG: bed_extruder_conflict_torque: " << bed_extruder_conflict_torque;
        BOOST_LOG_TRIVIAL(debug) << "SSG: total_torque: " << bed_total_torque << "   layer_z: " << layer_z;
#endif

        if (bed_total_torque > 0) {
            return {bed_total_torque / bed_conflict_torque_arm,
                    (this->connected_to_bed ? SupportPointCause::SeparationFromBed : SupportPointCause::UnstableFloatingPart)};
        }
    }

    // section for weak connection calculations
    {
        if (connection.area < EPSILON) return {1.0f, SupportPointCause::UnstableFloatingPart};

        Vec3f conn_centroid = connection.centroid_accumulator / connection.area;

        if (layer_z - conn_centroid.z() < 3.0f) { return {-1.0f, SupportPointCause::WeakObjectPart}; }

        Integrals integrals;
        integrals.area = connection.area;
        integrals.x_i = connection.centroid_accumulator.head<2>();
        integrals.x_i_squared = connection.second_moment_of_area_accumulator;
        integrals.xy = connection.second_moment_of_area_covariance_accumulator;

        float conn_yield_torque = compute_elastic_section_modulus(line_dir, extreme_point, integrals) * params.material_yield_strength;

        float conn_weight_arm    = (conn_centroid.head<2>() - mass_centroid.head<2>()).norm();
        if (layer_z - conn_centroid.z() < 30.0) {
            conn_weight_arm = 0.0f; // Given that we do not have very good info about the weight distribution between the connection and current layer,
            // do not consider the weight until quite far away from the weak connection segment
        }
        float conn_weight_torque = conn_weight_arm * weight * (1.0f - conn_centroid.z() / layer_z) * (1.0f - conn_centroid.z() / layer_z);

        float conn_movement_arm    = std::max(0.0f, mass_centroid.z() - conn_centroid.z());
        float conn_movement_torque = movement_force * conn_movement_arm;

        float conn_conflict_torque_arm      = layer_z - conn_centroid.z();
        float conn_extruder_conflict_torque = extruder_conflict_force * conn_conflict_torque_arm;

        float conn_total_torque = conn_movement_torque + conn_extruder_conflict_torque + conn_weight_torque - conn_yield_torque;

#ifdef DETAILED_DEBUG_LOGS
        BOOST_LOG_TRIVIAL(debug) << "conn_centroid: " << conn_centroid.x() << "  " << conn_centroid.y() << "  " << conn_centroid.z();
        BOOST_LOG_TRIVIAL(debug) << "SSG: conn_yield_torque: " << conn_yield_torque;
        BOOST_LOG_TRIVIAL(debug) << "SSG: conn_weight_arm: " << conn_weight_arm;
        BOOST_LOG_TRIVIAL(debug) << "SSG: conn_weight_torque: " << conn_weight_torque;
        BOOST_LOG_TRIVIAL(debug) << "SSG: conn_movement_arm: " << conn_movement_arm;
        BOOST_LOG_TRIVIAL(debug) << "SSG: conn_movement_torque: " << conn_movement_torque;
        BOOST_LOG_TRIVIAL(debug) << "SSG: conn_conflict_torque_arm: " << conn_conflict_torque_arm;
        BOOST_LOG_TRIVIAL(debug) << "SSG: conn_extruder_conflict_torque: " << conn_extruder_conflict_torque;
        BOOST_LOG_TRIVIAL(debug) << "SSG: total_torque: " << conn_total_torque << "   layer_z: " << layer_z;
#endif

        return {conn_total_torque / conn_conflict_torque_arm, SupportPointCause::WeakObjectPart};
    }
}

std::vector<const ExtrusionEntityCollection*> gather_extrusions(const LayerSlice& slice, const Layer* layer) {
    // TODO reserve might be good, benchmark
    std::vector<const ExtrusionEntityCollection*> result;

    for (const auto &island : slice.islands) {
        const LayerRegion *perimeter_region = layer->get_region(island.perimeters.region());
        for (size_t perimeter_idx : island.perimeters) {
            auto collection = static_cast<const ExtrusionEntityCollection *>(
                perimeter_region->perimeters().entities[perimeter_idx]
            );
            result.push_back(collection);
        }
        for (const LayerExtrusionRange &fill_range : island.fills) {
            const LayerRegion *fill_region = layer->get_region(fill_range.region());
            for (size_t fill_idx : fill_range) {
                auto collection = static_cast<const ExtrusionEntityCollection *>(
                   fill_region->fills().entities[fill_idx]
                );
                result.push_back(collection);
            }
        }
        const ExtrusionEntityCollection& collection = perimeter_region->thin_fills();
        result.push_back(&collection);
    }
    return result;
}

bool has_brim(const Layer* layer, const Params& params){
    return
        int(layer->id()) == params.raft_layers_count
        && params.raft_layers_count == 0
        && params.brim_type != BrimType::btNoBrim
        && params.brim_width > 0.0;
}


Polygons get_brim(const ExPolygon& slice_polygon, const BrimType brim_type, const float brim_width) {
    // TODO: The algorithm here should take into account that multiple slices may
    // have coliding Brim areas and the final brim area is smaller,
    // thus has lower adhesion. For now this effect will be neglected.
    ExPolygons brim;
    if (brim_type == BrimType::btOuterAndInner || brim_type == BrimType::btOuterOnly) {
        Polygon brim_hole = slice_polygon.contour;
        brim_hole.reverse();
        Polygons c = expand(slice_polygon.contour, scale_(brim_width)); // For very small polygons, the expand may result in empty vector, even thought the input is correct.
        if (!c.empty()) {
            brim.push_back(ExPolygon{c.front(), brim_hole});
        }
    }
    if (brim_type == BrimType::btOuterAndInner || brim_type == BrimType::btInnerOnly) {
        Polygons brim_contours = slice_polygon.holes;
        polygons_reverse(brim_contours);
        for (const Polygon &brim_contour : brim_contours) {
            Polygons brim_holes = shrink({brim_contour}, scale_(brim_width));
            polygons_reverse(brim_holes);
            ExPolygon inner_brim{brim_contour};
            inner_brim.holes = brim_holes;
            brim.push_back(inner_brim);
        }
    }
    return to_polygons(brim);
}

class ActiveObjectParts
{
    size_t                                 next_part_idx = 0;
    std::unordered_map<size_t, ObjectPart> active_object_parts;
    std::unordered_map<size_t, size_t>     active_object_parts_id_mapping;

public:
    size_t get_flat_id(size_t id)
    {
        size_t index = active_object_parts_id_mapping.at(id);
        while (index != active_object_parts_id_mapping.at(index)) { index = active_object_parts_id_mapping.at(index); }
        size_t i = id;
        while (index != active_object_parts_id_mapping.at(i)) {
            size_t next                       = active_object_parts_id_mapping[i];
            active_object_parts_id_mapping[i] = index;
            i                                 = next;
        }
        return index;
    }

    ObjectPart &access(size_t id) { return this->active_object_parts.at(this->get_flat_id(id)); }

    size_t insert(const ObjectPart &new_part)
    {
        this->active_object_parts.emplace(next_part_idx, new_part);
        this->active_object_parts_id_mapping.emplace(next_part_idx, next_part_idx);
        return next_part_idx++;
    }

    void merge(size_t from, size_t to)
    {
        size_t to_flat   = this->get_flat_id(to);
        size_t from_flat = this->get_flat_id(from);
        active_object_parts.at(to_flat).add(active_object_parts.at(from_flat));
        active_object_parts.erase(from_flat);
        active_object_parts_id_mapping[from] = to_flat;
    }
};

// Function that is used when new support point is generated. It will update the ObjectPart stability, weakest conneciton info,
// and the support presence grid and add the point to the issues.
void reckon_new_support_point(ObjectPart        &part,
                              SliceConnection   &weakest_conn,
                              SupportPoints     &supp_points,
                              SupportGridFilter &supports_presence_grid,
                              const SupportPoint& support_point,
                              bool               is_global = false)
{
    // if position is taken and point is for global stability (force > 0) or we are too close to the bed, do not add
    // This allows local support points (e.g. bridging) to be generated densely
    if ((supports_presence_grid.position_taken(support_point.position) && is_global)) {
        return;
    }

    float area = support_point.spot_radius * support_point.spot_radius * float(PI);
    // add the stability effect of the point only if the spot is not taken, so that the densely created local support points do
    // not add unrealistic amount of stability to the object (due to overlaping of local support points)
    if (!(supports_presence_grid.position_taken(support_point.position))) {
        part.add_support_point(support_point.position, area);
    }

    supp_points.push_back(support_point);
    supports_presence_grid.take_position(support_point.position);

    // The support point also increases the stability of the weakest connection of the object, which should be reflected
    if (weakest_conn.area > EPSILON) { // Do not add it to the weakest connection if it is not valid - does not exist
        weakest_conn.area += area;
        weakest_conn.centroid_accumulator += support_point.position * area;
        weakest_conn.second_moment_of_area_accumulator += area *
                                                          support_point.position.head<2>().cwiseProduct(support_point.position.head<2>());
        weakest_conn.second_moment_of_area_covariance_accumulator += area * support_point.position.x() * support_point.position.y();
    }
}

struct LocalSupports {
    std::vector<tbb::concurrent_vector<ExtrusionLine>> unstable_lines_per_slice;
    std::vector<tbb::concurrent_vector<ExtrusionLine>> ext_perim_lines_per_slice;
};

struct EnitityToCheck
{
    const ExtrusionEntity *e;
    const LayerRegion     *region;
    size_t                 slice_idx;
};

// TODO DRY: Very similar to gather extrusions.
std::vector<EnitityToCheck> gather_entities_to_check(const Layer* layer) {
    auto get_flat_entities = [](const ExtrusionEntity *e) {
        std::vector<const ExtrusionEntity *> entities;
        std::vector<const ExtrusionEntity *> queue{e};
        while (!queue.empty()) {
            const ExtrusionEntity *next = queue.back();
            queue.pop_back();
            if (next->is_collection()) {
                for (const ExtrusionEntity *e : static_cast<const ExtrusionEntityCollection *>(next)->entities) {
                    queue.push_back(e);
                }
            } else {
                entities.push_back(next);
            }
        }
        return entities;
    };

    std::vector<EnitityToCheck> entities_to_check;
    for (size_t slice_idx = 0; slice_idx < layer->lslices_ex.size(); ++slice_idx) {
        const LayerSlice &slice = layer->lslices_ex.at(slice_idx);
        for (const auto &island : slice.islands) {
            for (const LayerExtrusionRange &fill_range : island.fills) {
                const LayerRegion *fill_region = layer->get_region(fill_range.region());
                for (size_t fill_idx : fill_range) {
                    for (const ExtrusionEntity *e : get_flat_entities(fill_region->fills().entities[fill_idx])) {
                        if (e->role() == ExtrusionRole::BridgeInfill) {
                            entities_to_check.push_back({e, fill_region, slice_idx});
                        }
                    }
                }
            }

            const LayerRegion *perimeter_region = layer->get_region(island.perimeters.region());
            for (size_t perimeter_idx : island.perimeters) {
                for (const ExtrusionEntity *e : get_flat_entities(perimeter_region->perimeters().entities[perimeter_idx])) {
                    entities_to_check.push_back({e, perimeter_region, slice_idx});
                }
            }
        }
    }
    return entities_to_check;
}

LocalSupports compute_local_supports(
    const std::vector<EnitityToCheck>& entities_to_check,
    const std::optional<Linesf>& previous_layer_boundary,
    const LD& prev_layer_ext_perim_lines,
    size_t slices_count,
    const Params& params
) {
    std::vector<tbb::concurrent_vector<ExtrusionLine>> unstable_lines_per_slice(slices_count);
    std::vector<tbb::concurrent_vector<ExtrusionLine>> ext_perim_lines_per_slice(slices_count);

    AABBTreeLines::LinesDistancer<Linef> prev_layer_boundary_distancer =
        (previous_layer_boundary ? AABBTreeLines::LinesDistancer<Linef>{*previous_layer_boundary} : AABBTreeLines::LinesDistancer<Linef>{});

    if constexpr (debug_files) {
        for (const auto &e_to_check : entities_to_check) {
            for (const auto &line : check_extrusion_entity_stability(e_to_check.e, e_to_check.region, prev_layer_ext_perim_lines,
                                                                     prev_layer_boundary_distancer, params)) {
                if (line.support_point_generated.has_value()) {
                    unstable_lines_per_slice[e_to_check.slice_idx].push_back(line);
                }
                if (line.is_external_perimeter()) {
                    ext_perim_lines_per_slice[e_to_check.slice_idx].push_back(line);
                }
            }
        }
    } else {
        tbb::parallel_for(tbb::blocked_range<size_t>(0, entities_to_check.size()),
                          [&entities_to_check, &prev_layer_ext_perim_lines, &prev_layer_boundary_distancer, &unstable_lines_per_slice,
                           &ext_perim_lines_per_slice, &params](tbb::blocked_range<size_t> r) {
                              for (size_t entity_idx = r.begin(); entity_idx < r.end(); ++entity_idx) {
                                  const auto &e_to_check = entities_to_check[entity_idx];
                                  for (const auto &line :
                                       check_extrusion_entity_stability(e_to_check.e, e_to_check.region, prev_layer_ext_perim_lines,
                                                                        prev_layer_boundary_distancer, params)) {
                                      if (line.support_point_generated.has_value()) {
                                          unstable_lines_per_slice[e_to_check.slice_idx].push_back(line);
                                      }
                                      if (line.is_external_perimeter()) {
                                          ext_perim_lines_per_slice[e_to_check.slice_idx].push_back(line);
                                      }
                                  }
                              }
                          });
    }
    return {unstable_lines_per_slice, ext_perim_lines_per_slice};
}

struct SliceMappings
{
    std::unordered_map<size_t, size_t>          index_to_object_part_mapping;
    std::unordered_map<size_t, SliceConnection> index_to_weakest_connection;
};

std::optional<PartialObject> to_partial_object(const ObjectPart& part) {
    if (part.volume > EPSILON) {
        return PartialObject{part.volume_centroid_accumulator / part.volume, part.volume,
                                     part.connected_to_bed};
    }
    return {};
}

SliceMappings update_active_object_parts(const Layer                        *layer,
                                         const Params                       &params,
                                         const std::vector<SliceConnection> &precomputed_slice_connections,
                                         const SliceMappings                &previous_slice_mappings,
                                         ActiveObjectParts                  &active_object_parts,
                                         PartialObjects                     &partial_objects)
{
    SliceMappings new_slice_mappings;

    for (size_t slice_idx = 0; slice_idx < layer->lslices_ex.size(); ++slice_idx) {
        const LayerSlice &slice             = layer->lslices_ex.at(slice_idx);
        const std::vector<const ExtrusionEntityCollection*> extrusion_collections{gather_extrusions(slice, layer)};
        const bool connected_to_bed = int(layer->id()) == params.raft_layers_count;

        const std::optional<Polygons> brim{
             has_brim(layer, params) ?
             std::optional{get_brim(layer->lslices[slice_idx], params.brim_type, params.brim_width)} :
             std::nullopt
        };
        ObjectPart new_part{
            extrusion_collections,
            connected_to_bed,
            layer->print_z,
            layer->height,
            brim
        };

        const SliceConnection &connection_to_below = precomputed_slice_connections[slice_idx];

#ifdef DETAILED_DEBUG_LOGS
        std::cout << "SLICE IDX: " << slice_idx << std::endl;
        for (const auto &link : slice.overlaps_below) {
            std::cout << "connected to slice below: " << link.slice_idx << "  by area : " << link.area << std::endl;
        }
        connection_to_below.print_info("CONNECTION TO BELOW");
#endif

        if (connection_to_below.area < EPSILON) { // new object part emerging
            size_t part_id = active_object_parts.insert(new_part);
            new_slice_mappings.index_to_object_part_mapping.emplace(slice_idx, part_id);
            new_slice_mappings.index_to_weakest_connection.emplace(slice_idx, connection_to_below);
        } else {
            size_t          final_part_id{};
            SliceConnection transfered_weakest_connection{};
            // MERGE parts
            {
                std::unordered_set<size_t> parts_ids;
                for (const auto &link : slice.overlaps_below) {
                    size_t part_id = active_object_parts.get_flat_id(previous_slice_mappings.index_to_object_part_mapping.at(link.slice_idx));
                    parts_ids.insert(part_id);
                    transfered_weakest_connection.add(previous_slice_mappings.index_to_weakest_connection.at(link.slice_idx));
                }

                final_part_id = *parts_ids.begin();
                for (size_t part_id : parts_ids) {
                    if (final_part_id != part_id) {
                        auto object_part = active_object_parts.access(part_id);
                        if (auto object = to_partial_object(object_part)) {
                            partial_objects.push_back(std::move(*object));
                        }
                        active_object_parts.merge(part_id, final_part_id);
                    }
                }
            }
            const float bottom_z = layer->bottom_z();
            auto estimate_conn_strength = [bottom_z](const SliceConnection &conn) {
                if (conn.area < EPSILON) { // connection is empty, does not exists. Return max strength so that it is not picked as the
                                           // weakest connection.
                    return INFINITY;
                }
                Vec3f centroid         = conn.centroid_accumulator / conn.area;
                Vec2f variance         = (conn.second_moment_of_area_accumulator / conn.area -
                                  centroid.head<2>().cwiseProduct(centroid.head<2>()));
                float xy_variance      = variance.x() + variance.y();
                float arm_len_estimate = std::max(1.0f, bottom_z - (conn.centroid_accumulator.z() / conn.area));
                return conn.area * sqrt(xy_variance) / arm_len_estimate;
            };

#ifdef DETAILED_DEBUG_LOGS
            connection_to_below.print_info("new_weakest_connection");
            transfered_weakest_connection.print_info("transfered_weakest_connection");
#endif

            if (estimate_conn_strength(transfered_weakest_connection) > estimate_conn_strength(connection_to_below)) {
                transfered_weakest_connection = connection_to_below;
            }
            new_slice_mappings.index_to_weakest_connection.emplace(slice_idx, transfered_weakest_connection);
            new_slice_mappings.index_to_object_part_mapping.emplace(slice_idx, final_part_id);
            ObjectPart &part = active_object_parts.access(final_part_id);
            part.add(new_part);
        }
    }
    return new_slice_mappings;
}

void reckon_global_supports(const tbb::concurrent_vector<ExtrusionLine> &external_perimeter_lines,
                            const coordf_t                               layer_bottom_z,
                            const Params                                &params,
                            ObjectPart                                  &part,
                            SliceConnection                             &weakest_connection,
                            SupportPoints                               &supp_points,
                            SupportGridFilter                           &supports_presence_grid)
{
    LD    current_slice_lines_distancer({external_perimeter_lines.begin(), external_perimeter_lines.end()});
    float unchecked_dist = params.min_distance_between_support_points + 1.0f;

    for (const ExtrusionLine &line : external_perimeter_lines) {
        if ((unchecked_dist + line.len < params.min_distance_between_support_points &&
             line.curled_up_height < params.curling_tolerance_limit) ||
            line.len < EPSILON) {
            unchecked_dist += line.len;
        } else {
            unchecked_dist                   = line.len;
            Vec2f pivot_site_search_point    = Vec2f(line.b + (line.b - line.a).normalized() * 300.0f);
            auto [dist, nidx, nearest_point] = current_slice_lines_distancer.distance_from_lines_extra<false>(pivot_site_search_point);
            Vec3f position                   = to_3d(nearest_point, layer_bottom_z);
            auto [force, cause]              = part.is_stable_while_extruding(weakest_connection, line, position, layer_bottom_z, params);
            if (force > 0) {
                SupportPoint support_point{cause, position, params.support_points_interface_radius};
                reckon_new_support_point(part, weakest_connection, supp_points, supports_presence_grid, support_point, true);
            }
        }
    }
}

std::tuple<SupportPoints, PartialObjects> check_stability(const PrintObject                 *po,
                                                          const PrecomputedSliceConnections &precomputed_slices_connections,
                                                          const PrintTryCancel              &cancel_func,
                                                          const Params                      &params)
{
    SupportPoints     supp_points{};
    SupportGridFilter supports_presence_grid(po, params.min_distance_between_support_points);
    ActiveObjectParts active_object_parts{};
    PartialObjects    partial_objects{};
    LD                prev_layer_ext_perim_lines;

    SliceMappings slice_mappings;


    for (size_t layer_idx = 0; layer_idx < po->layer_count(); ++layer_idx) {
        cancel_func();
        const Layer *layer                 = po->get_layer(layer_idx);
        float        bottom_z              = layer->bottom_z();

        slice_mappings = update_active_object_parts(layer, params, precomputed_slices_connections[layer_idx], slice_mappings, active_object_parts, partial_objects);

        std::optional<Linesf> prev_layer_boundary = layer->lower_layer != nullptr ?
                                                        std::optional{to_unscaled_linesf(layer->lower_layer->lslices)} :
                                                        std::nullopt;

        LocalSupports local_supports{
            compute_local_supports(gather_entities_to_check(layer), prev_layer_boundary, prev_layer_ext_perim_lines, layer->lslices_ex.size(), params)};

        std::vector<ExtrusionLine> current_layer_ext_perims_lines{};
        current_layer_ext_perims_lines.reserve(prev_layer_ext_perim_lines.get_lines().size());
        // All object parts updated, and for each slice we have coresponding weakest connection.
        // We can now check each slice and its corresponding weakest connection and object part for stability.
        for (size_t slice_idx = 0; slice_idx < layer->lslices_ex.size(); ++slice_idx) {
            ObjectPart                &part         = active_object_parts.access(slice_mappings.index_to_object_part_mapping[slice_idx]);
            SliceConnection           &weakest_conn = slice_mappings.index_to_weakest_connection[slice_idx];

            if (layer_idx > 1) {
                for (const auto &l : local_supports.unstable_lines_per_slice[slice_idx]) {
                    assert(l.support_point_generated.has_value());
                    SupportPoint support_point{*l.support_point_generated, to_3d(l.b, bottom_z),
                                               params.support_points_interface_radius};
                    reckon_new_support_point(part, weakest_conn, supp_points, supports_presence_grid, support_point);
                }
            }

            const tbb::concurrent_vector<ExtrusionLine> &external_perimeter_lines = local_supports.ext_perim_lines_per_slice[slice_idx];
            if (layer_idx > 1) {
                reckon_global_supports(external_perimeter_lines, bottom_z, params, part, weakest_conn, supp_points, supports_presence_grid);
            }

            current_layer_ext_perims_lines.insert(current_layer_ext_perims_lines.end(), external_perimeter_lines.begin(), external_perimeter_lines.end());
        } // slice iterations
        prev_layer_ext_perim_lines = LD(current_layer_ext_perims_lines);
    } // layer iterations

    for (const auto& active_obj_pair : slice_mappings.index_to_object_part_mapping) {
        auto object_part = active_object_parts.access(active_obj_pair.second);
        if (auto object = to_partial_object(object_part)) {
            partial_objects.push_back(std::move(*object));
        }
    }

    return {supp_points, partial_objects};
}

#ifdef DEBUG_FILES
void debug_export(const SupportPoints& support_points,const PartialObjects& objects, std::string file_name)
{
    Slic3r::CNumericLocalesSetter locales_setter;
    {
        FILE *fp = boost::nowide::fopen(debug_out_path((file_name + "_supports.obj").c_str()).c_str(), "w");
        if (fp == nullptr) {
            BOOST_LOG_TRIVIAL(error) << "Debug files: Couldn't open " << file_name << " for writing";
            return;
        }

        for (size_t i = 0; i < support_points.size(); ++i) {
            Vec3f color{1.0f, 1.0f, 1.0f};
            switch (support_points[i].cause) {
            case SupportPointCause::FloatingBridgeAnchor: color = {0.863281f, 0.109375f, 0.113281f}; break; //RED
            case SupportPointCause::LongBridge: color = {0.960938f, 0.90625f, 0.0625f}; break;  // YELLOW
            case SupportPointCause::FloatingExtrusion: color = {0.921875f, 0.515625f, 0.101563f}; break; // ORANGE
            case SupportPointCause::SeparationFromBed: color = {0.0f, 1.0f, 0.0}; break; // GREEN
            case SupportPointCause::UnstableFloatingPart: color = {0.105469f, 0.699219f, 0.84375f}; break; // BLUE
            case SupportPointCause::WeakObjectPart: color = {0.609375f, 0.210938f, 0.621094f}; break; // PURPLE
            }

            fprintf(fp, "v %f %f %f  %f %f %f\n", support_points[i].position(0), support_points[i].position(1),
                    support_points[i].position(2), color[0], color[1], color[2]);
        }

        for (size_t i = 0; i < objects.size(); ++i) {
            Vec3f color{1.0f, 0.0f, 1.0f};
            if (objects[i].connected_to_bed) {
                color = {1.0f, 0.0f, 0.0f};
            }
            fprintf(fp, "v %f %f %f  %f %f %f\n", objects[i].centroid(0), objects[i].centroid(1), objects[i].centroid(2), color[0],
                    color[1], color[2]);
        }

        fclose(fp);
    }
}
#endif

std::tuple<SupportPoints, PartialObjects> full_search(const PrintObject *po, const PrintTryCancel& cancel_func, const Params &params)
{
    auto precomputed_slices_connections = precompute_slices_connections(po);
    auto results = check_stability(po, precomputed_slices_connections, cancel_func, params);
#ifdef DEBUG_FILES
    auto [supp_points, objects] = results;
    debug_export(supp_points, objects, "issues");
#endif

    return results;
}

void estimate_supports_malformations(SupportLayerPtrs &layers, float flow_width, const Params &params)
{
#ifdef DEBUG_FILES
    FILE *debug_file = boost::nowide::fopen(debug_out_path("supports_malformations.obj").c_str(), "w");
    FILE *full_file = boost::nowide::fopen(debug_out_path("supports_full.obj").c_str(), "w");
#endif

    AABBTreeLines::LinesDistancer<ExtrusionLine> prev_layer_lines{};

    for (SupportLayer *l : layers) {
        l->curled_lines.clear();
        std::vector<ExtrusionLine> current_layer_lines;

        for (const ExtrusionEntity *extrusion : l->support_fills.flatten().entities) {
            Polyline pl = extrusion->as_polyline();
            Polygon  pol(pl.points);
            pol.make_counter_clockwise();

            ExtrusionProcessor::PropertiesEstimationConfig config{};
            config.flow_width = flow_width;
            auto annotated_points = ExtrusionProcessor::estimate_points_properties<
                false>(pol.points, prev_layer_lines, config);

            for (size_t i = 0; i < annotated_points.size(); ++i) {
                const ExtrusionProcessor::ExtendedPoint &a = i > 0 ? annotated_points[i - 1] : annotated_points[i];
                const ExtrusionProcessor::ExtendedPoint &b = annotated_points[i];
                ExtrusionLine line_out{a.position.cast<float>(), b.position.cast<float>(), float((a.position - b.position).norm()),
                                       extrusion};

                Vec2f middle                               = 0.5 * (line_out.a + line_out.b);
                auto [middle_distance, bottom_line_idx, x] = prev_layer_lines.distance_from_lines_extra<false>(middle);
                ExtrusionLine bottom_line                  = prev_layer_lines.get_lines().empty() ? ExtrusionLine{} :
                                                                                                    prev_layer_lines.get_line(bottom_line_idx);

                Vec2f v1   = (bottom_line.b - bottom_line.a);
                Vec2f v2   = (a.position.cast<float>() - bottom_line.a);
                auto  d    = (v1.x() * v2.y()) - (v1.y() * v2.x());
                float sign = (d > 0) ? -1.0f : 1.0f;

                line_out.curled_up_height = estimate_curled_up_height(middle_distance * sign, 0.5 * (a.curvature + b.curvature), l->height,
                                                                      flow_width, bottom_line.curled_up_height, params);

                current_layer_lines.push_back(line_out);
            }
        }

        for (const ExtrusionLine &line : current_layer_lines) {
            if (line.curled_up_height > params.curling_tolerance_limit) {
                l->curled_lines.push_back(CurledLine{Point::new_scale(line.a), Point::new_scale(line.b), line.curled_up_height});
            }
        }

#ifdef DEBUG_FILES
        for (const ExtrusionLine &line : current_layer_lines) {
            if (line.curled_up_height > params.curling_tolerance_limit) {
                Vec3f color = value_to_rgbf(-EPSILON, l->height * params.max_curled_height_factor, line.curled_up_height);
                fprintf(debug_file, "v %f %f %f  %f %f %f\n", line.b[0], line.b[1], l->print_z, color[0], color[1], color[2]);
            }
        }
        for (const ExtrusionLine &line : current_layer_lines) {
            Vec3f color = value_to_rgbf(-EPSILON, l->height * params.max_curled_height_factor, line.curled_up_height);
            fprintf(full_file, "v %f %f %f  %f %f %f\n", line.b[0], line.b[1], l->print_z, color[0], color[1], color[2]);
        }
#endif

        prev_layer_lines = LD{current_layer_lines};
    }

#ifdef DEBUG_FILES
    fclose(debug_file);
    fclose(full_file);
#endif
}

void estimate_malformations(LayerPtrs &layers, const Params &params)
{
#ifdef DEBUG_FILES
    FILE *debug_file = boost::nowide::fopen(debug_out_path("object_malformations.obj").c_str(), "w");
    FILE *full_file  = boost::nowide::fopen(debug_out_path("object_full.obj").c_str(), "w");
#endif

    LD prev_layer_lines{};

    for (Layer *l : layers) {
        l->curled_lines.clear();
        std::vector<Linef> boundary_lines = l->lower_layer != nullptr ? to_unscaled_linesf(l->lower_layer->lslices) : std::vector<Linef>();
        AABBTreeLines::LinesDistancer<Linef> prev_layer_boundary{std::move(boundary_lines)};
        std::vector<ExtrusionLine>           current_layer_lines;
        for (const LayerRegion *layer_region : l->regions()) {
            for (const ExtrusionEntity *extrusion : layer_region->perimeters().flatten().entities) {
                if (!extrusion->role().is_external_perimeter())
                    continue;

                Points extrusion_pts;
                extrusion->collect_points(extrusion_pts);
                float flow_width       = get_flow_width(layer_region, extrusion->role());

                ExtrusionProcessor::PropertiesEstimationConfig config{};
                config.max_line_length = params.bridge_distance;
                config.add_corners = true;
                config.flow_width = flow_width;
                auto annotated_points = ExtrusionProcessor::estimate_points_properties<
                    false>(extrusion_pts, prev_layer_lines, config);

                for (size_t i = 0; i < annotated_points.size(); ++i) {
                    const ExtrusionProcessor::ExtendedPoint &a = i > 0 ? annotated_points[i - 1] : annotated_points[i];
                    const ExtrusionProcessor::ExtendedPoint &b = annotated_points[i];
                    ExtrusionLine line_out{a.position.cast<float>(), b.position.cast<float>(), float((a.position - b.position).norm()),
                                           extrusion};

                    Vec2f middle                               = 0.5 * (line_out.a + line_out.b);
                    auto [middle_distance, bottom_line_idx, x] = prev_layer_lines.distance_from_lines_extra<false>(middle);
                    ExtrusionLine bottom_line                  = prev_layer_lines.get_lines().empty() ? ExtrusionLine{} :
                                                                                                        prev_layer_lines.get_line(bottom_line_idx);

                    // correctify the distance sign using slice polygons
                    float sign = (prev_layer_boundary.distance_from_lines<true>(middle.cast<double>()) + 0.5f * flow_width) < 0.0f ? -1.0f :
                                                                                                                                     1.0f;

                    line_out.curled_up_height = estimate_curled_up_height(middle_distance * sign, 0.5 * (a.curvature + b.curvature),
                                                                          l->height, flow_width, bottom_line.curled_up_height, params);

                    current_layer_lines.push_back(line_out);
                }
            }
        }

        for (const ExtrusionLine &line : current_layer_lines) {
            if (line.curled_up_height > params.curling_tolerance_limit) {
                l->curled_lines.push_back(CurledLine{Point::new_scale(line.a), Point::new_scale(line.b), line.curled_up_height});
            }
        }

#ifdef DEBUG_FILES
        for (const ExtrusionLine &line : current_layer_lines) {
            if (line.curled_up_height > params.curling_tolerance_limit) {
                Vec3f color = value_to_rgbf(-EPSILON, l->height * params.max_curled_height_factor, line.curled_up_height);
                fprintf(debug_file, "v %f %f %f  %f %f %f\n", line.b[0], line.b[1], l->print_z, color[0], color[1], color[2]);
            }
        }
        for (const ExtrusionLine &line : current_layer_lines) {
            Vec3f color = value_to_rgbf(-EPSILON, l->height * params.max_curled_height_factor, line.curled_up_height);
            fprintf(full_file, "v %f %f %f  %f %f %f\n", line.b[0], line.b[1], l->print_z, color[0], color[1], color[2]);
        }
#endif

        prev_layer_lines = LD{current_layer_lines};
    }

#ifdef DEBUG_FILES
    fclose(debug_file);
    fclose(full_file);
#endif
}

std::vector<std::pair<SupportPointCause, bool>> gather_issues(const SupportPoints &support_points, PartialObjects &partial_objects)
{
    std::vector<std::pair<SupportPointCause, bool>> result;
    // The partial object are most likely sorted from smaller to larger as the print continues, so this should save some sorting time
    std::reverse(partial_objects.begin(), partial_objects.end());
    std::sort(partial_objects.begin(), partial_objects.end(),
              [](const PartialObject &left, const PartialObject &right) { return left.volume > right.volume; });

    // Object may have zero extrusions and thus no partial objects. (e.g. very tiny object)
    float max_volume_part = partial_objects.empty() ? 0.0f : partial_objects.front().volume;
    for (const PartialObject &p : partial_objects) {
        if (p.volume > max_volume_part / 200.0f && !p.connected_to_bed) {
                result.emplace_back(SupportPointCause::UnstableFloatingPart, true);
                break;
        }
    }

    // should be detected in previous step
    // if (!unstable_floating_part_added) {
    //     for (const SupportPoint &sp : support_points) {
    //             if (sp.cause == SupportPointCause::UnstableFloatingPart) {
    //             result.emplace_back(SupportPointCause::UnstableFloatingPart, true);
    //             break;
    //             }
    //     }
    // }

    std::vector<SupportPoint> ext_supp_points{};
    ext_supp_points.reserve(support_points.size());
    for (const SupportPoint &sp : support_points) {
        switch (sp.cause) {
        case SupportPointCause::FloatingBridgeAnchor:
        case SupportPointCause::FloatingExtrusion: ext_supp_points.push_back(sp); break;
        default: break;
        }
    }

    auto coord_fn = [&ext_supp_points](size_t idx, size_t dim) { return ext_supp_points[idx].position[dim]; };
    KDTreeIndirect<3, float, decltype(coord_fn)> ext_points_tree{coord_fn, ext_supp_points.size()};
    for (const SupportPoint &sp : ext_supp_points) {
        auto cluster         = find_nearby_points(ext_points_tree, sp.position, 3.0);
        int  score           = 0;
        bool floating_bridge = false;
        for (size_t idx : cluster) {
                score += ext_supp_points[idx].cause == SupportPointCause::FloatingBridgeAnchor ? 3 : 1;
                floating_bridge = floating_bridge || ext_supp_points[idx].cause == SupportPointCause::FloatingBridgeAnchor;
        }
        if (score > 5) {
                if (floating_bridge) {
                result.emplace_back(SupportPointCause::FloatingBridgeAnchor, true);
                } else {
                result.emplace_back(SupportPointCause::FloatingExtrusion, true);
                }
                break;
        }
    }

    for (const SupportPoint &sp : support_points) {
        if (sp.cause == SupportPointCause::SeparationFromBed) {
                result.emplace_back(SupportPointCause::SeparationFromBed, true);
                break;
        }
    }

    for (const SupportPoint &sp : support_points) {
        if (sp.cause == SupportPointCause::WeakObjectPart) {
                result.emplace_back(SupportPointCause::WeakObjectPart, true);
                break;
        }
    }

    if (ext_supp_points.size() > max_volume_part / 200.0f) {
        result.emplace_back(SupportPointCause::FloatingExtrusion, false);
    }

    for (const SupportPoint &sp : support_points) {
        if (sp.cause == SupportPointCause::LongBridge) {
                result.emplace_back(SupportPointCause::LongBridge, false);
                break;
        }
    }

    return result;
}

} // namespace SupportSpotsGenerator
