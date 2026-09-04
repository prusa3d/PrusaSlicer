#include "Slic3r/Biz/Scene/BedTracking.hpp"

#include "Slic3r/Domain/Project.hpp"
#include "Slic3r/Biz/Algorithms/BoundingBox.hpp"
#include "Slic3r/Biz/Algorithms/ModelObject.hpp"
#include "Slic3r/Biz/Algorithms/Point.hpp"
#include "Slic3r/Biz/Algorithms/Polygon.hpp"
#include "Slic3r/Domain/Model.hpp"

#include <tracy/Tracy.hpp>

using Slic3r::Biz::Algorithms::Bed::BedContainmentState;
using Slic3r::Biz::Algorithms::Bed::BedInstanceCollisionData;
using Slic3r::Domain::Bed;
using Slic3r::Domain::BedInstance;
using Slic3r::Domain::BoundingBox2d;
using Slic3r::Domain::Vec2ds;

namespace Slic3r::Biz {

namespace {

bool remove_instance(Domain::ModelInstanceList& instances, Domain::ModelInstance* inst)
{
    auto it = std::find(instances.begin(), instances.end(), inst);
    if (it == instances.end())
        return false;
    instances.erase(it);
    return true;
}

bool remove_instance(Domain::ModelInstanceSet& instances, Domain::ModelInstance* inst)
{
    auto it = std::find(instances.begin(), instances.end(), inst);
    if (it == instances.end())
        return false;
    instances.erase(it);
    return true;
}
} // anonymous namespace

void remove_instance_from_bed(
    Domain::Project& project,
    Domain::ModelInstance* model_instance,
    BedTrackingChanges& changes
)
{
    ZoneScoped;

    bool found_instance = remove_instance(project.unplaced_model_instances(), model_instance);
    changes.unplaced_instances_updated |= found_instance;

    for (auto& cc : project.config_containers()) {
        for (auto& bi : cc->bed_instances()) {
            // Remove only first occurence of model_instance
            if (!found_instance && remove_instance(bi->model_instances, model_instance)) {
                found_instance = true;
                changes.updated_beds.insert(Domain::BedRef{cc->id().id, bi->id().id});
            }

            // Instance may be colliding with mulitple beds
            // Go through everything and remove every instance of it
            if (remove_instance(bi->colliding_instances, model_instance)) {
                --changes.colliding_instances_updated_count;
            }
        }
    }
}

BedTracking::BedCacheEntry& BedTracking::get_or_create_bed_cache(const Bed& bed)
{
    std::map<size_t, BedCacheEntry>::iterator it = m_bed_cache.find(bed.id().id);
    if (it == m_bed_cache.end()) {
        it = m_bed_cache
                 .try_emplace(
                     bed.id().id,
                     BedCacheEntry{
                         Algorithms::Bed::bed_contour_as_aabb_mesh(bed),
                         Algorithms::Polygon::scaled(bed.contour())
                     }
                 )
                 .first;
    }

    return it->second;
}

std::tuple<Domain::ConfigContainer*, Domain::BedInstance*, Algorithms::Bed::BedContainmentState>
BedTracking::find_bed_instance_for_bounds(
    Domain::Project& project,
    const Algorithms::Bed::ObjectCollisionData& obj_collision_data
)
{
    ZoneScoped;

    for (const auto& cc : project.config_containers()) {
        const BedCacheEntry& bed_cache = get_or_create_bed_cache(cc->bed());
        for (const auto& bi : cc->bed_instances()) {
            BedInstanceCollisionData bi_collision_data(
                *bi,
                &bed_cache.aabb_mesh,
                &bed_cache.scaled_contour
            );
            BedContainmentState state =
                Algorithms::Bed::contains_3d(bi_collision_data, obj_collision_data);
            if (state == BedContainmentState::Inside || state == BedContainmentState::Colliding) {
                return std::make_tuple(cc.get(), bi.get(), state);
            }
        }
    }

    return std::make_tuple(nullptr, nullptr, BedContainmentState::Outside);
}

BedContainmentState BedTracking::check_containment_2d(
    const Bed& bed,
    const BedInstance& bed_instance,
    const BoundingBox2d& bounding_box,
    const Vec2ds& convex_hull
)
{
    const BedCacheEntry& bed_cache = this->get_or_create_bed_cache(bed);
    const BedInstanceCollisionData bed_instance_collision_data(
        bed_instance,
        &bed_cache.aabb_mesh,
        &bed_cache.scaled_contour
    );
    return Algorithms::Bed::contains_2d(bed_instance_collision_data, bounding_box, convex_hull);
}

BedContainmentState BedTracking::check_instance_containment_2d(
    const Domain::Project& project,
    const Domain::ModelInstance& instance,
    const Bed& bed,
    const BedInstance& bed_instance
)
{
    const Algorithms::Bed::ObjectCollisionData& collision =
        this->get_instance_collision_data(project, instance);
    const BoundingBox2d bbox_2d{
        Algorithms::Point::to_2d(collision.bounding_box.min),
        Algorithms::Point::to_2d(collision.bounding_box.max),
        collision.bounding_box.defined
    };
    return this->check_containment_2d(bed, bed_instance, bbox_2d, collision.convex_hull_2d);
}

const Algorithms::Bed::ObjectCollisionData& BedTracking::get_instance_collision_data(const Domain::Project& project,
    const Domain::ModelInstance& inst)
{
    ZoneScoped;

    // This function calculates an instance's 3D bounding box and 2D convex hull, using a cache to boost performance.
    // The cache is invalidated if the instance's transform, its object's internal volume transforms,
    // or any volume's ID or type are modified.
    // It reuses cached geometry for instances sharing the same rotation to further optimize.
    // The cache also periodically removes entries for instances that no longer exist.

    CacheObjectEntry& obj_cache = m_cache[inst.get_object()->id().id];
    
    bool obj_cache_valid = true;
    if (obj_cache.vol_data.size() != inst.get_object()->volumes.size())
        obj_cache_valid = false;
    if (obj_cache_valid) {
        for (size_t i=0; i<obj_cache.vol_data.size(); ++i) {
            if (obj_cache.vol_data[i].id != inst.get_object()->volumes[i]->id().id
             || obj_cache.vol_data[i].type != inst.get_object()->volumes[i]->type()) {
                obj_cache_valid = false;
                break;
            }
            if (obj_cache.vol_data[i].type == Domain::ModelVolumeType::MODEL_PART
             && ! obj_cache.vol_data[i].trafo.get_matrix().isApprox(inst.get_object()->volumes[i]->get_transformation().get_matrix())) {
                obj_cache_valid = false;
                break;
            }
        }
    }
    if (! obj_cache_valid) {
        obj_cache.instances.clear();
        obj_cache.vol_data.clear();
        for (const Domain::ModelVolume* mv : inst.get_object()->volumes)
            obj_cache.vol_data.emplace_back(mv->get_transformation(), mv->id().id, mv->type());
    }

    CacheInstanceEntry& cache_entry = obj_cache.instances[inst.id().id];
    const Domain::Transformation& trafo = inst.get_transformation();

    // Try to use the cache
    bool cache_used = false;
    if (cache_entry.collision.bounding_box.defined) {
        if (cache_entry.inst_trafo.get_rotation().isApprox(trafo.get_rotation()) &&
            cache_entry.inst_trafo.get_scaling_factor().isApprox(trafo.get_scaling_factor()) &&
            cache_entry.inst_trafo.get_mirror().isApprox(trafo.get_mirror())) {
            // Rotation, scale and mirror are the same as before. We can just translate the collision data itself.
            cache_entry.collision.translate(trafo.get_offset() - cache_entry.inst_trafo.get_offset());
            cache_entry.inst_trafo = trafo;
            cache_used = true;
        }
    }

    if (! cache_used) {
        // We must calculate the new collision data in this case.
        const Domain::ModelObject& obj = *inst.get_object();
        auto bb_3d = Algorithms::ModelObject::instance_bounding_box(obj, inst, Domain::SINKING_Z_THRESHOLD);
        auto ch_2d = Algorithms::Point::unscaled(Algorithms::ModelObject::convex_hull_2d(obj, trafo.get_matrix()).points);
        cache_entry = {
            trafo,
            {
                std::move(bb_3d),
                std::move(ch_2d)
            }
        };
    }

    // Clear the cache every now and then.
    if (std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - m_last_cache_clear_time).count() > 20) {
        m_last_cache_clear_time = std::chrono::steady_clock::now();
        // Get list of all currently existing instances.
        std::vector<size_t> existing_instances;
        for (const Domain::ModelObject* mo : project.model().objects) {
            for (const Domain::ModelInstance* mi : mo->instances)
                existing_instances.emplace_back(mi->id().id);
        }
        std::ranges::sort(existing_instances);

        // Erase all records for instances which no longer exist.
        for (auto& [obj_id, obj_cache] : m_cache) {
            std::erase_if(obj_cache.instances, [&existing_instances](const auto& item) {
                auto it = std::lower_bound(existing_instances.begin(), existing_instances.end(), item.first);
                return it == existing_instances.end() || *it != item.first;
            });
        }
        // Remove all object entries which have empty instances vector.
        std::erase_if(m_cache, [](const std::pair<size_t, CacheObjectEntry>& item) {
            return item.second.instances.empty();
        });
    }

