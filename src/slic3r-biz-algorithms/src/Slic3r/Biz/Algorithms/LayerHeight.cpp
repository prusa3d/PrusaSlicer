#include "Slic3r/Biz/Algorithms/LayerHeight.hpp"

#include "Slic3r/Biz/Algorithms/TriangleMesh.hpp"
#include "Slic3r/Domain/Config.hpp"
#include "Slic3r/Domain/ConfigPack.hpp"
#include "Slic3r/Domain/Constants.hpp"
#include "Slic3r/Domain/ModelInstance.hpp"
#include "Slic3r/Domain/ModelObject.hpp"

#include <algorithm>
#include <cassert>
#include <cfloat>
#include <cmath>

using Slic3r::Domain::ConfigItem;
using Slic3r::Domain::LayerHeightRange;
using Slic3r::Domain::LayerZRanges;
using Slic3r::Domain::VolumeSettings;
using Slic3r::Domain::ZHeightPair;
using Slic3r::Domain::ZHeightPairs;

namespace Slic3r::Biz::Algorithms::LayerHeight {

void adjust_layer_height_profile(
    const AdjustParams& params,
    ZHeightPairs& layer_height_profile,
    double z,
    double layer_thickness_delta,
    double band_width,
    AdjustAction action
)
{
    // Constrain the profile variability by the 1st layer height.
    std::pair<double, double> z_span_variable = std::pair<double, double>(
        params.first_object_layer_height_fixed ? params.first_object_layer_height : 0.,
        params.object_print_z_uncompensated_height
    );
    if (z < z_span_variable.first || z > z_span_variable.second) {
        return;
    }

    assert(!layer_height_profile.empty());
    assert(
        std::abs(layer_height_profile.back().z - params.object_print_z_uncompensated_height)
        < Domain::EPSILON
    );

    // 1) Get the current layer thickness at z.
    double current_layer_height = params.layer_height;
    for (size_t i = 0; i < layer_height_profile.size(); ++i) {
        if (i + 1 == layer_height_profile.size()) {
            current_layer_height = layer_height_profile[i].layer_height;
            break;
        } else if (layer_height_profile[i + 1].z > z) {
            double z1            = layer_height_profile[i].z;
            double h1            = layer_height_profile[i].layer_height;
            double z2            = layer_height_profile[i + 1].z;
            double h2            = layer_height_profile[i + 1].layer_height;
            current_layer_height = std::lerp(h1, h2, (z - z1) / (z2 - z1));
            break;
        }
    }

    // 2) Is it possible to apply the delta?
    switch (action) {
    case AdjustAction::Decrease:
        layer_thickness_delta = -layer_thickness_delta;
        // fallthrough
    case AdjustAction::Increase:
        if (layer_thickness_delta > 0) {
            if (current_layer_height >= params.max_layer_height - Domain::EPSILON) {
                return;
            }

            layer_thickness_delta =
                std::min(layer_thickness_delta, params.max_layer_height - current_layer_height);
        } else {
            if (current_layer_height <= params.min_layer_height + Domain::EPSILON) {
                return;
            }

            layer_thickness_delta =
                std::max(layer_thickness_delta, params.min_layer_height - current_layer_height);
        }
        break;
    case AdjustAction::Reduce:
    case AdjustAction::Smooth:
        layer_thickness_delta = std::abs(layer_thickness_delta);
        layer_thickness_delta =
            std::min(layer_thickness_delta, std::abs(params.layer_height - current_layer_height));
        if (layer_thickness_delta < Domain::EPSILON) {
            return;
        }

        break;
    default:
        assert(false);
        break;
    }

    // 3) Densify the profile inside z +- band_width/2, remove duplicate Zs from the height profile inside the band.
    double lo = std::max(z_span_variable.first, z - 0.5 * band_width);
    // Do not limit the upper side of the band, so that the modifications to the top point of the profile will be allowed.
    double hi     = z + 0.5 * band_width;
    double z_step = 0.1;
    size_t idx    = 0;
    while (idx < layer_height_profile.size() && layer_height_profile[idx].z < lo) {
        ++idx;
    }

    --idx;

    ZHeightPairs profile_new;
    profile_new.reserve(layer_height_profile.size());
    assert(idx + 1 < layer_height_profile.size());
    profile_new.insert(
        profile_new.end(),
        layer_height_profile.begin(),
        layer_height_profile.begin() + idx + 1
    );
    double zz                = lo;
    size_t i_resampled_start = profile_new.size();
    while (zz < hi) {
        size_t next   = idx + 1;
        double z1     = layer_height_profile[idx].z;
        double h1     = layer_height_profile[idx].layer_height;
        double height = h1;
        if (next < layer_height_profile.size()) {
            double z2 = layer_height_profile[next].z;
            double h2 = layer_height_profile[next].layer_height;
            height    = std::lerp(h1, h2, (zz - z1) / (z2 - z1));
        }

        // Adjust height by layer_thickness_delta.
        double weight = std::abs(zz - z) < 0.5 * band_width ?
            (0.5 + 0.5 * cos(2. * M_PI * (zz - z) / band_width)) :
            0.;
        switch (action) {
        case AdjustAction::Increase:
        case AdjustAction::Decrease:
            height += weight * layer_thickness_delta;
            break;
        case AdjustAction::Reduce: {
            double delta = height - params.layer_height;
            double step  = weight * layer_thickness_delta;
            step         = (std::abs(delta) > step) ? (delta > 0) ? -step : step : -delta;
            height += step;
            break;
        }
        case AdjustAction::Smooth: {
            // Don't modify the profile during resampling process, do it at the next step.
            break;
        }
        default:
            assert(false);
            break;
        }

        height = std::clamp(height, params.min_layer_height, params.max_layer_height);
        if (zz == z_span_variable.second) {
            // This is the last point of the profile.
            if (profile_new.back().z + Domain::EPSILON > zz) {
                profile_new.pop_back();
            }

            profile_new.push_back({zz, height});
            idx = layer_height_profile.size();
            break;
        }

        // Avoid entering a too short segment.
        if (profile_new.back().z + Domain::EPSILON < zz) {
            profile_new.push_back({zz, height});
        }

        // Limit zz to the object height, so the next iteration the last profile point will be set.
        zz  = std::min(zz + z_step, z_span_variable.second);
        idx = next;
        while (idx < layer_height_profile.size() && layer_height_profile[idx].z < zz) {
            ++idx;
        }

        --idx;
    }

    ++idx;
    assert(idx > 0);
    size_t i_resampled_end = profile_new.size();
    if (idx < layer_height_profile.size()) {
        assert(zz >= layer_height_profile[idx - 1].z);
        assert(zz <= layer_height_profile[idx].z);
        profile_new.insert(
            profile_new.end(),
            layer_height_profile.begin() + idx,
            layer_height_profile.end()
        );
    } else if (profile_new.back().z + 0.5 * Domain::EPSILON < z_span_variable.second) {
        profile_new.push_back(layer_height_profile.back());
    }

    layer_height_profile = std::move(profile_new);

    if (action == AdjustAction::Smooth) {
        if (i_resampled_start == 0) {
            ++i_resampled_start;
        }

        if (i_resampled_end == layer_height_profile.size()) {
            --i_resampled_end;
        }

        size_t n_rounds = 6;
        for (size_t i_round = 0; i_round < n_rounds; ++i_round) {
            profile_new = layer_height_profile;
            for (size_t i = i_resampled_start; i < i_resampled_end; ++i) {
                double zz = profile_new[i].z;
                double t  = std::abs(zz - z) < 0.5 * band_width ?
                     (0.25 + 0.25 * cos(2. * M_PI * (zz - z) / band_width)) :
                     0.;
                assert(t >= 0. && t <= 0.5000001);
                if (i == 0) {
                    layer_height_profile[i].layer_height = (1. - t) * profile_new[i].layer_height
                        + t * profile_new[i + 1].layer_height;
                } else if (i + 1 == profile_new.size()) {
                    layer_height_profile[i].layer_height = (1. - t) * profile_new[i].layer_height
                        + t * profile_new[i - 1].layer_height;
                } else {
                    layer_height_profile[i].layer_height = (1. - t) * profile_new[i].layer_height
                        + 0.5
                            * t
                            * (profile_new[i - 1].layer_height + profile_new[i + 1].layer_height);
                }
            }
        }
    }

    assert(layer_height_profile.size() > 1);
    assert(layer_height_profile.front().z == 0.);
    assert(
        std::abs(layer_height_profile.back().z - params.object_print_z_uncompensated_height)
        < Domain::EPSILON
    );
#ifdef _DEBUG
    for (size_t i = 1; i < layer_height_profile.size(); ++i) {
        assert(layer_height_profile[i - 1].z <= layer_height_profile[i].z);
    }

    for (size_t i = 0; i < layer_height_profile.size(); ++i) {
        assert(layer_height_profile[i].layer_height > params.min_layer_height - Domain::EPSILON);
        assert(layer_height_profile[i].layer_height < params.max_layer_height + Domain::EPSILON);
    }
#endif /* _DEBUG */
}

ZHeightPairs smooth_height_profile(const ZHeightPairs& profile, const SmoothParams& params)
{
    auto gauss_blur = [&params](const ZHeightPairs& profile) -> ZHeightPairs
    {
        auto gauss_kernel = [](unsigned int radius) -> std::vector<double>
        {
            unsigned int size = 2 * radius + 1;
            std::vector<double> ret;
            ret.reserve(size);

            // Reworked from static inline int getGaussianKernelSize(float sigma) taken from opencv-4.1.2\modules\features2d\src\kaze\AKAZEFeatures.cpp
            double sigma                    = 0.3 * static_cast<double>(radius - 1) + 0.8;
            double two_sq_sigma             = 2.0 * sigma * sigma;
            double inv_root_two_pi_sq_sigma = 1.0 / std::sqrt(M_PI * two_sq_sigma);

            for (unsigned int i = 0; i < size; ++i) {
                double x = static_cast<double>(i) - static_cast<double>(radius);
                ret.push_back(inv_root_two_pi_sq_sigma * std::exp(-x * x / two_sq_sigma));
            }

            return ret;
        };

        // Skip the first layer if fixed.
        size_t skip_count = params.first_object_layer_height_fixed ? 2 : 0;

        // Not enough data to smooth (need at least 3 layers).
        if (static_cast<int>(profile.size()) - static_cast<int>(skip_count) < 3) {
            return profile;
        }

        unsigned int radius        = std::max(params.radius, 1u);
        std::vector<double> kernel = gauss_kernel(radius);
        int radius_int             = static_cast<int>(radius);

        ZHeightPairs ret;
        size_t size = profile.size();
        ret.reserve(size);

        // Leave the first layer untouched
        for (size_t i = 0; i < skip_count; ++i) {
            ret.push_back(profile[i]);
        }

        // Smooth the rest of the profile using biased Gaussian blur.
        // The bias moves the smoothed profile closer to min_layer_height
        double delta_h     = params.max_layer_height - params.min_layer_height;
        double inv_delta_h = (delta_h != 0.0) ? 1.0 / delta_h : 1.0;

        double max_dz_band = static_cast<double>(radius) * params.layer_height;
        for (size_t i = skip_count; i < size; ++i) {
            double zi = profile[i].z;
            double hi = profile[i].layer_height;
            ret.push_back({zi, 0.});
            double& height = ret.back().layer_height;
            int begin = std::max(static_cast<int>(i) - radius_int, static_cast<int>(skip_count));
            int end   = std::min(static_cast<int>(i) + radius_int, static_cast<int>(size) - 1);
            double weight_total = 0.;
            for (int j = begin; j <= end; ++j) {
                int kernel_id = static_cast<int>(radius) + (j - static_cast<int>(i));
                double dz     = std::abs(zi - profile[j].z);
                if (dz * params.layer_height <= max_dz_band) {
                    double dh     = std::abs(params.max_layer_height - profile[j].layer_height);
                    double weight = kernel[kernel_id] * std::sqrt(dh * inv_delta_h);
                    height += weight * profile[j].layer_height;
                    weight_total += weight;
                }
            }

            height = std::clamp(
                weight_total == 0 ? hi : height / weight_total,
                params.min_layer_height,
                params.max_layer_height
            );
            if (params.keep_min) {
                height = std::min(height, hi);
            }
        }

        return ret;
    };

    return gauss_blur(profile);
}

LayerZRanges
generate_object_layers(const GenerateLayersParams& params, const ZHeightPairs& layer_height_profile)
{
    assert(!layer_height_profile.empty());

    double print_z = 0;
    double height  = 0;

    LayerZRanges out;

    if (params.first_object_layer_height_fixed) {
        print_z = params.first_object_layer_height;
        out.push_back({0, print_z});
    }

    const double shrinkage_compensation_z = params.object_shrinkage_compensation_z;
    size_t idx_layer_height_profile       = 0;
    // loop until we have at least one layer and the max slice_z reaches the object height
    double slice_z = print_z + 0.5 * params.min_layer_height;
    while (slice_z < params.object_print_z_height) {
        height = params.min_layer_height;
        if (idx_layer_height_profile < layer_height_profile.size()) {
            size_t next = idx_layer_height_profile + 1;
            for (;;) {
                if (next >= layer_height_profile.size()
                    || slice_z < layer_height_profile[next].z * shrinkage_compensation_z)
                {
                    break;
                }

                idx_layer_height_profile = next;
                ++next;
            }

            const double z1 =
                layer_height_profile[idx_layer_height_profile].z * shrinkage_compensation_z;
            const double h1 = layer_height_profile[idx_layer_height_profile].layer_height;
            height          = h1;
            if (next < layer_height_profile.size()) {
                const double z2 = layer_height_profile[next].z * shrinkage_compensation_z;
                const double h2 = layer_height_profile[next].layer_height;
                height          = std::lerp(h1, h2, (slice_z - z1) / (z2 - z1));
                assert(
                    height >= params.min_layer_height - Domain::EPSILON
                    && height <= params.max_layer_height + Domain::EPSILON
                );
            }
        }
        slice_z = print_z + 0.5 * height;
        if (slice_z >= params.object_print_z_height) {
            break;
        }

        assert(height > params.min_layer_height - Domain::EPSILON);
        assert(height < params.max_layer_height + Domain::EPSILON);
        const double bottom_z = print_z;

        print_z += height;
        slice_z = print_z + 0.5 * params.min_layer_height;
        out.push_back({bottom_z, print_z});
    }

    // FIXME: Adjust the last layer to align with the top object layer exactly?
    return out;
}

ZHeightPairs layer_height_profile_from_ranges(
    const ProfileFromRangesParams& params,
    const Domain::LayerConfigRanges& layer_config_ranges
)
{
    // 1) If there are any height ranges, trim one by the other to make them non-overlapping. Insert the 1st layer if fixed.
    std::vector<std::pair<LayerHeightRange, double>> ranges_non_overlapping;
    ranges_non_overlapping.reserve(layer_config_ranges.size() * 4);
    if (params.first_object_layer_height_fixed) {
        ranges_non_overlapping.emplace_back(
            LayerHeightRange(0., params.first_object_layer_height),
            params.first_object_layer_height
        );
    }

    const auto get_range_layer_height = [&params](const VolumeSettings& settings) -> double
    {
        const std::optional<ConfigItem> layer_height_override =
            settings.overrides.get("layer_height");
        return layer_height_override.has_value() ? layer_height_override->get<double>() :
                                                   params.layer_height;
    };

    // The height ranges are sorted lexicographically by low / high layer boundaries.
    for (const std::pair<const LayerHeightRange, VolumeSettings>& layer_config_range :
         layer_config_ranges)
    {
        double lo = layer_config_range.first.first;
        double hi = std::min(layer_config_range.first.second, params.object_print_z_height);
        const double height = get_range_layer_height(layer_config_range.second);

        if (!ranges_non_overlapping.empty()) {
            // Trim current low with the last high.
            lo = std::max(lo, ranges_non_overlapping.back().first.second);
        }

        if (lo + Domain::EPSILON < hi) {
            // Ignore too narrow ranges.
            ranges_non_overlapping.emplace_back(LayerHeightRange(lo, hi), height);
        }
    }

    // 2) Convert the trimmed ranges to a height profile, fill in the undefined intervals between
    // z=0 and z=params.object_print_z_uncompensated_height with params.layer_height.
    ZHeightPairs layer_height_profile;
    auto last_z = [&layer_height_profile]()
    { return layer_height_profile.empty() ? 0. : layer_height_profile.back().z; };

    auto lh_append = [&layer_height_profile](double z, double layer_height)
    {
        if (!layer_height_profile.empty()) {
            bool last_z_matches = Domain::is_approx(layer_height_profile.back().z, z);
            bool last_h_matches =
                Domain::is_approx(layer_height_profile.back().layer_height, layer_height);
            if (last_h_matches) {
                if (last_z_matches) {
                    // Drop a duplicate.
                    return;
                }

                if (layer_height_profile.size() >= 2
                    && Domain::is_approx(
                        layer_height_profile[layer_height_profile.size() - 2].layer_height,
                        layer_height
                    ))
                {
                    // Third repetition of the same layer_height. Update z of the last entry.
                    layer_height_profile.back().z = z;
                    return;
                }
            }
        }

        layer_height_profile.push_back({z, layer_height});
    };

    for (const std::pair<LayerHeightRange, double>& non_overlapping_range : ranges_non_overlapping)
    {
        double lo     = non_overlapping_range.first.first;
        double hi     = non_overlapping_range.first.second;
        double height = non_overlapping_range.second;
        if (double z = last_z(); lo > z + Domain::EPSILON) {
            // Insert a step of normal layer height.
            lh_append(z, params.layer_height);
            lh_append(lo, params.layer_height);
        }

        // Insert a step of the overridden layer height.
        lh_append(lo, height);
        lh_append(hi, height);
    }

    if (double z = last_z(); z < params.object_print_z_uncompensated_height) {
        // Insert a step of normal layer height up to the object top.
        lh_append(z, params.layer_height);
        lh_append(params.object_print_z_uncompensated_height, params.layer_height);
    }

    return layer_height_profile;
}

bool check_object_layers_fixed(
    const double layer_height,
    const double first_object_layer_height,
    const ZHeightPairs& layer_height_profile
)
{
    assert(layer_height_profile.size() >= 2);
    assert(layer_height_profile.front().z == 0.);

    if (layer_height_profile.size() != 2 && layer_height_profile.size() != 4) {
        return false;
    }

    const bool fixed_step1 = Domain::is_approx(
        layer_height_profile[0].layer_height,
        layer_height_profile[1].layer_height
    );
    const bool fixed_step2 = layer_height_profile.size() == 2
        || (layer_height_profile[1].z == layer_height_profile[2].z
            && Domain::is_approx(
                layer_height_profile[2].layer_height,
                layer_height_profile[3].layer_height
            ));

    if (!fixed_step1 || !fixed_step2) {
        return false;
    }

    if (layer_height_profile[1].z < 0.5 * first_object_layer_height + Domain::EPSILON
        || !Domain::is_approx(layer_height_profile[1].layer_height, first_object_layer_height))
    {
        return false;
    }

    const double z_max = layer_height_profile.back().z;
    const double z_2nd = first_object_layer_height + 0.5 * layer_height;
    if (z_2nd > z_max) {
        return true;
    }

    const ZHeightPair& prev_last = layer_height_profile[layer_height_profile.size() - 2];
    if (z_2nd < prev_last.z + Domain::EPSILON
        || !Domain::is_approx(layer_height_profile.back().layer_height, layer_height))
    {
        return false;
    }

    return true;
}

void clamp_layer_height_profile(
    ZHeightPairs& layer_height_profile,
    const double min_layer_height,
    const double max_layer_height
)
{
    for (ZHeightPair& z_height_pair : layer_height_profile) {
        z_height_pair.layer_height =
            std::clamp(z_height_pair.layer_height, min_layer_height, max_layer_height);
    }
}

} // namespace Slic3r::Biz::Algorithms::LayerHeight

