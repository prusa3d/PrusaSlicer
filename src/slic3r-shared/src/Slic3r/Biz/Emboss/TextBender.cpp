#include "Slic3r/Biz/Emboss/TextBender.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <numbers>

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
        return width;
    }

    double horizontal_span() const
    {
        if (arc == 0.0)
            return width;
        const double half = 0.5 * std::abs(arc);
        return 2.0 * (std::abs(width / arc) + 0.5 * height) * std::sin(half);
    }

    std::pair<double, double> vertical_bounds() const
    {
        if (arc == 0.0)
            return {center.y() - 0.5 * height, center.y() + 0.5 * height};
        const double half = 0.5 * std::abs(arc);
        const double cosine = std::cos(half);
        const double sag = width / arc * (1.0 - cosine);
        const std::array<double, 4> offsets{
            -0.5 * height,
            0.5 * height,
            sag - 0.5 * height * cosine,
            sag + 0.5 * height * cosine
        };
        const auto [lo, hi] = std::minmax_element(offsets.begin(), offsets.end());
        return {center.y() + *lo, center.y() + *hi};
    }

    double vertical_span() const
    {
        const auto [lo, hi] = vertical_bounds();
        return hi - lo;
    }

    double vertical_center() const
    {
        const auto [lo, hi] = vertical_bounds();
        return 0.5 * (lo + hi);
    }

    Domain::Vec3d bend_horizontal_step(const Domain::Vec3d& pt) const
    {
        if (horizontal == 0.0)
            return pt;
        const double radius = horizontal_span() / horizontal;
        const double alpha = (pt.x() - center.x()) / radius;
        const double z_rel = pt.z() - center.z();
        Domain::Vec3d res = pt;
        res.x() = center.x() + (radius - z_rel) * std::sin(alpha);
        res.z() = center.z() + 2.0 * radius * std::pow(std::sin(0.5 * alpha), 2)
            + z_rel * std::cos(alpha);
        return res;
    }

    Domain::Vec3d unbend_horizontal_step(const Domain::Vec3d& pt, double span) const
    {
        if (horizontal == 0.0)
            return pt;
        const double radius = span / horizontal;
        const double dx = pt.x() - center.x();
        const double dz_center = center.z() + radius - pt.z();
        const double sign_r = radius >= 0.0 ? 1.0 : -1.0;
        double alpha = std::atan2(sign_r * dx, sign_r * dz_center);
        double r_eff = std::hypot(dx, dz_center) * sign_r;
        if (std::abs(alpha) > 0.5 * std::numbers::pi + 1e-4) {
            const double opposite_alpha = alpha - std::copysign(std::numbers::pi, alpha);
            if (std::abs(opposite_alpha) < std::abs(alpha)) {
                alpha = opposite_alpha;
                r_eff = -r_eff;
            }
        }
        Domain::Vec3d res = pt;
        res.x() = center.x() + radius * alpha;
        res.z() = center.z() + radius - r_eff;
        return res;
    }

    Domain::Vec3d bend_vertical_step(const Domain::Vec3d& pt) const
    {
        if (vertical == 0.0)
            return pt;
        const double radius = vertical_span() / vertical;
        const double bend_center = vertical_center();
        const double gamma = (pt.y() - bend_center) / radius;
        const double z_rel = pt.z() - center.z();
        Domain::Vec3d res = pt;
        res.y() = bend_center + (radius - z_rel) * std::sin(gamma);
        res.z() = center.z() + 2.0 * radius * std::pow(std::sin(0.5 * gamma), 2)
            + z_rel * std::cos(gamma);
        return res;
    }

    Domain::Vec3d unbend_vertical_step(const Domain::Vec3d& pt, double span) const
    {
        if (vertical == 0.0)
            return pt;
        const double radius = span / vertical;
        const double bend_center = vertical_center();
        const double dy = pt.y() - bend_center;
        const double dz_center = center.z() + radius - pt.z();
        const double sign_r = radius >= 0.0 ? 1.0 : -1.0;
        double gamma = std::atan2(sign_r * dy, sign_r * dz_center);
        double r_eff = std::hypot(dy, dz_center) * sign_r;
        if (std::abs(gamma) > 0.5 * std::numbers::pi + 1e-4) {
            const double opposite_gamma = gamma - std::copysign(std::numbers::pi, gamma);
            if (std::abs(opposite_gamma) < std::abs(gamma)) {
                gamma = opposite_gamma;
                r_eff = -r_eff;
            }
        }
        Domain::Vec3d res = pt;
        res.y() = bend_center + radius * gamma;
        res.z() = center.z() + radius - r_eff;
        return res;
    }

    Domain::Vec3d bend_arc_step(const Domain::Vec3d& pt) const
    {
        if (arc == 0.0)
            return pt;
        const double radius = arc_span() / arc;
        const double beta = (pt.x() - center.x()) / radius;
        const double y_rel = pt.y() - center.y();
        Domain::Vec3d res = pt;
        res.x() = center.x() + (radius - y_rel) * std::sin(beta);
        res.y() = center.y() + 2.0 * radius * std::pow(std::sin(0.5 * beta), 2) + y_rel * std::cos(beta);
        return res;
    }

    Domain::Vec3d unbend_arc_step(const Domain::Vec3d& pt, double span) const
    {
        if (arc == 0.0)
            return pt;
        const double radius = span / arc;
        const double dx = pt.x() - center.x();
        const double dy_center = center.y() + radius - pt.y();
        const double sign_r = radius >= 0.0 ? 1.0 : -1.0;
        double beta = std::atan2(sign_r * dx, sign_r * dy_center);
        double r_eff = std::hypot(dx, dy_center) * sign_r;
        const double max_beta = 0.5 * std::abs(arc) + 1e-4;
        if (std::abs(beta) > max_beta) {
            const double opposite_beta = beta - std::copysign(std::numbers::pi, beta);
            if (std::abs(opposite_beta) <= max_beta) {
                beta = opposite_beta;
                r_eff = -r_eff;
            } else {
                beta = std::clamp(beta, -max_beta, max_beta);
            }
        }
        const double y_rel = radius - r_eff;
        Domain::Vec3d res = pt;
        res.x() = center.x() + radius * beta;
        res.y() = center.y() + y_rel;
        return res;
    }

    Domain::Vec3d bend(const Domain::Vec3d& point) const
    {
        // Arc the face while extrusion still follows Z, then rotate the whole
        // cross-section through both depth bends. This keeps front-to-back
        // distance normal to the final surface instead of losing a cosine
        // factor toward the edges.
        Domain::Vec3d result = bend_arc_step(point);
        result = bend_horizontal_step(result);
        return bend_vertical_step(result);
    }

    Domain::Vec3d unbend(const Domain::Vec3d& point) const
    {
        Domain::Vec3d result = unbend_vertical_step(point, vertical_span());
        result = unbend_horizontal_step(result, horizontal_span());
        return unbend_arc_step(result, arc_span());
    }
};