    return cache_entry.collision;
}

void BedTracking::update_instance_bed_placement(
    Domain::Project& project,
    Domain::ModelInstance& inst,
    BedTrackingChanges& changes
)
{
    ZoneScoped;

    const auto& collision_data = get_instance_collision_data(project, inst);

    auto [cc, bi, state] = find_bed_instance_for_bounds(project, collision_data);
    if (bi != nullptr) {
        if (state == Algorithms::Bed::BedContainmentState::Inside) {
            bi->model_instances.push_back(&inst);
            const Domain::BedRef bed_ref{Domain::BedRef{cc->id().id, bi->id().id}};
            changes.updated_beds.insert(bed_ref);
            inst.set_last_bed(bed_ref);
            changes.updated_instances.insert(
                Domain::ElementRef(inst.get_object()->id().id, inst.id().id)
                );
            return;
        } else {
            if (!bi->colliding_instances.contains(&inst)) {
                bi->colliding_instances.insert(&inst);
                ++changes.colliding_instances_updated_count;
            }
        }
    }
    project.unplaced_model_instances().push_back(&inst);
    changes.unplaced_instances_updated = true;
}

BedTrackingChanges BedTracking::update_instances_bed_placement(Domain::Project& project)
{
    BedTrackingChanges changes;

    // Clear old tracking
    project.unplaced_model_instances().clear();
    for (auto& cc : project.config_containers())
        for (auto& bi : cc->bed_instances()) {
            auto& insts = bi->model_instances;
            if (!insts.empty()) {
                insts.clear();
                changes.updated_beds.insert(Domain::BedRef{cc->id().id, bi->id().id});
            }
            auto& colliding_insts = bi->colliding_instances;
            if (!colliding_insts.empty()) {
                colliding_insts.clear();
                changes.updated_beds.insert(Domain::BedRef{cc->id().id, bi->id().id});
            }
        }

    // Build new tracking
    for (auto* o : project.model().objects)
        for (auto* inst : o->instances)
            update_instance_bed_placement(project, *inst, changes);

    return changes;
}

