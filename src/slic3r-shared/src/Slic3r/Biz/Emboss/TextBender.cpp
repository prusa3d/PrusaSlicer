#include "Slic3r/Biz/Emboss/TextBender.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <limits>

namespace Slic3r::Biz::Emboss {
namespace {

constexpr double ANGLE_EPSILON = 1e-4;

double effective_angle(float angle)
{
    const double value = TextBender::clamp_angle(angle);
    return std::abs(value) <= ANGLE_EPSILON ? 0.0 : value;
}

Domain::BoundingBox3d to_double(const Domain::BoundingBox3f& box)
{
    return {box.min.cast<double>(), box.max.cast<double>()};
}

struct BendGeometry {
    Domain::Vec3d center;
    double width;
    double height;
    double horizontal;
    double vertical;
    double arc;
    double inverse_tolerance = 0.0;

    BendGeometry(const BendParams& params, const Domain::BoundingBox3d& box) :
        center(0.5 * (box.min + box.max)),
        width(std::max(box.max.x() - box.min.x(), 1e-3)),
        height(std::max(box.max.y() - box.min.y(), 1e-3)),
        horizontal(effective_angle(params.horizontal_bend)),
        vertical(effective_angle(params.vertical_curl)),
        arc(effective_angle(params.vertical_arc))
    {}

    bool flat() const { return horizontal == 0.0 && vertical == 0.0 && arc == 0.0; }

    double arc_span() const
    {
        const double half = 0.5 * horizontal;
        return half == 0.0 ? width : width * std::sin(half) / half;
    }

    double inverse_angle(double offset, double radius, double angle) const
    {
        const double sine = std::clamp(offset / radius, -1.0, 1.0);
        const double half = 0.5 * std::abs(angle);
        // Mesh vertices are floats. At a semicircle's ends, asin amplifies
        // their rounding error; retain the known endpoint within that error.
        if (inverse_tolerance > 0.0
            && std::abs(std::abs(sine) - std::sin(half)) <= inverse_tolerance / std::abs(radius))
            return std::copysign(half, sine);
        return std::asin(sine);
    }

    Domain::Vec3d unbend_arc(const Domain::Vec3d& point, double span) const
    {
        if (arc == 0.0)
            return point;
        const double radius = span / arc;
        const double beta = inverse_angle(point.x() - center.x(), radius, arc);
        Domain::Vec3d result = point;
        result.x() = center.x() + radius * beta;
        result.y() -= 2.0 * radius * std::pow(std::sin(0.5 * beta), 2);
        return result;
    }

    Domain::Vec3d bend(const Domain::Vec3d& point) const
    {
        Domain::Vec3d result = point;
        auto bend_axis = [&](int axis, double span, double angle) {
            if (angle == 0.0)
                return;
            const double alpha = (point[axis] - center[axis]) * angle / span;
            const double radius = span / angle;
            result[axis] = center[axis] + radius * std::sin(alpha);
            // This form avoids cancellation for small angles.
            result.z() += 2.0 * radius * std::pow(std::sin(0.5 * alpha), 2);
        };
        bend_axis(0, width, horizontal);
        bend_axis(1, height, vertical);
        // Apply the in-plane arc after the two depth bends. The middle remains
        // fixed; both ends rise for a positive angle and fall for a negative one.
        if (arc != 0.0) {
            const double radius = arc_span() / arc;
            const double beta = (result.x() - center.x()) / radius;
            result.x() = center.x() + radius * std::sin(beta);
            result.y() += 2.0 * radius * std::pow(std::sin(0.5 * beta), 2);
        }
        return result;
    }