namespace Slic3r::Biz::Algorithms::LayerHeight::Adaptive {

// Based on the work of Florens Waserfall (@platch on github)
// and his paper
// Florens Wasserfall, Norman Hendrich, Jianwei Zhang:
// Adaptive Slicing for the FDM Process Revisited
// 13th IEEE Conference on Automation Science and Engineering (CASE-2017), August 20-23, Xi'an, China. DOI: 10.1109/COASE.2017.8256074
// https://tams.informatik.uni-hamburg.de/publications/2017/Adaptive%20Slicing%20for%20the%20FDM%20Process%20Revisited.pdf

// Vojtech believes that there is a bug in @platch's derivation of the triangle area error metric.
// Following Octave code paints graphs of recommended layer height versus surface slope angle.
/*
adeg=0:1:85;
a=adeg*pi/180;
t=tan(a);
tsqr=sqrt(tan(a));
lerr=1./cos(a);
lerr2=1./(0.3+cos(a));
plot(adeg, t, 'b', adeg, sqrt(t), 'g', adeg, 0.5 * lerr, 'm', adeg, 0.5 * lerr2, 'r')
xlabel("angle(deg), 0 - horizontal wall, 90 - vertical wall");
ylabel("layer height");
legend("tan(a) as cura - topographic lines distance limit", "sqrt(tan(a)) as PrusaSlicer - error triangle area limit", "old slic3r - max distance metric", "new slic3r - Waserfall paper");
*/

struct FaceZ
{
    std::pair<float, float> z_span;
    /**
     * Cosine of the normal vector towards the Z axis.
     */
    float n_cos;
    /**
     * Sine of the normal vector towards the Z axis.
     */
    float n_sin;
};

// By Florens Waserfall aka @platch:
// This constant essentially describes the volumetric error at the surface which is induced
// by stacking "elliptic" extrusion threads. It is empirically determined by
// 1. measuring the surface profile of printed parts to find
// the ratio between layer height and profile height and then
// 2. computing the geometric difference between the model-surface and the elliptic profile.
//
// The definition of the roughness formula is in
// https://tams.informatik.uni-hamburg.de/publications/2017/Adaptive%20Slicing%20for%20the%20FDM%20Process%20Revisited.pdf
// (page 51, formula (8))
// Currenty @platch's error metric formula is not used.
// static constexpr const double SURFACE_CONST = 0.18403;

/**
 * @brief For a given facet, compute maximum height within the allowed surface roughness / stair-stepping deviation.
 */
static inline float layer_height_from_slope(const FaceZ& face, float max_surface_deviation)
{
    // @platch's formula, see his paper "Adaptive Slicing for the FDM Process Revisited".
    // return static_cast<float>(max_surface_deviation / (SURFACE_CONST + 0.5 * std::abs(normal_z)));

    // Constant stepping in horizontal direction, as used by Cura.
    // return (face.n_cos > 1e-5) ? static_cast<float>(max_surface_deviation * face.n_sin / face.n_cos) : FLT_MAX;

    // Constant error measured as an area of the surface error triangle, Vojtech's formula.
    // return (face.n_cos > 1e-5) ? static_cast<float>(1.44 * max_surface_deviation * sqrt(face.n_sin / face.n_cos)) : FLT_MAX;

    // Constant error measured as an area of the surface error triangle, Vojtech's formula with clamping to roughness at 90 degrees.
    return std::min(
        max_surface_deviation / 0.184f,
        (face.n_cos > 1e-5) ?
            static_cast<float>(1.44 * max_surface_deviation * sqrt(face.n_sin / face.n_cos)) :
            FLT_MAX
    );

    // Constant stepping along the surface, equivalent to the "surface roughness" metric by Perez and later Pandey et all, see @platch's paper for references.
    // return static_cast<float>(max_surface_deviation * face.n_sin);
}

static std::vector<FaceZ> prepare_faces(const Domain::ModelObject& object)
{
    Domain::TriangleMesh mesh                   = object.raw_mesh();
    const Domain::ModelInstance& first_instance = *object.instances.front();
    mesh.transform(first_instance.get_matrix(), first_instance.is_left_handed());

    // 1) Collect faces from mesh.
    std::vector<FaceZ> faces;
    faces.reserve(mesh.facets_count());
    for (stl_triangle_vertex_indices face : mesh.its.indices) {
        Domain::Vec3f vertex[3] =
            {mesh.its.vertices[face[0]], mesh.its.vertices[face[1]], mesh.its.vertices[face[2]]};
        Domain::Vec3f n = TriangleMesh::face_normal_normalized(vertex);
        std::pair<float, float> face_z_span{
            std::min({vertex[0].z(), vertex[1].z(), vertex[2].z()}),
            std::max({vertex[0].z(), vertex[1].z(), vertex[2].z()})
        };
        faces.emplace_back(
            FaceZ{face_z_span, std::abs(n.z()), std::sqrt(n.x() * n.x() + n.y() * n.y())}
        );
    }

    // 2) Sort faces lexicographically by their Z span.
    std::ranges::sort(
        faces,
        [](const FaceZ& f1, const FaceZ& f2) { return f1.z_span < f2.z_span; }
    );

    return faces;
}

/**
 * @brief Returns height of the next layer.
 *
 * @param current_facet Is in/out parameter, remembers the index of the last face of m_faces visited, where this function will start from.
 * @param print_z The top print surface of the previous layer.
 * @return Height of the next layer.
 */
float next_layer_height(
    const AdaptiveParams& params,
    const std::vector<FaceZ>& faces,
    const float print_z,
    float quality_factor,
    size_t& current_facet
)
{
    float height = static_cast<float>(params.max_layer_height);

    float max_surface_deviation;
    {
#if 0
// @platch's formula for quality:
	    double delta_min = SURFACE_CONST * params.min_layer_height;
	    double delta_mid = (SURFACE_CONST + 0.5) * params.layer_height;
	    double delta_max = (SURFACE_CONST + 0.5) * params.max_layer_height;
#else
        // Vojtech's formula for triangle area error metric.
        double delta_min = params.min_layer_height;
        double delta_mid = params.layer_height;
        double delta_max = params.max_layer_height;
#endif
        max_surface_deviation = (quality_factor < 0.5f) ?
            std::lerp(delta_min, delta_mid, 2. * quality_factor) :
            std::lerp(delta_max, delta_mid, 2. * (1. - quality_factor));
    }

    // Find all facets intersecting the slice-layer.
    size_t ordered_id = current_facet;
    {
        bool first_hit = false;
        for (; ordered_id < faces.size(); ++ordered_id) {
            const std::pair<float, float>& zspan = faces[ordered_id].z_span;
            // Facet's minimum is higher than slice_z -> end loop
            if (zspan.first >= print_z) {
                break;
            }

            // Facet's maximum is higher than slice_z -> store the first event for the next cusp_height call to begin at this point.
            if (zspan.second > print_z) {
                // First event?
                if (!first_hit) {
                    first_hit     = true;
                    current_facet = ordered_id;
                }

                // Skip touching facets which could otherwise cause small cusp values.
                if (zspan.second < print_z + Domain::EPSILON) {
                    continue;
                }

                // Compute cusp-height for this facet and store a minimum of all heights.
                height = std::min(
                    height,
                    layer_height_from_slope(faces[ordered_id], max_surface_deviation)
                );
            }
        }
    }

    // Lower height limit due to printer capabilities.
    height = std::max(height, static_cast<float>(params.min_layer_height));

    // Check for sloped facets inside the determined layer and the correct height if necessary.
    if (height > static_cast<float>(params.min_layer_height)) {
        for (; ordered_id < faces.size(); ++ordered_id) {
            const std::pair<float, float>& zspan = faces[ordered_id].z_span;
            // Facet's minimum is higher than slice_z + height -> end loop
            if (zspan.first >= print_z + height) {
                break;
            }

            // Skip touching facets which could otherwise cause small cusp values.
            if (zspan.second < print_z + Domain::EPSILON) {
                continue;
            }

            // Compute cusp-height for this facet and check against height.
            float reduced_height =
                layer_height_from_slope(faces[ordered_id], max_surface_deviation);

            float z_diff = zspan.first - print_z;
            if (reduced_height < z_diff) {
                assert(z_diff < height + Domain::EPSILON);
                // The currently visited triangle's slope limits the next layer height so much that
                // the lowest point of the currently visible triangle is already above the newly proposed layer height.
                // This means that we need to limit the layer height so that the offending newly visited triangle
                // is just above of the new layer.
                height = z_diff;
            } else if (reduced_height < height) {
                height = reduced_height;
            }
        }

        // Lower height limit due to printer capabilities again.
        height = std::max(height, static_cast<float>(params.min_layer_height));
    }

    return height;
}

} // namespace Slic3r::Biz::Algorithms::LayerHeight::Adaptive