// Split every marked edge in both incident triangles. Sharing midpoint indices
// and handling all three edges together prevents cracks and T junctions.
void subdivide_edges(indexed_triangle_set& mesh, const BendGeometry& geometry)
{
    if (geometry.flat())
        return;
    const double k_h = std::abs(geometry.horizontal) / geometry.width;
    const double k_v = std::abs(geometry.vertical) / geometry.height;
    const double k_a = std::abs(geometry.arc) / geometry.arc_span();
    for (;;) {
        std::map<std::pair<int, int>, int> midpoints;
        auto midpoint = [&](int a, int b) {
            const Domain::Vec3d delta = (mesh.vertices[a] - mesh.vertices[b]).cast<double>();
            const double dx = std::abs(delta.x());
            const double dy = std::abs(delta.y());
            const double dz = std::abs(delta.z());
            // Bound the middle-surface chord error and the additional rotation
            // of an edge that crosses through the text depth.
            const double error = (k_h * dx * dx + k_v * dy * dy
                + k_a * dx * dx * (1.0 + 0.5 * k_a * geometry.height)) / 8.0
                + 0.25 * (k_a * dx * dy + (k_h + k_a) * dx * dz + (k_v + k_a) * dy * dz);
            // Leave margin for higher-order terms from composing all stages.
            if (error <= 0.4 * TextBender::MAX_CHORD_ERROR)
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
    if (mesh.vertices.empty())
        return;
    BendGeometry initial_geometry(params, bent_bbox);
    if (initial_geometry.flat())
        return;
    // The bent extrema are chord endpoints. Recover the arc lengths before
    // refining all six original bounds as a fixed point. Including the bounds
    // refinement is necessary now that the front and back surfaces rotate:
    // their axis-aligned bent box is wider than the middle-surface chord.
    auto arc_length = [](double chord, double angle) {
        const double half = 0.5 * std::abs(angle);
        return half == 0.0 ? chord : chord * half / std::sin(half);
    };
    Domain::BoundingBox3d estimate = bent_bbox;
    const Domain::Vec3d initial_center = 0.5 * (estimate.min + estimate.max);
    const double width = arc_length(estimate.max.x() - estimate.min.x(), initial_geometry.horizontal);
    const double height = arc_length(estimate.max.y() - estimate.min.y(), initial_geometry.vertical);
    estimate.min.x() = initial_center.x() - 0.5 * width;
    estimate.max.x() = initial_center.x() + 0.5 * width;
    estimate.min.y() = initial_center.y() - 0.5 * height;
    estimate.max.y() = initial_center.y() + 0.5 * height;

    std::vector<Domain::Vec3d> recovered(mesh.vertices.size());
    for (int iteration = 0; iteration < 100; ++iteration) {
        BendGeometry geometry(params, estimate);
        Domain::Vec3d lo;
        Domain::Vec3d hi;
        for (size_t i = 0; i < mesh.vertices.size(); ++i) {
            recovered[i] = geometry.unbend(mesh.vertices[i].cast<double>());
            if (i == 0) {
                lo = hi = recovered[i];
            } else {
                lo = lo.cwiseMin(recovered[i]);
                hi = hi.cwiseMax(recovered[i]);
            }
        }
        const double change = std::max((lo - estimate.min).cwiseAbs().maxCoeff(),
            (hi - estimate.max).cwiseAbs().maxCoeff());
        estimate.min = 0.5 * (estimate.min + lo);
        estimate.max = 0.5 * (estimate.max + hi);
        if (change <= 1e-8 * std::max(1.0, (estimate.max - estimate.min).maxCoeff()))
            break;
    }
    BendGeometry geometry(params, estimate);
    for (auto& vertex : mesh.vertices)
        vertex = geometry.unbend(vertex.cast<double>()).cast<float>();
}

} // namespace Slic3r::Biz::Emboss
