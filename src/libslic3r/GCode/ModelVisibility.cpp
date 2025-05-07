#include <boost/log/trivial.hpp>
#include <igl/Hit.h>
#include <oneapi/tbb/blocked_range.h>
#include <oneapi/tbb/parallel_for.h>
#include <cmath>
#include <cstdlib>

#include "libslic3r/ShortEdgeCollapse.hpp"
#include "libslic3r/GCode/ModelVisibility.hpp"
#include "libslic3r/AABBTreeIndirect.hpp"
#include "admesh/stl.h"
#include "libslic3r/TriangleMesh.hpp"
#include "libslic3r/libslic3r.h"

namespace Slic3r::ModelInfo {
namespace Impl {

CoordinateFunctor::CoordinateFunctor(const std::vector<Vec3f> *coords) : coordinates(coords) {}
CoordinateFunctor::CoordinateFunctor() : coordinates(nullptr) {}

const float &CoordinateFunctor::operator()(size_t idx, size_t dim) const {
    return coordinates->operator[](idx)[dim];
}


template<typename T> int sgn(T val) {
    return int(T(0) < val) - int(val < T(0));
}

/// Coordinate frame
class Frame {
public:
    Frame() {
        mX = Vec3f(1, 0, 0);
        mY = Vec3f(0, 1, 0);
        mZ = Vec3f(0, 0, 1);
    }

    Frame(const Vec3f &x, const Vec3f &y, const Vec3f &z) :
            mX(x), mY(y), mZ(z) {
    }

    void set_from_z(const Vec3f &z) {
        mZ = z.normalized();
        Vec3f tmpZ = mZ;
        Vec3f tmpX = (std::abs(tmpZ.x()) > 0.99f) ? Vec3f(0, 1, 0) : Vec3f(1, 0, 0);
        mY = (tmpZ.cross(tmpX)).normalized();
        mX = mY.cross(tmpZ);
    }

    Vec3f to_world(const Vec3f &a) const {
        return a.x() * mX + a.y() * mY + a.z() * mZ;
    }

    Vec3f to_local(const Vec3f &a) const {
        return Vec3f(mX.dot(a), mY.dot(a), mZ.dot(a));
    }

    const Vec3f& binormal() const {
        return mX;
    }

    const Vec3f& tangent() const {
        return mY;
    }

