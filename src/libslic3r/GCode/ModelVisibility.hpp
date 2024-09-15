#ifndef libslic3r_ModelVisibility_hpp_
#define libslic3r_ModelVisibility_hpp_

#include <stddef.h>
#include <functional>
#include <vector>
#include <cstddef>

#include "libslic3r/KDTreeIndirect.hpp"
#include "libslic3r/Point.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/TriangleSetSampling.hpp"

namespace Slic3r::ModelInfo {
namespace Impl {

struct CoordinateFunctor
{
    const std::vector<Vec3f> *coordinates;
    CoordinateFunctor(const std::vector<Vec3f> *coords);
    CoordinateFunctor();

    const float &operator()(size_t idx, size_t dim) const;
};
} // namespace Impl

struct Visibility
{
    struct Params
    {
        // Number of samples generated on the mesh. There are
        // sqr_rays_per_sample_point*sqr_rays_per_sample_point rays casted from each samples
        size_t raycasting_visibility_samples_count{};
        size_t fast_decimation_triangle_count_target{};
        // square of number of rays per sample point
        size_t sqr_rays_per_sample_point{};
    };

    Visibility(
        const Transform3d &obj_transform,
        const ModelVolumePtrs &volumes,
        const Params &params,
        const std::function<void(void)> &throw_if_canceled
    );

    TriangleSetSamples mesh_samples;
    std::vector<float> mesh_samples_visibility;
    Impl::CoordinateFunctor mesh_samples_coordinate_functor;
    KDTreeIndirect<3, float, Impl::CoordinateFunctor> mesh_samples_tree{Impl::CoordinateFunctor{}};
    float mesh_samples_radius;

    float calculate_point_visibility(const Vec3f &position) const;
};

} // namespace Slic3r::ModelInfo
#endif // libslic3r_ModelVisibility_hpp_
