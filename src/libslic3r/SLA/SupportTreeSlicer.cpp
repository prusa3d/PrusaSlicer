#include "SupportTreeSlicer.hpp"
#include "libslic3r/SLA/SupportTreeTypes.hpp"
#include "libslic3r/ClipperUtils.hpp"

#include <tbb/parallel_for.h>


#define PARALLEL_SLICING_OF_TREE_SUPPORTS 1

namespace Slic3r {
namespace sla {
    
namespace {

// Beware! The polygon may be empty when x_limit_left and right are provided.
Polygon create_ellipse(double Cx, double Cy, double a, double b, size_t segments,
    double x_limit_left = std::numeric_limits<double>::lowest(), double x_limit_right = std::numeric_limits<double>::max())
{
    Polygon ellipse;
    Points& pts = ellipse.points;
    pts.reserve(segments + 2); // to avoid wasting space that we don't need.

    x_limit_left = std::max(Cx-a, x_limit_left);
    x_limit_right = std::min(Cx+a, x_limit_right);
    if (x_limit_right < x_limit_left + 10 * EPSILON)
        return Polygon();

    const double dx = (x_limit_right - 2 * EPSILON - x_limit_left) / (segments / 2.);
    double x = x_limit_left + EPSILON;
    while (x < x_limit_right) {
        pts.emplace_back(Point::new_scale(x, Cy - b * std::sqrt(1. - std::pow((x - Cx)/a, 2.))));
        x += dx;
    }
    if (pts.empty())
        return ellipse;
    pts.back().x() -= 2 * EPSILON;
    for (int idx = pts.size() - 1; idx >= 0; --idx) {
        Point pt(pts[idx].x(), coord_t(scale_(Cy)) - (pts[idx].y() - coord_t(scale_(Cy))));
        pts.emplace_back(pt);
    }

    return ellipse;
}

Polygon create_hyperbola(double Cx, double Cy, double a, double b, size_t segments,
    double x_limit_left, double x_limit_right)
{
    Polygon hyperbola;
    Points& pts = hyperbola.points;
    pts.reserve(segments + 2); // to avoid wasting space that we don't need.

    x_limit_left = std::max(Cx + a, x_limit_left);
    if (x_limit_right < x_limit_left + 10 * EPSILON)
        return Polygon();

    const double dx = (x_limit_right - 2 * EPSILON - x_limit_left) / (segments / 2.);
    double x = x_limit_left + EPSILON;
    while (x < x_limit_right) {
        pts.emplace_back(Point::new_scale(x, Cy - b * std::sqrt(std::pow((x - Cx)/a, 2.) - 1.)));
        x += dx;
    }
    pts.back().x() -= 2 * EPSILON;
    for (int idx = pts.size() - 1; idx >= 0; --idx) {
        Point pt(pts[idx].x(), coord_t(scale_(Cy)) - (pts[idx].y() - coord_t(scale_(Cy))));
        pts.emplace_back(pt);
    }

    return hyperbola;
}

inline std::optional<Polygon> slice_vertical_cylinder(const Vec3d& center, double height, double r, double h, int steps)
{
    if (h >= center.z() && h < center.z() + height)
        return create_ellipse(center.x(), center.y(), r, r, steps);
    return std::nullopt;
}

inline std::optional<Polygon> slice_vertical_cone(const Vec3d& center, double height, double r1, double r2, double h, int steps)
{
    if (h >= center.z() && h < center.z() + height) {
        float r = (h - center.z()) * (r2 - r1) / height + r1;
        return create_ellipse(center.x(), center.y(), r, r, steps);
    }
    return std::nullopt;
}

inline std::optional<Polygon> slice_sphere(const Vec3d& center, double R, double h, int steps)
{
    if (h >= center.z() - R && h < center.z() + R) {
        double r = std::sqrt(R * R - std::pow(h - center.z(), 2.f));
        return create_ellipse(center.x(), center.y(), r, r, steps);
    }
    return std::nullopt;
}
            

inline std::optional<Polygon> slice_cylinder(const Vec3d& center1, const Vec3d& center2, double r, double h, int steps)
{
    Vec3d top = center1.z() < center2.z() ? center2 : center1;
    Vec3d bottom = center1.z() < center2.z() ? center1 : center2;
    const double alpha = std::asin((top.z() - bottom.z()) / (top - bottom).norm());

    if (is_approx(alpha, M_PI_2))
        return slice_vertical_cylinder(bottom, top.z() - bottom.z(), r, h, steps);


    assert(alpha >= 0. && alpha < M_PI_2);
    double min_z = std::min(bottom.z() - r * std::cos(alpha), top.z() - r * std::cos(alpha));
    double max_z = std::max(top.z() + r * std::cos(alpha), bottom.z() + r * std::cos(alpha));

    if (h > min_z && h < max_z) {
        // Transform top so the problem is in 2D ((x,z) plane with bottom at origin)
        h -= bottom.z();
        top -= bottom;
        double theta = std::atan2(top.y(), top.x());
        top = Vec3d(std::hypot(top.x(), top.y()), 0., top.z());
        double len = top.norm();

        // Create ellipse and move it along x axis so it matches the cut at height h.
        double center_x = h / top.z() * top.x();
        double f = 1./std::tan(alpha) + std::tan(alpha);
        Polygon circle = create_ellipse(center_x, 0., r * len / top.z(), r, steps,
            center_x - h * f,
            center_x + (top.z() - h) * f);

        // Transform back to original position.
        circle.rotate(theta);                
        circle.translate(Point::new_scale(bottom.x(), bottom.y()));
        return circle;
    }
    return std::nullopt;
}

inline std::optional<Polygon> slice_cone(const Vec3d& center1, const Vec3d& center2, double r1, double r2, double h, int steps)
{
    Vec3d top = center1.z() < center2.z() ? center2 : center1;
    Vec3d bottom = center1.z() < center2.z() ? center1 : center2;
    double r_top = center1.z() < center2.z() ? r2 : r1;
    double r_bottom = center1.z() < center2.z() ? r1 : r2;

    if (is_approx(r_top, r_bottom))
        return slice_cylinder(bottom, top, r_top, h, steps);

    if (is_approx(center1.x(), center2.x()) && is_approx(center1.y(), center2.y()))
        return slice_vertical_cone(bottom, top.z() - bottom.z(), r_bottom, r_top, h, steps);

    if (r_top < r_bottom) {
        // Transform the problem so that r_top >= r_bottom.
        h = top.z() - h + bottom.z();
        std::swap(r_top, r_bottom);
        std::swap(top.z(), bottom.z());
        std::swap(top, bottom);
    }
    double alpha = std::asin((top.z() - bottom.z()) / (top - bottom).norm());
    double min_z = std::min(bottom.z() - r_bottom * std::cos(alpha), top.z() - r_top * std::cos(alpha));
    double max_z = std::max(top.z() + r_top * std::cos(alpha), bottom.z() + r_bottom * std::cos(alpha));

    if ( h > min_z && h < max_z) {
        // We rely on the fact that r_top > r_bottom, as ensured by the above code.
        Vec3d apex = (bottom - top).normalized() * ((bottom - top).norm() * (r_bottom / (r_top - r_bottom))) + bottom;

        // Transform top so the problem is in 2D ((x,z) plane with cone apex at origin)
        h -= apex.z();
        top -= apex;
        double theta = std::atan2(top.y(), top.x());
        top = Vec3d(std::hypot(top.x(), top.y()), 0., top.z());
        double len = top.norm();
        double beta = std::atan2(r_top, top.norm()); // half of apex angle
        if (std::abs(alpha - beta) < EPSILON) {
            // Treat the nearly parabolic situations as a hyperbola.
            r_top += EPSILON * 10;
            beta = std::atan2(r_top, top.norm());
        }

        Polygon conic_section;
        if (alpha > beta) {
            // The cut is an ellipse. Calculate its parameters.
            Vec3d A(std::abs(alpha+beta-M_PI_2) < EPSILON ? 0. : h / std::tan(alpha + beta), 0., h);
            Vec3d B(h / std::tan(alpha - beta), 0., h);
            Vec3d S = (A + B) / 2.;
            double a = S.x() - A.x(); // ellipse major axis

            // Now calculate minor axis for this slice.
            double v = h / std::tan(alpha);
            double w = S.x() - v;
            double s = v / std::cos(alpha) + w * cos(alpha);
            double r = r_top / len * s;
            double o = w * std::sin(alpha);
            double b = std::sqrt(r*r-o*o);

            // Create ellipse and move it along x axis so it matches the cut at height h.
            conic_section = create_ellipse(S.x(), 0., a, b, steps,
                std::tan(alpha) * ((bottom-apex).norm() / std::sin(alpha) - h),
                std::tan(alpha) * (len / std::sin(alpha) - h)
            );
        } else {
            // The cut is a hyperbola.
            if (h < 0.) {
                h = -h;
                alpha = -alpha;
            }
            Vec3d A(std::abs(alpha+beta-M_PI_2) < EPSILON ? 0. : h / std::tan(alpha + beta), 0., h);
            Vec3d B(-h / std::tan(beta - alpha), 0., h);
            Vec3d S = (A + B) / 2.;
            double a = A.x() - S.x();

            // Now calculate minor axis for this slice.
            double r = (h - len*std::sin(alpha))/std::cos(alpha);
            double x = top.x() - r * std::sin(alpha) - S.x();
            double y = std::sqrt(r_top*r_top - r*r);
            double b = a*y/std::sqrt(x*x-a*a);

            double llim = std::abs(alpha) < EPSILON
                ? (bottom - apex).norm()
                : std::tan(alpha) * ((bottom-apex).norm() / std::sin(alpha) - h);
            double rlim = S.x() + x;

            conic_section = create_hyperbola(S.x(), 0., a, b, steps, llim, rlim);
        }

        // Transform back to original position.
        conic_section.rotate(theta);                
        conic_section.translate(Point::new_scale(apex.x(), apex.y()));
        return conic_section;
    }
    return std::nullopt;
}


std::optional<Polygon> slice_head(const Head& head, double h, int steps)
{
    const Vec3d c1 = head.pos + head.dir * (head.r_back_mm + 2 * head.r_pin_mm + head.width_mm - head.penetration_mm);
    const Vec3d c2 = head.pos + head.dir * (head.r_pin_mm - head.penetration_mm);
    Polygons out(3);
    if (std::optional<Polygon> s = slice_sphere(c1, head.r_back_mm, h, steps); s)
        out[0] = std::move(*s);
    if (std::optional<Polygon> s = slice_sphere(c2, head.r_pin_mm, h, steps); s)
        out[1] = std::move(*s);

    Vec3d dir = (c2-c1).normalized();
    double phi = std::asin(head.r_back_mm / (c2-c1).norm());
    double l1 = head.r_back_mm * std::cos(M_PI_2 - phi);
    double l2 = head.r_pin_mm * std::cos(M_PI_2 - phi);
    if (std::optional<Polygon> s = slice_cone(
        c1 + dir * l1,
        c2 + dir * l2,
        head.r_back_mm*std::sin(M_PI_2-phi),
        head.r_pin_mm*std::sin(M_PI_2-phi),
        h, steps); s)
        out[2] = std::move(*s);

    out = union_(out);
    if (out.empty())
        return std::nullopt;
    return out.front();
}
    

} // anonymous namespace



ExPolygons slice_support_tree_at_height(const sla::SupportTreeOutput& output, float height, int steps)
{
    ExPolygons out;

    for (const Pillar& p : output.pillars) {
        if (std::optional<Polygon> slice = slice_vertical_cone(p.endpt, p.height, p.r_end, p.r_start, height, steps))
            out.emplace_back(std::move(*slice));
    }
    for (const Pedestal& p : output.pedestals) {
        if (std::optional<Polygon> slice = slice_vertical_cone(p.pos, p.height, p.r_bottom, p.r_top, height, steps))
            out.emplace_back(std::move(*slice));
    }
    for (const Junction& j : output.junctions) {
        if (std::optional<Polygon> slice = slice_sphere(j.pos, j.r, height, steps))
            out.emplace_back(std::move(*slice));
    }
    for (const Bridge& b : output.bridges) {
        if (std::optional<Polygon> slice = slice_cylinder(b.startp, b.endp, b.r, height, steps))
            out.emplace_back(std::move(*slice));
    }
    for (const Bridge& b : output.crossbridges) {
        if (std::optional<Polygon> slice = slice_cylinder(b.startp, b.endp, b.r, height, steps))
            out.emplace_back(std::move(*slice));
    }
    for (const DiffBridge& b : output.diffbridges) {
        if (std::optional<Polygon> slice = slice_cone(b.startp, b.endp, b.r, b.end_r, height, steps))
            out.emplace_back(std::move(*slice));
    }
    for (const Head& head : output.heads) {
        if (head.is_valid())
            if (std::optional<Polygon> slice = slice_head(head, height, steps); slice)
                out.emplace_back(std::move(*slice));
    }
    for (const Anchor& head : output.anchors) {
        if (head.is_valid())
            if (std::optional<Polygon> slice = slice_head(head, height, steps); slice)
                out.emplace_back(std::move(*slice));
    }

    return out;
}



std::vector<ExPolygons> slice_support_tree(
    const sla::SupportTreeOutput& output,
    const std::vector<float>& heights,
    int steps)
{
    // The algorithm seems inefficient at first. It iterates through all supports features
    // for every layer. It might be possible to optimize it by some clever sorting and iterating
    // over features instead, placing each slice into the correct vector. However:
    // 1. There are early exits for the features
    // 2. There are no lookups, just calculations (which are fast)
    // 3. The time it takes is already  negligible compared to the other slicing steps.
    // 4. By iterating over layers, the algorithm is trivially parallel without any synchronization needed.
    // lukasmatena expects that trying to make it better will just make it less readable, not faster.

    std::vector<ExPolygons> support_slices(heights.size());
    
#if PARALLEL_SLICING_OF_TREE_SUPPORTS
    tbb::parallel_for(tbb::blocked_range<size_t>(0, heights.size()),
    [&support_slices, &heights, &output, &steps](const tbb::blocked_range<size_t>& range) {
    for (size_t i = range.begin(); i != range.end(); ++i) {
#else
    for (size_t i = 0; i != heights.size(); ++i) {
#endif
        const float h = heights[i];
        support_slices[i] = slice_support_tree_at_height(output, h, steps);
    }
#if PARALLEL_SLICING_OF_TREE_SUPPORTS
    });
#endif
    return support_slices;
}



static double cone_side_area(double r1, double r2, double h)
{
    return M_PI*(r1+r2) * std::sqrt(h*h - std::pow(r1-r2, 2.));
}

static double cone_area(double r1, double r2, double h)
{
    return M_PI*r1*r1 + M_PI*r2*r2 + cone_side_area(r1, r2, h);
}

static double cylinder_side_area(double r, double h)
{
    return 2.*M_PI*r*h;
}

static double sphere_area(double r)
{
    return 4.*M_PI*r*r;
}

static double head_area(const Head& head)
{
    double area = 0.;
    const Vec3d c1 = head.pos + head.dir * (head.r_back_mm + 2 * head.r_pin_mm + head.width_mm - head.penetration_mm);
    const Vec3d c2 = head.pos + head.dir * (head.r_pin_mm - head.penetration_mm);
    //area += sphere_area(head.r_back_mm);
    //area += sphere_area(head.r_pin_mm);
    area += cone_area(head.r_back_mm, head.r_pin_mm, (c2-c1).norm());
    return area;
}

double calculate_supports_area(const sla::SupportTreeOutput& output)
{
    double area = 0.;
    for (const Pillar& p : output.pillars)
        area += cone_side_area(p.r_start, p.r_end, p.height);
    for (const Pedestal& p : output.pedestals)
        area += cone_side_area(p.r_bottom, p.r_top, p.height);
    for (const Junction& j : output.junctions) {
        // Do not count junctions. The are usually only partially visible, and we
        // overestimate some of the others.
    }
    for (const Bridge& b : output.bridges)
        area += cylinder_side_area(b.r, (b.endp - b.startp).norm());
    for (const Bridge& b : output.crossbridges)
        area += cylinder_side_area(b.r, (b.endp - b.startp).norm());
    for (const DiffBridge& b : output.diffbridges)
        area += cone_side_area(b.r, b.end_r, (b.startp - b.endp).norm());
    for (const Head& head : output.heads)
        area += head_area(head);
    for (const Anchor& head : output.anchors)
        area += head_area(head);
    return area;
}


} // namespace sla
} // namespace Slic3r

#undef PARALLEL_SLICING_OF_TREE_SUPPORTS
