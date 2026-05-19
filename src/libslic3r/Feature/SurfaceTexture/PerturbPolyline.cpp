#include "PerturbPolyline.hpp"

#include <cmath>
#include <random>

#include "libslic3r/Arachne/utils/ExtrusionJunction.hpp"
#include "libslic3r/Arachne/utils/ExtrusionLine.hpp"

namespace Slic3r::Feature::SurfaceTexture {

double random_unit()
{
    thread_local std::random_device rd;
    thread_local std::mt19937 gen(rd.entropy() > 0 ? rd() : std::hash<std::thread::id>()(std::this_thread::get_id()));
    thread_local std::uniform_real_distribution<double> dist(0.0, 1.0);
    return dist(gen);
}

// Compute the next step length given the spacing config. For zero jitter,
// this is always point_distance_scaled. For non-zero jitter, a uniform
// random perturbation in ±jitter_fraction is applied.
static inline double next_step(const PerturbSpacing &spacing)
{
    const double pd = spacing.point_distance_scaled;
    if (spacing.jitter_fraction <= 0.0) return pd;
    const double min_step = pd * (1.0 - spacing.jitter_fraction);
    const double range    = pd * spacing.jitter_fraction * 2.0;
    return min_step + random_unit() * range;
}

void perturb_polyline(Points &poly, const bool closed,
                      const PerturbSpacing &spacing,
                      const OffsetProvider &offset_provider)
{
    if (poly.size() < 2 || spacing.point_distance_scaled <= 0.0) return;

    // Starting carry-over: a random offset for jittered spacing (matches
    // fuzzy-skin's original behaviour), zero for constant spacing.
    double dist_left_over = spacing.jitter_fraction > 0.0
                            ? random_unit() * (spacing.point_distance_scaled * (1.0 - spacing.jitter_fraction)) * 0.5
                            : 0.0;

    Points out;
    out.reserve(poly.size());

    Point *p0 = closed ? &poly.back() : &poly.front();
    for (auto it = closed ? poly.begin() : std::next(poly.begin()); it != poly.end(); ++it) {
        Point &p1 = *it;
        const Vec2d p0p1      = (p1 - *p0).cast<double>();
        const double p0p1_len = p0p1.norm();
        // Skip degenerate / near-zero segments to avoid extreme direction vectors.
        if (p0p1_len < 1.0) { p0 = &p1; continue; }
        const Vec2d dir      = p0p1 / p0p1_len;
        const Vec2d perp_dir = perp(dir);

        double d = dist_left_over;
        while (d < p0p1_len) {
            const Vec2d pos_f = p0->cast<double>() + dir * d;
            const double off  = offset_provider(pos_f, perp_dir);
            // Guard against NaN / infinity from edge-case sampling.
            if (!std::isfinite(off)) { d += next_step(spacing); continue; }
            const Vec2d delta = perp_dir * off;
            out.emplace_back(static_cast<coord_t>(pos_f.x() + delta.x()),
                             static_cast<coord_t>(pos_f.y() + delta.y()));
            d += next_step(spacing);
        }
        dist_left_over = d - p0p1_len;
        p0 = &p1;
    }

    // Ensure the result has at least 3 vertices (mirrors fuzzy-skin's safety).
    while (out.size() < 3 && !poly.empty()) {
        const size_t idx = poly.size() - (out.size() + 1);
        out.emplace_back(poly[idx]);
        if (idx == 0) break;
    }

    if (out.size() >= 3) poly = std::move(out);
}

void perturb_extrusion_line(Arachne::ExtrusionLine &line,
                            const PerturbSpacing &spacing,
                            const OffsetProvider &offset_provider)
{
    if (line.junctions.size() < 2 || spacing.point_distance_scaled <= 0.0) return;

    double dist_left_over = spacing.jitter_fraction > 0.0
                            ? random_unit() * (spacing.point_distance_scaled * (1.0 - spacing.jitter_fraction)) * 0.5
                            : 0.0;

    Arachne::ExtrusionJunctions out;
    out.reserve(line.junctions.size());

    Arachne::ExtrusionJunction *p0 = &line.junctions.front();
    for (auto &p1 : line.junctions) {
        if (p0->p == p1.p) {
            // Degenerate segment — preserve the point.
            out.emplace_back(p1.p, p1.w, p1.perimeter_index);
            continue;
        }
        const Vec2d p0p1      = (p1.p - p0->p).cast<double>();
        const double p0p1_len = p0p1.norm();
        if (p0p1_len < 1.0) { p0 = &p1; continue; }
        const Vec2d dir      = p0p1 / p0p1_len;
        const Vec2d perp_dir = perp(dir);

        double d = dist_left_over;
        while (d < p0p1_len) {
            const Vec2d pos_f = p0->p.cast<double>() + dir * d;
            const double off  = offset_provider(pos_f, perp_dir);
            if (!std::isfinite(off)) { d += next_step(spacing); continue; }
            const Vec2d delta = perp_dir * off;
            out.emplace_back(Point(static_cast<coord_t>(pos_f.x() + delta.x()),
                                   static_cast<coord_t>(pos_f.y() + delta.y())),
                             p1.w, p1.perimeter_index);
            d += next_step(spacing);
        }
        dist_left_over = d - p0p1_len;
        p0 = &p1;
    }

    while (out.size() < 3 && !line.junctions.empty()) {
        const size_t idx = line.junctions.size() - (out.size() + 1);
        out.emplace_back(line.junctions[idx]);
        if (idx == 0) break;
    }

    // Close the loop if the input was a closed curve.
    if (line.junctions.back().p == line.junctions.front().p && !out.empty())
        out.front().p = out.back().p;

    if (out.size() >= 3) line.junctions = std::move(out);
}

} // namespace Slic3r::Feature::SurfaceTexture
