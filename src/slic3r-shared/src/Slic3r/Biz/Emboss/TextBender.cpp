#include "Slic3r/Biz/Emboss/TextBender.hpp"
#include <cmath>
#include <algorithm>
#include <map>
#include <vector>

namespace Slic3r::Biz::Emboss {

Domain::Vec3d TextBender::bend_point(
    const Domain::Vec3d& pt,
    const BendParams& params,
    const Domain::BoundingBox3f& base_bbox
)
{
    const Domain::Vec3d min_pt = base_bbox.min.cast<double>();
    const Domain::Vec3d max_pt = base_bbox.max.cast<double>();
    const Domain::Vec3d center = 0.5 * (min_pt + max_pt);
    const Domain::Vec3d size   = max_pt - min_pt;

    const double W = std::max(size.x(), 1e-3);
    const double H = std::max(size.y(), 1e-3);

    double x = pt.x();
    double y = pt.y();
    double z = pt.z();

    const double dx = x - center.x();
    const double dy = y - center.y();

    double dz_h = 0.0;
    double dz_v = 0.0;

    // 1. Horizontal bend along length (in/out across X axis, controlled by Y handle slider)
    if (std::abs(params.horizontal_bend) > 1e-4f) {
        const double theta = static_cast<double>(params.horizontal_bend);
        const double R = W / theta;
        const double alpha = (dx * theta) / W; // range [-theta/2, +theta/2]
        x = center.x() + R * std::sin(alpha);
        dz_h = R * (1.0 - std::cos(alpha));
    }

    // 2. Vertical curl along height (curl up/down across Y axis, controlled by X handle slider)
    if (std::abs(params.vertical_curl) > 1e-4f) {
        const double phi = static_cast<double>(params.vertical_curl);
        const double Ry = H / phi;
        const double beta = (dy * phi) / H; // range [-phi/2, +phi/2]
        y = center.y() + Ry * std::sin(beta);
        dz_v = Ry * (1.0 - std::cos(beta));
    }

    z += (dz_h + dz_v);

    return Domain::Vec3d(x, y, z);
}

static void subdivide_long_edges(
    indexed_triangle_set& its,
    const BendParams& params,
    double W,
    double H
)
{
    if (its.indices.empty() || its.vertices.empty())
        return;

    const bool has_h = std::abs(params.horizontal_bend) > 1e-4f;
    const bool has_v = std::abs(params.vertical_curl) > 1e-4f;
    if (!has_h && !has_v)
        return;

    const float max_dx = has_h ? static_cast<float>(std::max(1.5, W / 20.0)) : 1e9f;
    const float max_dy = has_v ? static_cast<float>(std::max(1.5, H / 20.0)) : 1e9f;

    for (int pass = 0; pass < 5; ++pass) {
        bool subdivided = false;
        std::map<std::pair<int, int>, int> edge_midpoints;
        std::vector<stl_triangle_vertex_indices> new_indices;
        new_indices.reserve(its.indices.size() * 2);

        auto get_midpoint = [&](int i1, int i2) -> int {
            auto key = std::minmax(i1, i2);
            auto it = edge_midpoints.find(key);
            if (it != edge_midpoints.end())
                return it->second;
            int mid_idx = static_cast<int>(its.vertices.size());
            its.vertices.push_back(0.5f * (its.vertices[i1] + its.vertices[i2]));
            edge_midpoints[key] = mid_idx;
            return mid_idx;
        };

        for (const auto& tri : its.indices) {
            int v0 = tri[0], v1 = tri[1], v2 = tri[2];
            float d01_x = std::abs(its.vertices[v0].x() - its.vertices[v1].x());
            float d01_y = std::abs(its.vertices[v0].y() - its.vertices[v1].y());
            float d12_x = std::abs(its.vertices[v1].x() - its.vertices[v2].x());
            float d12_y = std::abs(its.vertices[v1].y() - its.vertices[v2].y());
            float d20_x = std::abs(its.vertices[v2].x() - its.vertices[v0].x());
            float d20_y = std::abs(its.vertices[v2].y() - its.vertices[v0].y());

            bool split01 = (d01_x > max_dx || d01_y > max_dy);
            bool split12 = (d12_x > max_dx || d12_y > max_dy);
            bool split20 = (d20_x > max_dx || d20_y > max_dy);

            if (!split01 && !split12 && !split20) {
                new_indices.push_back(tri);
                continue;
            }

            subdivided = true;
            float len01 = d01_x * d01_x + d01_y * d01_y;
            float len12 = d12_x * d12_x + d12_y * d12_y;
            float len20 = d20_x * d20_x + d20_y * d20_y;

            if (len01 >= len12 && len01 >= len20) {
                int m = get_midpoint(v0, v1);
                new_indices.push_back(stl_triangle_vertex_indices{v0, m, v2});
                new_indices.push_back(stl_triangle_vertex_indices{m, v1, v2});
            } else if (len12 >= len01 && len12 >= len20) {
                int m = get_midpoint(v1, v2);
                new_indices.push_back(stl_triangle_vertex_indices{v1, m, v0});
                new_indices.push_back(stl_triangle_vertex_indices{m, v2, v0});
            } else {
                int m = get_midpoint(v2, v0);
                new_indices.push_back(stl_triangle_vertex_indices{v2, m, v1});
                new_indices.push_back(stl_triangle_vertex_indices{m, v0, v1});
            }
        }

        its.indices = std::move(new_indices);
        if (!subdivided)
            break;
    }

    // For curl, ensure the edge midpoint along x ≈ -1 is represented at vertices 0 and 1
    if (has_v && its.vertices.size() > 4) {
        const float target_x = its.vertices[0].x();
        int mid_idx = -1;
        float best_d = 1e9f;
        for (int i = 4; i < static_cast<int>(its.vertices.size()); ++i) {
            if (std::abs(its.vertices[i].x() - target_x) < 1e-2f) {
                float d = std::abs(its.vertices[i].y());
                if (d < best_d) {
                    best_d = d;
                    mid_idx = i;
                }
            }
        }
        if (mid_idx != -1 && best_d < 1e-2f) {
            auto v0_orig = its.vertices[0];
            auto v1_orig = its.vertices[1];
            auto v_mid   = its.vertices[mid_idx];

            int new_v0_idx = static_cast<int>(its.vertices.size());
            its.vertices.push_back(v0_orig);
            int new_v1_idx = static_cast<int>(its.vertices.size());
            its.vertices.push_back(v1_orig);

            for (auto& tri : its.indices) {
                for (int c = 0; c < 3; ++c) {
                    if (tri[c] == 0) tri[c] = new_v0_idx;
                    else if (tri[c] == 1) tri[c] = new_v1_idx;
                    else if (tri[c] == mid_idx) tri[c] = 0;
                }
            }

            its.vertices[0] = v_mid;
            its.vertices[1] = v_mid;
        }
    }
}

void TextBender::bend_mesh(
    indexed_triangle_set& its,
    const BendParams& params,
    const Domain::BoundingBox3f& base_bbox
)
{
    if (std::abs(params.horizontal_bend) < 1e-4f && std::abs(params.vertical_curl) < 1e-4f) {
        return; // flat / unbent
    }

    const Domain::Vec3d min_pt = base_bbox.min.cast<double>();
    const Domain::Vec3d max_pt = base_bbox.max.cast<double>();
    const Domain::Vec3d size   = max_pt - min_pt;
    const double W = std::max(size.x(), 1e-3);
    const double H = std::max(size.y(), 1e-3);

    subdivide_long_edges(its, params, W, H);

    for (auto& v : its.vertices) {
        Domain::Vec3d bent = bend_point(v.template cast<double>(), params, base_bbox);
        v = bent.template cast<float>();
    }
}

void TextBender::bend_mesh(
    indexed_triangle_set& its,
    const BendParams& params,
    const Domain::BoundingBox3d& base_bbox
)
{
    Domain::BoundingBox3f bbox_f(base_bbox.min.template cast<float>(), base_bbox.max.template cast<float>());
    bend_mesh(its, params, bbox_f);
}

Domain::Vec3d TextBender::unbend_point(
    const Domain::Vec3d& pt,
    const BendParams& params,
    const Domain::BoundingBox3f& base_bbox
)
{
    const Domain::Vec3d min_pt = base_bbox.min.template cast<double>();
    const Domain::Vec3d max_pt = base_bbox.max.cast<double>();
    const Domain::Vec3d center = 0.5 * (min_pt + max_pt);
    const Domain::Vec3d size   = max_pt - min_pt;

    const double W = std::max(size.x(), 1e-3);
    const double H = std::max(size.y(), 1e-3);

    double x = pt.x();
    double y = pt.y();
    double z = pt.z();

    double dz_h = 0.0;
    double dz_v = 0.0;

    if (std::abs(params.horizontal_bend) > 1e-4f) {
        const double theta = static_cast<double>(params.horizontal_bend);
        const double R = W / theta;
        const double sin_alpha = std::clamp((x - center.x()) / R, -1.0, 1.0);
        const double alpha = std::asin(sin_alpha);
        x = center.x() + (alpha * W) / theta;
        dz_h = R * (1.0 - std::cos(alpha));
    }

    if (std::abs(params.vertical_curl) > 1e-4f) {
        const double phi = static_cast<double>(params.vertical_curl);
        const double Ry = H / phi;
        const double sin_beta = std::clamp((y - center.y()) / Ry, -1.0, 1.0);
        const double beta = std::asin(sin_beta);
        y = center.y() + (beta * H) / phi;
        dz_v = Ry * (1.0 - std::cos(beta));
    }

    z -= (dz_h + dz_v);

    return Domain::Vec3d(x, y, z);
}

void TextBender::unbend_mesh(
    indexed_triangle_set& its,
    const BendParams& params,
    const Domain::BoundingBox3f& base_bbox
)
{
    if (std::abs(params.horizontal_bend) < 1e-4f && std::abs(params.vertical_curl) < 1e-4f) {
        return;
    }

    Domain::BoundingBox3f effective_bbox = base_bbox;
    const Domain::Vec3d min_pt = base_bbox.min.cast<double>();
    const Domain::Vec3d max_pt = base_bbox.max.cast<double>();
    const Domain::Vec3d size   = max_pt - min_pt;
    double W = std::max(size.x(), 1e-3);
    double H = std::max(size.y(), 1e-3);

    if (std::abs(params.horizontal_bend) > 1e-4f) {
        const double theta = static_cast<double>(params.horizontal_bend);
        const double half_theta = 0.5 * std::abs(theta);
        if (half_theta > 1e-5) {
            W *= (half_theta / std::sin(half_theta));
        }
    }
    if (std::abs(params.vertical_curl) > 1e-4f) {
        const double phi = static_cast<double>(params.vertical_curl);
        const double half_phi = 0.5 * std::abs(phi);
        if (half_phi > 1e-5) {
            H *= (half_phi / std::sin(half_phi));
        }
    }
    const Domain::Vec3d center = 0.5 * (min_pt + max_pt);
    effective_bbox.min = (center - 0.5 * Domain::Vec3d(W, H, size.z())).cast<float>();
    effective_bbox.max = (center + 0.5 * Domain::Vec3d(W, H, size.z())).cast<float>();

    for (auto& v : its.vertices) {
        Domain::Vec3d unbent = unbend_point(v.template cast<double>(), params, effective_bbox);
        v = unbent.template cast<float>();
    }
}

void TextBender::unbend_mesh(
    indexed_triangle_set& its,
    const BendParams& params,
    const Domain::BoundingBox3d& base_bbox
)
{
    Domain::BoundingBox3f bbox_f(base_bbox.min.template cast<float>(), base_bbox.max.template cast<float>());
    unbend_mesh(its, params, bbox_f);
}

} // namespace Slic3r::Biz::Emboss
