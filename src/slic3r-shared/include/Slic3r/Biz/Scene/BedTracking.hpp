#pragma once

#include <chrono>
#include <map>
#include <set>
#include <iterator>

#include "Slic3r/Biz/Algorithms/Bed.hpp"
#include "Slic3r/Domain/BedRef.hpp"
#include "Slic3r/Domain/Polygon.hpp"
#include "Slic3r/Domain/Project.hpp"

namespace Slic3r::Biz {

/**
 * @brief Information on changed bed instances when updating model-instance to bed-instance tracking.
 */
struct BedTrackingChanges
{
    using BedRefSet = std::set<Domain::BedRef>;

    using InstanceRefSet = std::set<Domain::ElementRef>;

    /**
     * @brief Set of changed bed instances
     */
    BedRefSet updated_beds;

    /**
     * @brief Set of ModelInstances which last bed property has changed
     */
    InstanceRefSet updated_instances;

    /**
     * @brief Indicates if Project::unplaced_instances gets updated too.
     */
    bool unplaced_instances_updated{false};

    /**
     * @brief Indicates if BedInstance::colliding_instances gets updated too.
     */
    int colliding_instances_updated_count{0};

    void append(const BedTrackingChanges& others)
    {
        std::ranges::copy(others.updated_beds, std::inserter(updated_beds, updated_beds.end()));
        std::ranges::copy(others.updated_instances, std::inserter(updated_instances, updated_instances.end()));
        unplaced_instances_updated =
            unplaced_instances_updated || others.unplaced_instances_updated;
        colliding_instances_updated_count += others.colliding_instances_updated_count;
    }
};

/**
 * @brief Remove single model instance from bed instance.
 * @param project Project to update
 * @param model_instance Model instance to be removed from bed instance
 */
void remove_instance_from_bed(
    Domain::Project& project,
    Domain::ModelInstance* model_instance,
    BedTrackingChanges& changes
);

class BedTracking
{
public:
    /**
     * @brief Rebuild all model-instance to bed links.
     * @param project Project to update
     */
    BedTrackingChanges update_instances_bed_placement(
        Domain::Project& project
    );

    /**
     * @brief Rebuild model-instance to bed links for given instances
     * @param project Project to update
     * @param instances List of instances to update
     * @param remove_original_links If true the original links are removed before update,
     * if false it is assumed that the instances are newly added and has no original links.
     */
    BedTrackingChanges update_instances_bed_placement(
        Domain::Project& project,
        const Domain::ElementRefs& instances,
        bool remove_original_links = true
    );

    /**
     * @brief Rebuild model-instance to bed links for given instances
     * @param project Project to update
     * @param instances List of instances to update
     * @param remove_original_links If true the original links are removed before update,
     */
    BedTrackingChanges update_instances_bed_placement(
        Domain::Project& project,
        const Domain::ModelInstanceList& instances,
        bool remove_original_links = true
    );

    /**
     * @brief Check whether a 2D shape is contained within the bed boundary.
     *
     * @param bed Bed definition providing the bed contour for collision mesh creation.
     * @param bed_instance Bed instance whose offset is used to transform the tested shape into bed-local coordinates.
     * @param bounding_box Axis-aligned bounding box of the shape to test, in scene coordinates.
     * @param convex_hull Convex hull vertices of the shape to test, in scene coordinates.
     * @return BedContainmentState indicating whether the shape is Inside, Colliding with, or Outside the bed.
     */
    Algorithms::Bed::BedContainmentState check_containment_2d(
        const Domain::Bed& bed,
        const Domain::BedInstance& bed_instance,
        const Domain::BoundingBox2d& bounding_box,
        const Domain::Vec2ds& convex_hull
    );

    /**
     * @brief Check containment of a model instance against the given bed & bed-instance pair,
     *        using the internal instance-collision cache for the 2D footprint/AABB.
     *
     * This is the same logic used by @ref update_instance_bed_placement for normal placement,
     * exposed so callers can ask hypothetical questions like
     * "would this instance be Inside a bed located at transform X?".
     */
    Algorithms::Bed::BedContainmentState check_instance_containment_2d(
        const Domain::Project& project,
        const Domain::ModelInstance& instance,
        const Domain::Bed& bed,
        const Domain::BedInstance& bed_instance
    );

private:
    struct BedCacheEntry
    {
        AABBMesh aabb_mesh;
        Domain::Polygon scaled_contour;
    };

    void update_instance_bed_placement(
        Domain::Project& project,
        Domain::ModelInstance& inst,
        BedTrackingChanges& changes
    );

    const Algorithms::Bed::ObjectCollisionData&
    get_instance_collision_data(const Domain::Project& project, const Domain::ModelInstance& inst);

    BedCacheEntry& get_or_create_bed_cache(const Domain::Bed& bed);

    std::tuple<Domain::ConfigContainer*, Domain::BedInstance*, Algorithms::Bed::BedContainmentState>
    find_bed_instance_for_bounds(
        Domain::Project& project,
        const Algorithms::Bed::ObjectCollisionData& obj_collision_data
    );

    // The cache for transformed bounding boxes to allow quick lookup based on
    // object_id and instance_id.
    struct CacheInstanceEntry
    {
        Domain::Transformation inst_trafo;
        Biz::Algorithms::Bed::ObjectCollisionData collision;
    };

    struct VolData
    {
        Domain::Transformation trafo;
        size_t id;
        Domain::ModelVolumeType type;
    };

    struct CacheObjectEntry
    {
        std::vector<VolData> vol_data;
        std::map<size_t, CacheInstanceEntry> instances;
    };

    std::map<size_t, CacheObjectEntry> m_cache;
    std::chrono::steady_clock::time_point m_last_cache_clear_time =
        std::chrono::steady_clock::now();

    // Cache containing bed contour's AABBMesh and scaled polygon, keyed by bed ID, to avoid expensive recreation.
    std::map<size_t, BedCacheEntry> m_bed_cache;
};

} // namespace Slic3r::Biz