BedTrackingChanges BedTracking::update_instances_bed_placement(Domain::Project& project, const Domain::ElementRefs& instances,
    bool remove_original_links)
{
    // Remove cached AABBMesh entries for beds that no longer exist in the project.
    std::vector<size_t> bed_ids;
    bed_ids.reserve(project.config_containers().size());
    for (const auto& cc : project.config_containers()) {
        bed_ids.emplace_back(cc->bed().id().id);
    }
    std::sort(bed_ids.begin(), bed_ids.end());
    std::erase_if(
        m_bed_cache,
        [&](const auto& item)
        { return !std::binary_search(bed_ids.begin(), bed_ids.end(), item.first); }
    );

    BedTrackingChanges changes;
    for (const auto& e : instances) {
        if (e.is_wipe_tower()) {
            const std::size_t bed_instance_id{e.wipe_tower_id.bed_instance_id};
            const Domain::ConfigContainer* config_container{
                project.find_config_container_by_bed_instance_id(bed_instance_id)
            };
            if (config_container == nullptr) {
                continue;
            }
            changes.updated_beds.insert(Domain::BedRef{config_container->id().id, bed_instance_id});
            continue;
        }
        auto* inst = DEBUG_ASSERT_VAL(project.find_instance_by_id(e.object_id, e.instance_id));
        if (remove_original_links)
            remove_instance_from_bed(project, inst, changes);
        if (inst == nullptr)
            continue;
        update_instance_bed_placement(project, *inst, changes);
    }
    return changes;
}

BedTrackingChanges
BedTracking::update_instances_bed_placement(Domain::Project& project, const Domain::ModelInstanceList& instances, bool remove_original_links)
{
    ZoneScoped;

    BedTrackingChanges changes;
    for (auto* inst : instances) {
        if (remove_original_links)
            remove_instance_from_bed(project, inst, changes);
        update_instance_bed_placement(project, *inst, changes);
    }
    return changes;
}

} // namespace Slic3r::Biz