    const Vec3f& normal() const {
        return mZ;
    }

private:
    Vec3f mX, mY, mZ;
};

Vec3f sample_sphere_uniform(const Vec2f &samples) {
    float term1 = 2.0f * float(PI) * samples.x();
    float term2 = 2.0f * sqrt(samples.y() - samples.y() * samples.y());
    return {cos(term1) * term2, sin(term1) * term2,
        1.0f - 2.0f * samples.y()};
}

Vec3f sample_hemisphere_uniform(const Vec2f &samples) {
    float term1 = 2.0f * float(PI) * samples.x();
    float term2 = 2.0f * sqrt(samples.y() - samples.y() * samples.y());
    return {cos(term1) * term2, sin(term1) * term2,
        abs(1.0f - 2.0f * samples.y())};
}

Vec3f sample_power_cosine_hemisphere(const Vec2f &samples, float power) {
    float term1 = 2.f * float(PI) * samples.x();
    float term2 = pow(samples.y(), 1.f / (power + 1.f));
    float term3 = sqrt(1.f - term2 * term2);

    return Vec3f(cos(term1) * term3, sin(term1) * term3, term2);
}

std::vector<float> raycast_visibility(
    const AABBTreeIndirect::Tree<3, float> &raycasting_tree,
    const indexed_triangle_set &triangles,
    const TriangleSetSamples &samples,
    size_t negative_volumes_start_index,
    const Visibility::Params &params
) {
    BOOST_LOG_TRIVIAL(debug)
    << "SeamPlacer: raycast visibility of " << samples.positions.size() << " samples over " << triangles.indices.size()
            << " triangles: end";

    //prepare uniform samples of a hemisphere
    float step_size = 1.0f / params.sqr_rays_per_sample_point;
    std::vector<Vec3f> precomputed_sample_directions(
            params.sqr_rays_per_sample_point * params.sqr_rays_per_sample_point);
    for (size_t x_idx = 0; x_idx < params.sqr_rays_per_sample_point; ++x_idx) {
        float sample_x = x_idx * step_size + step_size / 2.0;
        for (size_t y_idx = 0; y_idx < params.sqr_rays_per_sample_point; ++y_idx) {
            size_t dir_index = x_idx * params.sqr_rays_per_sample_point + y_idx;
            float sample_y = y_idx * step_size + step_size / 2.0;
            precomputed_sample_directions[dir_index] = sample_hemisphere_uniform( { sample_x, sample_y });
        }
    }

    bool model_contains_negative_parts = negative_volumes_start_index < triangles.indices.size();

    std::vector<float> result(samples.positions.size());
    tbb::parallel_for(tbb::blocked_range<size_t>(0, result.size()),
            [&triangles, &precomputed_sample_directions, model_contains_negative_parts, negative_volumes_start_index,
                    &raycasting_tree, &result, &samples, &params](tbb::blocked_range<size_t> r) {
                // Maintaining hits memory outside of the loop, so it does not have to be reallocated for each query.
                std::vector<igl::Hit> hits;
                for (size_t s_idx = r.begin(); s_idx < r.end(); ++s_idx) {
                    result[s_idx] = 1.0f;
                    const float decrease_step = 1.0f
                            / (params.sqr_rays_per_sample_point * params.sqr_rays_per_sample_point);

                    const Vec3f &center = samples.positions[s_idx];
                    const Vec3f &normal = samples.normals[s_idx];
                    // apply the local direction via Frame struct - the local_dir is with respect to +Z being forward
                    Frame f;
                    f.set_from_z(normal);

                    for (const auto &dir : precomputed_sample_directions) {
                        Vec3f final_ray_dir = (f.to_world(dir));
                        if (!model_contains_negative_parts) {
                            igl::Hit hitpoint;
                            // FIXME: This AABBTTreeIndirect query will not compile for float ray origin and
                            // direction.
                            Vec3d final_ray_dir_d = final_ray_dir.cast<double>();
                            Vec3d ray_origin_d = (center + normal * 0.01f).cast<double>(); // start above surface.
                            bool hit = AABBTreeIndirect::intersect_ray_first_hit(triangles.vertices,
                                    triangles.indices, raycasting_tree, ray_origin_d, final_ray_dir_d, hitpoint);
                            if (hit && its_face_normal(triangles, hitpoint.id).dot(final_ray_dir) <= 0) {
                                result[s_idx] -= decrease_step;
                            }
                        } else { //TODO improve logic for order based boolean operations - consider order of volumes
                            bool casting_from_negative_volume = samples.triangle_indices[s_idx]
                                    >= negative_volumes_start_index;

                            Vec3d ray_origin_d = (center + normal * 0.01f).cast<double>(); // start above surface.
                            if (casting_from_negative_volume) { // if casting from negative volume face, invert direction, change start pos
                                final_ray_dir = -1.0 * final_ray_dir;
                                ray_origin_d = (center - normal * 0.01f).cast<double>();
                            }
                            Vec3d final_ray_dir_d = final_ray_dir.cast<double>();
                            bool some_hit = AABBTreeIndirect::intersect_ray_all_hits(triangles.vertices,
                                    triangles.indices, raycasting_tree,
                                    ray_origin_d, final_ray_dir_d, hits);
                            if (some_hit) {
                                int counter = 0;
                                // NOTE: iterating in reverse, from the last hit for one simple reason: We know the state of the ray at that point;
                                //  It cannot be inside model, and it cannot be inside negative volume
                                for (int hit_index = int(hits.size()) - 1; hit_index >= 0; --hit_index) {
                                    Vec3f face_normal = its_face_normal(triangles, hits[hit_index].id);
                                    if (hits[hit_index].id >= int(negative_volumes_start_index)) { //negative volume hit
                                        counter -= sgn(face_normal.dot(final_ray_dir)); // if volume face aligns with ray dir, we are leaving negative space
                                        // which in reverse hit analysis means, that we are entering negative space :) and vice versa
                                    } else {
                                        counter += sgn(face_normal.dot(final_ray_dir));
                                    }
                                }
                                if (counter == 0) {
                                    result[s_idx] -= decrease_step;
                                }
                            }
                        }
                    }
                }
            });

    BOOST_LOG_TRIVIAL(debug)
    << "SeamPlacer: raycast visibility of " << samples.positions.size() << " samples over " << triangles.indices.size()
            << " triangles: end";

    return result;
}
}

Visibility::Visibility(
    const Transform3d &obj_transform,
    const ModelVolumePtrs &volumes,
    const Params &params,
    const std::function<void(void)> &throw_if_canceled
) {
    BOOST_LOG_TRIVIAL(debug)
    << "SeamPlacer: gather occlusion meshes: start";
    indexed_triangle_set triangle_set;
    indexed_triangle_set negative_volumes_set;
    //add all parts
    for (const ModelVolume *model_volume : volumes) {
        if (model_volume->type() == ModelVolumeType::MODEL_PART
                || model_volume->type() == ModelVolumeType::NEGATIVE_VOLUME) {
            auto model_transformation = model_volume->get_matrix();
            indexed_triangle_set model_its = model_volume->mesh().its;
            its_transform(model_its, model_transformation);
            if (model_volume->type() == ModelVolumeType::MODEL_PART) {
                its_merge(triangle_set, model_its);
            } else {
                its_merge(negative_volumes_set, model_its);
            }
        }
    }
    throw_if_canceled();

    BOOST_LOG_TRIVIAL(debug)
    << "SeamPlacer: gather occlusion meshes: end";

    BOOST_LOG_TRIVIAL(debug)
    << "SeamPlacer: decimate: start";
    its_short_edge_collpase(triangle_set, params.fast_decimation_triangle_count_target);
    its_short_edge_collpase(negative_volumes_set, params.fast_decimation_triangle_count_target);

    size_t negative_volumes_start_index = triangle_set.indices.size();
    its_merge(triangle_set, negative_volumes_set);
    its_transform(triangle_set, obj_transform);
    BOOST_LOG_TRIVIAL(debug)
    << "SeamPlacer: decimate: end";

    BOOST_LOG_TRIVIAL(debug)
    << "SeamPlacer: Compute visibility sample points: start";

    this->mesh_samples = sample_its_uniform_parallel(params.raycasting_visibility_samples_count,
            triangle_set);
    this->mesh_samples_coordinate_functor = Impl::CoordinateFunctor(&this->mesh_samples.positions);
    this->mesh_samples_tree = KDTreeIndirect<3, float, Impl::CoordinateFunctor>(this->mesh_samples_coordinate_functor,
            this->mesh_samples.positions.size());

    // The following code determines search area for random visibility samples on the mesh when calculating visibility of each perimeter point
    // number of random samples in the given radius (area) is approximately poisson distribution
    // to compute ideal search radius (area), we use exponential distribution (complementary distr to poisson)
    // parameters of exponential distribution to compute area that will have with probability="probability" more than given number of samples="samples"
    float probability = 0.9f;
    float samples = 4;
    float density = params.raycasting_visibility_samples_count / this->mesh_samples.total_area;
    // exponential probability distrubtion function is : f(x) = P(X > x) = e^(l*x) where l is the rate parameter (computed as 1/u where u is mean value)
    // probability that sampled area A with S samples contains more than samples count:
    //  P(S > samples in A) = e^-(samples/(density*A));   express A:
    float search_area = samples / (-logf(probability) * density);
    float search_radius = sqrt(search_area / PI);
    this->mesh_samples_radius = search_radius;

    BOOST_LOG_TRIVIAL(debug)
    << "SeamPlacer: Compute visiblity sample points: end";
    throw_if_canceled();

    BOOST_LOG_TRIVIAL(debug)
    << "SeamPlacer: Mesh sample raidus: " << this->mesh_samples_radius;

    BOOST_LOG_TRIVIAL(debug)
    << "SeamPlacer: build AABB tree: start";
    auto raycasting_tree = AABBTreeIndirect::build_aabb_tree_over_indexed_triangle_set(triangle_set.vertices,
            triangle_set.indices);

    throw_if_canceled();
    BOOST_LOG_TRIVIAL(debug)
    << "SeamPlacer: build AABB tree: end";
    this->mesh_samples_visibility = Impl::raycast_visibility(raycasting_tree, triangle_set, this->mesh_samples,
            negative_volumes_start_index, params);
    throw_if_canceled();
}

float Visibility::calculate_point_visibility(const Vec3f &position) const {
    std::vector<size_t> points = find_nearby_points(mesh_samples_tree, position, mesh_samples_radius);
    if (points.empty()) {
        return 1.0f;
    }

    auto compute_dist_to_plane = [](const Vec3f &position, const Vec3f &plane_origin,
                                    const Vec3f &plane_normal) {
        Vec3f orig_to_point = position - plane_origin;
        return std::abs(orig_to_point.dot(plane_normal));
    };

    float total_weight = 0;
    float total_visibility = 0;
    for (size_t i = 0; i < points.size(); ++i) {
        size_t sample_idx = points[i];

        Vec3f sample_point = this->mesh_samples.positions[sample_idx];
        Vec3f sample_normal = this->mesh_samples.normals[sample_idx];

        float weight = mesh_samples_radius -
            compute_dist_to_plane(position, sample_point, sample_normal);
        weight += (mesh_samples_radius - (position - sample_point).norm());
        total_visibility += weight * mesh_samples_visibility[sample_idx];
        total_weight += weight;
    }

    return total_visibility / total_weight;
}

}
