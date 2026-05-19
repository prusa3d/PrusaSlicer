// Port of stlTexturizer/js/mapping.js to C++.

#include "UVProjection.hpp"

#include <algorithm>
#include <cmath>

namespace Slic3r::Feature::TextureSkin {

static constexpr double TWO_PI = 2.0 * M_PI;
static constexpr double CUBIC_AXIS_EPSILON = 1e-4;

static inline double fract(double x) { return x - std::floor(x); }

static inline double smoothstep(double t) { return t * t * (3.0 - 2.0 * t); }

static inline UVSample apply_transform(double u, double v,
                                       double scale_u, double scale_v,
                                       double offset_u, double offset_v,
                                       double rot_rad)
{
    double uu = u / scale_u + offset_u;
    double vv = v / scale_v + offset_v;
    if (rot_rad != 0.0) {
        const double c = std::cos(rot_rad);
        const double s = std::sin(rot_rad);
        uu -= 0.5;
        vv -= 0.5;
        const double ru = c * uu - s * vv;
        const double rv = s * uu + c * vv;
        uu = ru + 0.5;
        vv = rv + 0.5;
    }
    return UVSample{fract(uu), fract(vv), 1.0};
}

enum class CubicAxis { X, Y, Z };

static inline CubicAxis dominant_cubic_axis(const Vec3d &n)
{
    const double ax = std::abs(n.x());
    const double ay = std::abs(n.y());
    const double az = std::abs(n.z());
    if (ax >= ay - CUBIC_AXIS_EPSILON && ax >= az - CUBIC_AXIS_EPSILON) return CubicAxis::X;
    if (ay >= az - CUBIC_AXIS_EPSILON) return CubicAxis::Y;
    return CubicAxis::Z;
}

static inline bool is_ambiguous_cubic_normal(const Vec3d &n)
{
    const double ax = std::abs(n.x());
    const double ay = std::abs(n.y());
    const double az = std::abs(n.z());
    const CubicAxis axis = dominant_cubic_axis(n);
    const double primary   = axis == CubicAxis::X ? ax : axis == CubicAxis::Y ? ay : az;
    const double secondary = axis == CubicAxis::X ? std::max(ay, az)
                           : axis == CubicAxis::Y ? std::max(ax, az)
                                                   : std::max(ax, ay);
    return primary - secondary <= CUBIC_AXIS_EPSILON;
}

struct CubicWeights { double x, y, z; };

static CubicWeights cubic_blend_weights(const Vec3d &n, double blend, double seam_band_width)
{
    const CubicAxis axis = dominant_cubic_axis(n);
    const double ax = std::abs(n.x());
    const double ay = std::abs(n.y());
    const double az = std::abs(n.z());
    const double primary   = axis == CubicAxis::X ? ax : axis == CubicAxis::Y ? ay : az;
    const double secondary = axis == CubicAxis::X ? std::max(ay, az)
                           : axis == CubicAxis::Y ? std::max(ax, az)
                                                   : std::max(ax, ay);

    CubicWeights one_hot{axis == CubicAxis::X ? 1.0 : 0.0,
                         axis == CubicAxis::Y ? 1.0 : 0.0,
                         axis == CubicAxis::Z ? 1.0 : 0.0};

    if (blend <= 0.001 || is_ambiguous_cubic_normal(n))
        return one_hot;

    const double seam_width = std::max(seam_band_width, CUBIC_AXIS_EPSILON * 2.0);
    const double seam_mix_raw = 1.0 - std::clamp((primary - secondary) / seam_width, 0.0, 1.0);
    const double seam_mix = blend * smoothstep(seam_mix_raw);
    if (seam_mix <= 0.001) return one_hot;

    const double power = 1.0 + (1.0 - seam_mix) * 11.0;
    const double sx = std::pow(ax, power);
    const double sy = std::pow(ay, power);
    const double sz = std::pow(az, power);
    const double smooth_sum = sx + sy + sz + 1e-6;
    const CubicWeights smooth{sx / smooth_sum, sy / smooth_sum, sz / smooth_sum};

    const double mx = one_hot.x * (1.0 - seam_mix) + smooth.x * seam_mix;
    const double my = one_hot.y * (1.0 - seam_mix) + smooth.y * seam_mix;
    const double mz = one_hot.z * (1.0 - seam_mix) + smooth.z * seam_mix;
    const double sum = mx + my + mz;
    return {mx / sum, my / sum, mz / sum};
}

static inline UVResult single(const UVSample &s)
{
    UVResult r;
    r.samples[0] = s;
    r.count = 1;
    return r;
}

UVResult compute_uv(const Vec3d &pos, const Vec3d &normal, UVMode mode,
                    const UVSettings &settings, const BoundingBoxf3 &bounds)
{
    const Vec3d min    = bounds.min;
    const Vec3d size   = bounds.size();
    const Vec3d center = bounds.center();

    const double aU = settings.texture_aspect_u != 0.0 ? settings.texture_aspect_u : 1.0;
    const double aV = settings.texture_aspect_v != 0.0 ? settings.texture_aspect_v : 1.0;
    const double scale_u  = (settings.scale_u != 0.0 ? settings.scale_u : 1.0) / aU;
    const double scale_v  = (settings.scale_v != 0.0 ? settings.scale_v : 1.0) / aV;
    const double offset_u = settings.offset_u;
    const double offset_v = settings.offset_v;
    const double rot_rad  = settings.rotation_deg * M_PI / 180.0;

    const double max_dim = std::max({size.x(), size.y(), size.z()});
    const double md = std::max(max_dim, 1e-6);

    double u = 0.0, v = 0.0;

    switch (mode) {
    case UVMode::PlanarXY:
        u = (pos.x() - min.x()) / md;
        v = (pos.y() - min.y()) / md;
        return single(apply_transform(u, v, scale_u, scale_v, offset_u, offset_v, rot_rad));

    case UVMode::PlanarXZ:
        u = (pos.x() - min.x()) / md;
        v = (pos.z() - min.z()) / md;
        return single(apply_transform(u, v, scale_u, scale_v, offset_u, offset_v, rot_rad));

    case UVMode::PlanarYZ:
        u = (pos.y() - min.y()) / md;
        v = (pos.z() - min.z()) / md;
        return single(apply_transform(u, v, scale_u, scale_v, offset_u, offset_v, rot_rad));

    case UVMode::Cylindrical: {
        const double r  = std::max(size.x(), size.y()) * 0.5;
        const double C  = TWO_PI * std::max(r, 1e-6);
        const double rx = pos.x() - center.x();
        const double ry = pos.y() - center.y();
        const double blend = settings.mapping_blend;
        const double theta = std::atan2(ry, rx);
        const double u_raw = (theta / TWO_PI) + 0.5;
        const double v_side = (pos.z() - min.z()) / C;

        const double seam_band = settings.seam_band_width * 0.1;
        const double seam_dist = std::min(u_raw, 1.0 - u_raw);
        const bool in_seam_zone = seam_band > 0.001 && seam_dist < seam_band;

        UVResult result;
        if (in_seam_zone) {
            const double d = u_raw < 0.5 ? u_raw : u_raw - 1.0;
            const double t_raw = (d + seam_band) / (2.0 * seam_band);
            const double t = smoothstep(t_raw);
            UVSample t_left  = apply_transform(1.0 + d, v_side, scale_u, scale_v, offset_u, offset_v, rot_rad);
            UVSample t_right = apply_transform(d,       v_side, scale_u, scale_v, offset_u, offset_v, rot_rad);
            t_right.w = t;
            t_left.w  = 1.0 - t;
            result.samples[0] = t_right;
            result.samples[1] = t_left;
            result.count = 2;
        } else {
            result.samples[0] = apply_transform(u_raw, v_side, scale_u, scale_v, offset_u, offset_v, rot_rad);
            result.count = 1;
        }

        if (blend <= 0.001)
            return result;

        const double cap_threshold = std::cos(settings.cap_angle_deg * M_PI / 180.0);
        const double blend_half = settings.seam_band_width * 0.5;
        const double abs_nz = std::abs(normal.z());
        const double cap_w = std::clamp((abs_nz - (cap_threshold - blend_half)) / (2.0 * blend_half + 1e-6), 0.0, 1.0);

        if (cap_w <= 0.0) return result;

        const double u_cap = rx / C + 0.5;
        const double v_cap = ry / C + 0.5;
        UVSample t_cap = apply_transform(u_cap, v_cap, scale_u, scale_v, offset_u, offset_v, rot_rad);

        if (cap_w >= 1.0) {
            return single(t_cap);
        }

        // Combine side samples (scaled by 1-capW) with cap sample.
        for (int i = 0; i < result.count; ++i)
            result.samples[i].w *= (1.0 - cap_w);
        t_cap.w = cap_w;
        result.samples[result.count++] = t_cap;
        return result;
    }

    case UVMode::Spherical: {
        const double rx = pos.x() - center.x();
        const double ry = pos.y() - center.y();
        const double rz = pos.z() - center.z();
        const double r  = std::sqrt(rx*rx + ry*ry + rz*rz);
        const double phi   = std::acos(std::clamp(rz / std::max(r, 1e-6), -1.0, 1.0));
        const double theta = std::atan2(ry, rx);
        const double u_raw = (theta / TWO_PI) + 0.5;
        const double v_raw = phi / M_PI;

        const double seam_band = settings.seam_band_width * 0.1;
        const double seam_dist = std::min(u_raw, 1.0 - u_raw);
        if (seam_band > 0.001 && seam_dist < seam_band) {
            const double d = u_raw < 0.5 ? u_raw : u_raw - 1.0;
            const double t_raw = (d + seam_band) / (2.0 * seam_band);
            const double t = smoothstep(t_raw);
            UVSample t_left  = apply_transform(1.0 + d, v_raw, scale_u, scale_v, offset_u, offset_v, rot_rad);
            UVSample t_right = apply_transform(d,       v_raw, scale_u, scale_v, offset_u, offset_v, rot_rad);
            t_right.w = t;
            t_left.w  = 1.0 - t;
            UVResult result;
            result.samples[0] = t_right;
            result.samples[1] = t_left;
            result.count = 2;
            return result;
        }

        return single(apply_transform(u_raw, v_raw, scale_u, scale_v, offset_u, offset_v, rot_rad));
    }

    case UVMode::Cubic: {
        const CubicWeights w = cubic_blend_weights(normal, settings.mapping_blend, settings.seam_band_width);
        double yzU = (pos.y() - min.y()) / md; if (normal.x() < 0) yzU = -yzU;
        double xzU = (pos.x() - min.x()) / md; if (normal.y() > 0) xzU = -xzU;
        double xyU = (pos.x() - min.x()) / md; if (normal.z() < 0) xyU = -xyU;
        UVSample tYZ = apply_transform(yzU, (pos.z() - min.z()) / md, scale_u, scale_v, offset_u, offset_v, rot_rad);
        UVSample tXZ = apply_transform(xzU, (pos.z() - min.z()) / md, scale_u, scale_v, offset_u, offset_v, rot_rad);
        UVSample tXY = apply_transform(xyU, (pos.y() - min.y()) / md, scale_u, scale_v, offset_u, offset_v, rot_rad);

        if (w.x > 0.999) return single(tYZ);
        if (w.y > 0.999) return single(tXZ);
        if (w.z > 0.999) return single(tXY);

        UVResult result;
        tXY.w = w.z; result.samples[0] = tXY;
        tXZ.w = w.y; result.samples[1] = tXZ;
        tYZ.w = w.x; result.samples[2] = tYZ;
        result.count = 3;
        return result;
    }

    case UVMode::Triplanar:
    default: {
        const double ax = std::abs(normal.x());
        const double ay = std::abs(normal.y());
        const double az = std::abs(normal.z());
        constexpr double pw = 4.0;
        const double bx = std::pow(ax, pw);
        const double by = std::pow(ay, pw);
        const double bz = std::pow(az, pw);
        const double sum = bx + by + bz + 1e-6;
        const double wx = bx / sum;
        const double wy = by / sum;
        const double wz = bz / sum;

        double yzU = (pos.y() - min.y()) / md; if (normal.x() < 0) yzU = -yzU;
        double xzU = (pos.x() - min.x()) / md; if (normal.y() > 0) xzU = -xzU;
        double xyU = (pos.x() - min.x()) / md; if (normal.z() < 0) xyU = -xyU;

        UVSample xy = apply_transform(xyU, (pos.y() - min.y()) / md, scale_u, scale_v, offset_u, offset_v, rot_rad);
        UVSample xz = apply_transform(xzU, (pos.z() - min.z()) / md, scale_u, scale_v, offset_u, offset_v, rot_rad);
        UVSample yz = apply_transform(yzU, (pos.z() - min.z()) / md, scale_u, scale_v, offset_u, offset_v, rot_rad);
        xy.w = wz; xz.w = wy; yz.w = wx;

        UVResult result;
        result.samples[0] = xy;
        result.samples[1] = xz;
        result.samples[2] = yz;
        result.count = 3;
        return result;
    }
    }
}

} // namespace Slic3r::Feature::TextureSkin
