#include "Slic3r/Biz/Emboss/TextBender.hpp"
#include <cmath>
#include <algorithm>

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

void TextBender::bend_mesh(
    indexed_triangle_set& its,
    const BendParams& params,
    const Domain::BoundingBox3f& base_bbox
)
{
    if (std::abs(params.horizontal_bend) < 1e-4f && std::abs(params.vertical_curl) < 1e-4f) {
        return; // flat / unbent
    }

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
    Domain::BoundingBox3f bbox_f(base_bbox.min.cast<float>(), base_bbox.max.cast<float>());
    bend_mesh(its, params, bbox_f);
}

} // namespace Slic3r::Biz::Emboss
