#include "Slic3r/Biz/Scene/SelectionExtents.hpp"
#include "Slic3r/Biz/Algorithms/BoundingBox.hpp"
#include "Slic3r/Biz/Scene/SceneInteractor.hpp"

namespace Slic3r::Biz::Scene {

// Intentionally different semantics than axis aligned bounding box.
struct Extents
{
    Domain::Vec3d minimum{std::numeric_limits<double>::max() * Domain::Vec3d::Ones()};
    Domain::Vec3d maximum{std::numeric_limits<double>::lowest() * Domain::Vec3d::Ones()};

    bool operator==(const Extents&) const = default;
};

static Extents get_volume_extents_in_basis(
    const Domain::ModelVolume& volume,
    Domain::Transform3d instance_trafo,
    const Domain::SquareMatrix3d& basis
)
{
    Extents result;

    for (const Domain::Vec3f& vertex : volume.get_convex_hull().its.vertices) {
        const Domain::Transform3d volume_trafo{volume.get_matrix()};
        const Domain::Vec3d world_vertex{
            instance_trafo * volume_trafo * Domain::Vec3d{vertex.cast<double>()}
        };

        for (int i{}; i < 3; ++i) {
            const Domain::Vec3d axis{basis.col(i)};
            const double dot_product{axis.dot(world_vertex)};
            result.minimum(i) = std::min(dot_product, result.minimum(i));
            result.maximum(i) = std::max(dot_product, result.maximum(i));
        }
    }
    return result;
}

static void update_extents(
    const Domain::ModelInstance* instance,
    const Domain::ModelVolume* volume,
    const Domain::SquareMatrix3d& basis,
    Extents& extents
)
{
    const Extents volume_extents{
        get_volume_extents_in_basis(*ASSERT_VAL(volume), instance->get_matrix(), basis)
    };
    extents.minimum = extents.minimum.cwiseMin(volume_extents.minimum);
    extents.maximum = extents.maximum.cwiseMax(volume_extents.maximum);
}

static std::optional<Biz::Scene::OrientedBoundingBox> get_selection_bounding_box_in_basis(
    Domain::SelectionId project_id,
    const Biz::Scene::ObjectSelection& selection,
    const Domain::Workbench& workbench,
    const Domain::SquareMatrix3d& basis
)
{
    ASSERT(
        (basis.transpose() * basis).isApprox(Eigen::Matrix3d::Identity()),
        "Basis must be orthonormal!"
    );

    using Biz::Scene::SelectionState;

    const Domain::Project& project{workbench.project(project_id)};

    Extents extents;
    for (const Domain::ElementRef& element : selection.elements) {
        if (!element.has_instance()) {
            continue;
        }
        const Domain::ModelInstance* instance{
            project.find_instance_by_id(element.object_id, element.instance_id)
        };
        if (instance == nullptr) {
            continue;
        }

        if (element.has_volume()) {
            const Domain::ModelVolume* volume{
                project.find_volume_by_id(element.object_id, element.volume_id)
            };
            if (volume == nullptr) {
                continue;
            }
            update_extents(instance, volume, basis, extents);
        } else {
            for (const Domain::ModelVolume* volume : instance->get_object()->volumes) {
                update_extents(instance, volume, basis, extents);
            }
        }
    }
    if (extents == Extents{}) {
        return std::nullopt;
    }

    const Domain::Vec3d extents_dimensions{extents.maximum - extents.minimum};

    return Biz::Scene::OrientedBoundingBox{
        basis * ((extents.maximum + extents.minimum) / 2.0),
        extents_dimensions,
        basis
    };
}

static std::optional<Biz::Scene::OrientedBoundingBox> get_global_obb(
    Domain::SelectionId project_id,
    const Biz::Scene::ObjectSelection& selection,
    const Domain::Workbench& workbench
)
{
    if (selection.elements.empty()) {
        return std::nullopt;
    }
    return get_selection_bounding_box_in_basis(
        project_id,
        selection,
        workbench,
        Domain::SquareMatrix3d::Identity()
    );
}

static std::optional<Biz::Scene::OrientedBoundingBox> get_instance_obb(
    Domain::SelectionId project_id,
    const Biz::Scene::ObjectSelection& selection,
    const Domain::Workbench& workbench
)
{
    if (selection.elements.empty()) {
        return std::nullopt;
    }
    const Domain::ElementRef& element{selection.elements.front()};
    if (!element.has_instance()) {
        return std::nullopt;
    }

    const Domain::ModelInstance* instance{
        workbench.project(project_id).find_instance_by_id(element.object_id, element.instance_id)
    };
    if (!instance) {
        return std::nullopt;
    }

    return get_selection_bounding_box_in_basis(
        project_id,
        selection,
        workbench,
        instance->get_matrix().rotation()
    );
}

static std::optional<Biz::Scene::OrientedBoundingBox> get_volume_obb(
    Domain::SelectionId project_id,
    const Biz::Scene::ObjectSelection& selection,
    const Domain::Workbench& workbench
)
{
    if (selection.elements.empty()) {
        return std::nullopt;
    }
    const Domain::ElementRef& element{selection.elements.front()};

    const Domain::ModelObject* object{
        workbench.project(project_id).find_object_by_id(element.object_id)
    };

    if (!element.has_instance()) {
        return std::nullopt;
    }
    if (!element.has_volume() && object->volumes.size() != 1) {
        return std::nullopt;
    }

    const Domain::ModelInstance* instance{
        workbench.project(project_id).find_instance_by_id(element.object_id, element.instance_id)
    };
    if (instance == nullptr) {
        return std::nullopt;
    }

    const Domain::ModelVolume* volume{!element.has_volume() ? object->volumes.front() : nullptr};

    if (volume == nullptr) {
        if (!element.has_volume()) {
            return std::nullopt;
        }
        volume =
            workbench.project(project_id).find_volume_by_id(element.object_id, element.volume_id);
    };

    if (volume == nullptr) {
        return std::nullopt;
    }

    return get_selection_bounding_box_in_basis(
        project_id,
        selection,
        workbench,
        (instance->get_matrix() * volume->get_matrix()).rotation()
    );
}

static double
get_volume_min_z(const Domain::ModelVolume& volume, Domain::Transform3d instance_trafo)
{
    double min{std::numeric_limits<double>::max()};
    for (const Domain::Vec3f& vertex : volume.get_convex_hull().its.vertices) {
        const Domain::Transform3d volume_trafo{volume.get_matrix()};
        const Domain::Vec3d world_vertex{
            instance_trafo * volume_trafo * Domain::Vec3d{vertex.cast<double>()}
        };
        min = std::min(min, world_vertex.z());
    }
    return min;
}

static std::optional<double> get_selection_min_z(
    Domain::SelectionId project_id,
    const ObjectSelection& selection,
    const Domain::Workbench& workbench
)
{
    const Domain::Project& project{workbench.project(project_id)};

    std::optional<double> min_z;

    for (const Domain::ElementRef& element : selection.elements) {
        if (element.is_wipe_tower()) {
            if (!min_z) {
                min_z = 0.0;
            } else {
                min_z = std::min(*min_z, 0.0);
            }
            continue;
        }
        if (!element.has_instance()) {
            continue;
        }
        const Domain::ModelInstance* instance{
            project.find_instance_by_id(element.object_id, element.instance_id)
        };
        if (instance == nullptr) {
            continue;
        }

        if (element.has_volume()) {
            const Domain::ModelVolume* volume{
                project.find_volume_by_id(element.object_id, element.volume_id)
            };
            if (volume == nullptr) {
                continue;
            }
            if (!volume->is_model_part()) {
                continue;
            }
            const double volume_min_z{get_volume_min_z(*volume, instance->get_matrix())};
            if (!min_z) {
                min_z = volume_min_z;
            } else {
                min_z = std::min(*min_z, volume_min_z);
            }
        } else {
            for (const Domain::ModelVolume* volume : instance->get_object()->volumes) {
                if (!volume->is_model_part()) {
                    continue;
                }
                const double volume_min_z{get_volume_min_z(*volume, instance->get_matrix())};
                if (!min_z) {
                    min_z = volume_min_z;
                } else {
                    min_z = std::min(*min_z, volume_min_z);
                }
            }
        }
    }
    return min_z;
}

std::optional<SelectionExtents> get_selection_extents(
    Domain::SelectionId project_id,
    const ObjectSelection& selection,
    const SceneInteractor& scene_interactor,
    const Domain::Workbench& workbench
)
{
    namespace BB = Biz::Algorithms::BoundingBox;

    if (selection.empty()) {
        return std::nullopt;
    }

    const Domain::Project& project{workbench.project(project_id)};

    std::optional<OrientedBoundingBox> oriented_bounding_box{};
    switch (scene_interactor.object_selection_reference_frame()) {
    case SelectionReferenceFrame::Volume: {
        oriented_bounding_box = get_volume_obb(project_id, selection, workbench);
    } break;
    case SelectionReferenceFrame::Instance: {
        oriented_bounding_box = get_instance_obb(project_id, selection, workbench);
    } break;
    case SelectionReferenceFrame::Bed: {
        const std::optional<Biz::Scene::OrientedBoundingBox> global_obb{
            get_global_obb(project_id, selection, workbench)
        };

        Domain::BoundingBox3d bounding_box;
        if (global_obb) {
            ASSERT(global_obb->rotation.isApprox(Domain::SquareMatrix3d::Identity()));
            bounding_box = Domain::BoundingBox3d{
                global_obb->center - global_obb->dimensions / 2.0,
                global_obb->center + global_obb->dimensions / 2.0
            };
        };

        for (const Domain::ElementRef& element : selection.elements) {
            if (!element.is_wipe_tower()) {
                continue;
            }
            const Domain::BedInstance* bed_instance{
                project.find_bed_instance_by_id(element.wipe_tower_id.bed_instance_id)
            };
            if (!bed_instance) {
                continue;
            }
            const Biz::Slicing::WipeTowerGeometry* wipe_tower_geometry{
                scene_interactor.wipe_tower_geometry(element.wipe_tower_id.bed_instance_id)
            };
            if (!wipe_tower_geometry) {
                continue;
            }
            Domain::BoundingBox3d wipe_tower_bb{
                wipe_tower_geometry->get_bounding_box(bed_instance->wipe_tower)
            };

            wipe_tower_bb =
                BB::transformed(wipe_tower_bb, bed_instance->transformation.get_matrix());
            bounding_box = BB::merge(bounding_box, wipe_tower_bb);
        }
        if (!bounding_box.defined) {
            return std::nullopt;
        }
        oriented_bounding_box = Biz::Scene::OrientedBoundingBox{
            .center     = BB::center(bounding_box),
            .dimensions = BB::sizes(bounding_box),
            .rotation   = Domain::SquareMatrix3d::Identity()
        };
    } break;
    }

    if (!oriented_bounding_box) {
        return std::nullopt;
    }

    const std::optional<double> min_z{get_selection_min_z(project_id, selection, workbench)};
    return Biz::Scene::SelectionExtents{*oriented_bounding_box, min_z.value_or(0.0)};
}
} // namespace Slic3r::Biz::Scene