namespace Slic3r::Biz::Algorithms::LayerHeight {

/**
 * @brief Fill layer_height_profile by heights ensuring a prescribed maximum cusp height.
 *
 * @note Based on the work of @platsch.
 */
ZHeightPairs layer_height_profile_adaptive(
    const AdaptiveParams& params,
    const Domain::ModelObject& object,
    const float quality_factor
)
{
    // 1) Initialize the SlicingAdaptive class with the object meshes.
    const std::vector<Adaptive::FaceZ> faces = Adaptive::prepare_faces(object);

    // 2) Generate layers using the algorithm of @platsch.
    ZHeightPairs layer_height_profile;
    layer_height_profile.push_back({0., params.first_object_layer_height});
    if (params.first_object_layer_height_fixed) {
        layer_height_profile.push_back(
            {params.first_object_layer_height, params.first_object_layer_height}
        );
    }

    double print_z = params.first_object_layer_height;
    // Last facet visited by the Adaptive::next_layer_height() function, where the facets are sorted by their increasing Z span.
    size_t current_facet = 0;
    // Loop until we have at least one layer and the max slice_z reaches the object height.
    while (print_z + Domain::EPSILON < params.object_print_z_uncompensated_height) {
        float height = static_cast<float>(params.max_layer_height);
        // Determine the next layer height.
        const float cusp_height = Adaptive::next_layer_height(
            params,
            faces,
            static_cast<float>(print_z),
            quality_factor,
            current_facet
        );

        height = std::min(cusp_height, height);

        layer_height_profile.push_back({print_z, static_cast<double>(height)});
        print_z += height;
    }

    const double z_gap = params.object_print_z_uncompensated_height - layer_height_profile.back().z;
    if (z_gap > 0.0) {
        layer_height_profile.push_back(
            {params.object_print_z_uncompensated_height,
             std::clamp(z_gap, params.min_layer_height, params.max_layer_height)}
        );
    }

    return layer_height_profile;
}

} // namespace Slic3r::Biz::Algorithms::LayerHeight