    Domain::Vec3d unbend(const Domain::Vec3d& point) const
    {
        Domain::Vec3d result = unbend_arc(point, arc_span());
        auto unbend_axis = [&](int axis, double span, double angle) {
            if (angle == 0.0)
                return;
            const double radius = span / angle;
            const double alpha = inverse_angle(result[axis] - center[axis], radius, angle);
            result[axis] = center[axis] + radius * alpha;
            result.z() -= 2.0 * radius * std::pow(std::sin(0.5 * alpha), 2);
        };
        unbend_axis(0, width, horizontal);
        unbend_axis(1, height, vertical);
        return result;
    }
};

// Split every marked edge in both incident triangles. Sharing midpoint indices
// and handling all three edges together prevents cracks and T junctions.
void subdivide_edges(indexed_triangle_set& mesh, const BendGeometry& geometry)
{
    if (geometry.flat())
        return;
    // The final arc can amplify the preceding deformation by at most sqrt(2).
    const double amplification = geometry.arc == 0.0 ? 1.0 : std::sqrt(2.0);
    const double curvature_x = amplification * std::abs(geometry.horizontal) / geometry.width
        + std::abs(geometry.arc) / geometry.arc_span();
    const double curvature_y = amplification * std::abs(geometry.vertical) / geometry.height;
    for (;;) {
        std::map<std::pair<int, int>, int> midpoints;
        auto midpoint = [&](int a, int b) {
            const Domain::Vec3d delta = (mesh.vertices[a] - mesh.vertices[b]).cast<double>();
            // Bound the deviation of the warped edge from a straight segment.
            const double error = (curvature_x * delta.x() * delta.x()
                + curvature_y * delta.y() * delta.y()) / 8.0;
            if (error <= TextBender::MAX_CHORD_ERROR)
                return -1;
            const std::pair<int, int> key = std::minmax(a, b);
            const auto found = midpoints.find(key);
            if (found != midpoints.end())
                return found->second;
            const int index = static_cast<int>(mesh.vertices.size());
            const Domain::Vec3f position = 0.5f * (mesh.vertices[a] + mesh.vertices[b]);
            mesh.vertices.push_back(position);
            midpoints.emplace(key, index);
            return index;
        };

        std::vector<Domain::Index3> triangles;
        triangles.reserve(mesh.indices.size());
        for (const auto& tri : mesh.indices) {
            const int mids[] = {midpoint(tri[0], tri[1]), midpoint(tri[1], tri[2]), midpoint(tri[2], tri[0])};
            const int count = (mids[0] >= 0) + (mids[1] >= 0) + (mids[2] >= 0);
            if (count == 0) {
                triangles.push_back(tri);
            } else if (count == 3) {
                triangles.push_back({tri[0], mids[0], mids[2]});
                triangles.push_back({mids[0], tri[1], mids[1]});
                triangles.push_back({mids[2], mids[1], tri[2]});
                triangles.push_back({mids[0], mids[1], mids[2]});
            } else {
                int edge = 0;
                while (mids[edge] < 0 || (count == 2 && mids[(edge + 1) % 3] < 0))
                    ++edge;
                const int a = tri[edge], b = tri[(edge + 1) % 3], c = tri[(edge + 2) % 3];
                const int m = mids[edge];
                if (count == 1) {
                    triangles.push_back({a, m, c});
                    triangles.push_back({m, b, c});
                } else {
                    const int n = mids[(edge + 1) % 3];
                    triangles.push_back({b, n, m});
                    triangles.push_back({a, m, c});
                    triangles.push_back({m, n, c});
                }
            }
        }
        if (midpoints.empty())
            break;
        mesh.indices = std::move(triangles);
    }
}

} // namespace

double TextBender::clamp_angle(double radians)
{
    return std::isfinite(radians) ? std::clamp(radians, -std::numbers::pi, std::numbers::pi) : 0.0;
}

Domain::Vec3d TextBender::bend_point(const Domain::Vec3d& point, const BendParams& params,
    const Domain::BoundingBox3f& base_bbox)
{
    return BendGeometry(params, to_double(base_bbox)).bend(point);
}

Domain::Vec3d TextBender::unbend_point(const Domain::Vec3d& point, const BendParams& params,
    const Domain::BoundingBox3f& base_bbox)
{
    return BendGeometry(params, to_double(base_bbox)).unbend(point);
}

void TextBender::prepare_mesh(indexed_triangle_set& mesh, const Domain::BoundingBox3d& base_bbox)
{
    const float limit = static_cast<float>(std::numbers::pi);
    subdivide_edges(mesh, BendGeometry({limit, limit, limit}, base_bbox));
}

void TextBender::restore_mesh(indexed_triangle_set& mesh, const BendParams& params,
    const Domain::BoundingBox3d& base_bbox)
{
    BendGeometry geometry(params, base_bbox);
    if (geometry.flat())
        return;
    geometry.inverse_tolerance = 4.0 * std::numeric_limits<float>::epsilon()
        * std::max({1.0, base_bbox.min.cwiseAbs().maxCoeff(), base_bbox.max.cwiseAbs().maxCoeff()});
    for (auto& vertex : mesh.vertices)
        vertex = geometry.unbend(vertex.cast<double>()).cast<float>();
}

void TextBender::bend_mesh(indexed_triangle_set& mesh, const BendParams& params,
    const Domain::BoundingBox3f& base_bbox)
{
    bend_mesh(mesh, params, to_double(base_bbox));
}

void TextBender::bend_mesh(indexed_triangle_set& mesh, const BendParams& params,
    const Domain::BoundingBox3d& base_bbox)
{
    const BendGeometry geometry(params, base_bbox);
    if (geometry.flat())
        return;
    subdivide_edges(mesh, geometry);
    for (auto& vertex : mesh.vertices)
        vertex = geometry.bend(vertex.cast<double>()).cast<float>();
}

void TextBender::unbend_mesh(indexed_triangle_set& mesh, const BendParams& params,
    const Domain::BoundingBox3f& bent_bbox)
{
    unbend_mesh(mesh, params, to_double(bent_bbox));
}

void TextBender::unbend_mesh(indexed_triangle_set& mesh, const BendParams& params,
    const Domain::BoundingBox3d& bent_bbox)
{
    BendGeometry geometry(params, bent_bbox);
    if (geometry.flat() || mesh.vertices.empty())
        return;
    // The bent extrema are chord endpoints. Recover the arc lengths before
    // applying each inverse stage.
    auto arc_length = [](double chord, double angle) {
        const double half = 0.5 * std::abs(angle);
        return half == 0.0 ? chord : chord * half / std::sin(half);
    };
    if (geometry.arc != 0.0) {
        const double span = arc_length(geometry.width, geometry.arc);
        std::vector<Domain::Vec3d> intermediate;
        intermediate.reserve(mesh.vertices.size());
        for (const auto& vertex : mesh.vertices)
            intermediate.push_back(geometry.unbend_arc(vertex.cast<double>(), span));
        Domain::Vec3d lo = intermediate.front(), hi = lo;
        for (const auto& vertex : intermediate) {
            lo = lo.cwiseMin(vertex);
            hi = hi.cwiseMax(vertex);
        }
        // The in-plane arc shifts Y bounds. Undo it before recovering the
        // original height, keeping intermediate values in double precision.
        geometry = BendGeometry({params.horizontal_bend, params.vertical_curl}, {lo, hi});
        geometry.width = arc_length(geometry.width, geometry.horizontal);
        geometry.height = arc_length(geometry.height, geometry.vertical);
        for (size_t i = 0; i < mesh.vertices.size(); ++i)
            mesh.vertices[i] = geometry.unbend(intermediate[i]).cast<float>();
        return;
    }
    geometry.width = arc_length(geometry.width, geometry.horizontal);
    geometry.height = arc_length(geometry.height, geometry.vertical);
    for (auto& vertex : mesh.vertices)
        vertex = geometry.unbend(vertex.cast<double>()).cast<float>();
}

} // namespace Slic3r::Biz::Emboss
