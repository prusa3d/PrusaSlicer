#include "Slic3r/App/CLI/ProcessTransform.hpp"

#include "Slic3r/App/CLI/CLIRuntime.hpp"
#include "Slic3r/App/CLI/CLIUtils.hpp"
#include "Slic3r/App/Init.hpp"
#include "Slic3r/Biz/Algorithms/BoundingBox.hpp"
#include "Slic3r/Biz/Algorithms/Model.hpp"
#include "Slic3r/Biz/Algorithms/ModelObject.hpp"
#include "Slic3r/Biz/Algorithms/Point.hpp"
#include "Slic3r/Biz/Algorithms/Scaling.hpp"
#include "Slic3r/Biz/Arrange/Settings.hpp"
#include "Slic3r/Biz/Utils/CutUtils.hpp"
#include "Slic3r/Domain/ConfigPack.hpp"
#include "Slic3r/Domain/Model.hpp"
#include "Slic3r/Domain/ModelInstance.hpp"
#include "Slic3r/Domain/ModelObject.hpp"
#include "Slic3r/Domain/Project.hpp"
#include "Slic3r/Domain/Transformation.hpp"
#include "Slic3r/Domain/Types.hpp"
#include "Slic3r/Log.hpp"
#include "Slic3r/Math.hpp"

#include <algorithm>
#include <array>
#include <ranges>
#include <vector>

using namespace Slic3r;
using namespace Slic3r::Biz;

using Slic3r::App::InitParams;
using Slic3r::App::TransformParams;
using Slic3r::Biz::Scene::ObjectSelection;
using Slic3r::Biz::Scene::SceneInteractor;
using Slic3r::Biz::Scene::SelectionMode;
using Slic3r::Domain::BoundingBox3d;
using Slic3r::Domain::ConfigContainer;
using Slic3r::Domain::ConfigPack;
using Slic3r::Domain::ElementRef;
using Slic3r::Domain::ElementRefs;
using Slic3r::Domain::Model;
using Slic3r::Domain::ModelInstance;
using Slic3r::Domain::ModelObject;
using Slic3r::Domain::ModelObjectPtrs;
using Slic3r::Domain::Project;
using Slic3r::Domain::SelectionId;
using Slic3r::Domain::SquareMatrix4d;
using Slic3r::Domain::Transform3d;
using Slic3r::Domain::Vec2d;
using Slic3r::Domain::Vec3d;

namespace Slic3r::App::CLI {

static ObjectSelection make_all_instances_selection(const Project& project)
{
    ObjectSelection object_selection{SelectionMode::Instance, {}};
    for (const ModelObject* model_object : project.model().objects) {
        for (const ModelInstance* model_instance : model_object->instances) {
            object_selection.elements.push_back({model_object->id().id, model_instance->id().id});
        }
    }

    return object_selection;
}

static ObjectSelection make_object_selection(const ModelObject& model_object)
{
    ObjectSelection object_selection{SelectionMode::Instance, {}};
    for (const ModelInstance* model_instance : model_object.instances) {
        object_selection.elements.push_back({model_object.id().id, model_instance->id().id});
    }

    return object_selection;
}

/**
 * @brief Applies the given world transformation to the object's volumes.
 */
static void transform_object_volumes(
    SceneInteractor& scene_interactor,
    const ModelObject& model_object,
    const SquareMatrix4d& world_transform
)
{
    if (model_object.instances.empty()) {
        return;
    }

    const ModelInstance& first_instance = *model_object.instances.front();
    scene_interactor.set_object_selection(
        ObjectSelection{
            SelectionMode::Instance,
            {ElementRef{model_object.id().id, first_instance.id().id}}
        }
    );

    Scene::TransformMemento transform_memento;
    transform_memento.forced_volume_mode = true;
    scene_interactor.transform_selection(world_transform, transform_memento, false);
    scene_interactor.finalize_transform_selection(transform_memento, false);
}

/**
 * @brief Scales the volumes of the object by the given factor.
 */
static void scale_object_volumes(
    SceneInteractor& scene_interactor,
    const ModelObject& model_object,
    const double scaling_factor
)
{
    if (model_object.instances.empty()) {
        return;
    }

    const Vec3d scaling_pivot = model_object.instances.front()->get_offset();
    Transform3d scaling_transform{Transform3d::Identity()};
    scaling_transform.translate(scaling_pivot);
    scaling_transform.scale(scaling_factor);
    scaling_transform.translate(-scaling_pivot);

    transform_object_volumes(scene_interactor, model_object, scaling_transform.matrix());
}

/**
 * @brief Translates all instances of the selected project by the given vector.
 */
static void translate_selected_project_instances(
    SceneInteractor& scene_interactor,
    const Project& project,
    const Vec3d& translation_vector
)
{
    if (project.model().objects.empty()) {
        return;
    }

    const ObjectSelection all_instances_selection = make_all_instances_selection(project);
    scene_interactor.set_object_selection(all_instances_selection);

    Transform3d translation_transform{Transform3d::Identity()};
    translation_transform.translate(translation_vector);
    scene_interactor.transform_selection(translation_transform.matrix(), false);
}

void center_selected_project_around_point(CLIRuntime& runtime, const Vec2d& center_point)
{
    ProjectInteractor& project_interactor = runtime.project_interactor();
    const Project& project                = project_interactor.selected_project();
    if (project.model().objects.empty()) {
        return;
    }

    BoundingBox3d instances_bounding_box;
    for (const ModelObject* model_object : project.model().objects) {
        for (size_t instance_index = 0; instance_index < model_object->instances.size();
             ++instance_index)
        {
            instances_bounding_box = Algorithms::BoundingBox::merge(
                instances_bounding_box,
                Algorithms::ModelObject::instance_bounding_box(*model_object, instance_index, false)
            );
        }
    }

    const Vec2d center_shift = center_point
        - Algorithms::Point::to_2d(Algorithms::BoundingBox::center(instances_bounding_box));
    if (std::abs(center_shift.x()) < Domain::EPSILON
        && std::abs(center_shift.y()) < Domain::EPSILON)
    {
        return; // No significant shift, don't do anything.
    }

    translate_selected_project_instances(
        project_interactor.scene_interactor(),
        project,
        Vec3d{center_shift.x(), center_shift.y(), 0.}
    );
}

void arrange_and_wait(CLIRuntime& runtime, const SelectionId project_id)
{
    ProjectInteractor& project_interactor{runtime.project_interactor()};
    const ConfigContainer& config_container{project_interactor.selected_config_container()};
    const ConfigPack config_pack{config_container.build_print_config()};

    const double scaled_offset{
        static_cast<double>(Algorithms::Scaling::scaled(min_object_distance(config_pack))) / 2.};

    Domain::ConstModelInstanceList instances{};
    std::vector<Biz::BedToArrange> beds;
    for (const auto& bed_instance : config_container.bed_instances()) {
        instances.insert(instances.end(),
                         bed_instance->model_instances.begin(),
                         bed_instance->model_instances.end());
        beds.push_back({Domain::BedRef{config_container.id().id, bed_instance->id().id},
                        bed_instance->index()});
    }

    const Domain::ModelInstanceList& unplaced_instances{
        project_interactor.scene_interactor().unplaced_model_instances(
            project_interactor.selected_project_id())};
    instances.insert(instances.end(), unplaced_instances.begin(), unplaced_instances.end());

    bool arrange_finished{false};
    ArrangeInteractor& arrange_interactor{project_interactor.arrange_interactor()};
    arrange_interactor.arrange(project_interactor.selected_project_id(),
                               beds,
                               config_container.id().id,
                               instances,
                               Arrange::Settings{.scaled_offset = scaled_offset},
                               [&arrange_finished]() { arrange_finished = true; });

    runtime.wait_until([&arrange_finished]() { return arrange_finished; });
}

/**
 * @brief Merges all projects into the first one and removes the merged-out projects.
 * Shrinks @p project_ids to the single remaining project.
 */
static void apply_merge(
    CLIRuntime& runtime,
    const bool rearrange_after_merge,
    std::vector<SelectionId>& project_ids
)
{
    ProjectInteractor& project_interactor = runtime.project_interactor();
    SceneInteractor& scene_interactor     = project_interactor.scene_interactor();

    const SelectionId merged_project_id = project_ids.front();
    for (const SelectionId source_project_id :
         std::ranges::subrange(project_ids.begin() + 1, project_ids.end()))
    {
        project_interactor.select_project(merged_project_id);

        const Project& source_project = project_interactor.project(source_project_id);
        const ElementRefs source_instance_refs =
            make_all_instances_selection(source_project).elements;

        scene_interactor.clone_objects_from_project(source_project_id, source_instance_refs);
        project_interactor.remove_project(source_project_id);
    }

    // Rearrange instances unless --dont-arrange is supplied.
    if (rearrange_after_merge) {
        project_interactor.select_project(merged_project_id);
        arrange_and_wait(runtime, merged_project_id);
    }

    project_ids.resize(1);
}

/**
 * @brief Sets the instance count of every object to @p duplicate_count and rearranges.
 */
static void apply_duplicate(
    CLIRuntime& runtime,
    const uint32_t duplicate_count,
    const std::vector<SelectionId>& project_ids
)
{
    ProjectInteractor& project_interactor = runtime.project_interactor();
    SceneInteractor& scene_interactor     = project_interactor.scene_interactor();

    for (const SelectionId project_id : project_ids) {
        project_interactor.select_project(project_id);

        const Project& project                        = project_interactor.project(project_id);
        const ObjectSelection all_instances_selection = make_all_instances_selection(project);
        scene_interactor.set_object_selection(all_instances_selection);
        scene_interactor.set_selected_objects_instance_count(static_cast<int>(duplicate_count));

        arrange_and_wait(runtime, project_id);
    }
}

/**
 * @brief Duplicates the single object of every project into an X*Y grid of instances.
 */
static bool apply_duplicate_grid(
    CLIRuntime& runtime,
    const std::array<uint32_t, 2>& grid_dimensions,
    const std::vector<SelectionId>& project_ids
)
{
    ProjectInteractor& project_interactor = runtime.project_interactor();
    SceneInteractor& scene_interactor     = project_interactor.scene_interactor();

    const size_t grid_size_x   = std::max<uint32_t>(grid_dimensions[0], 1);
    const size_t grid_size_y   = std::max<uint32_t>(grid_dimensions[1], 1);
    const double grid_distance = 6.; // TODO: duplicate_distance was removed in the new configs.

    for (const SelectionId project_id : project_ids) {
        project_interactor.select_project(project_id);

        const Project& project = project_interactor.project(project_id);
        if (project.model().objects.size() > 1) {
            SPDLOG_ERROR("Grid duplication is not supported with multiple objects");
            return false;
        }

        if (project.model().objects.empty()) {
            continue;
        }

        const ModelObject& model_object        = *project.model().objects.front();
        const ObjectSelection object_selection = make_object_selection(model_object);
        const Vec3d grid_cell_size             = Algorithms::BoundingBox::sizes(
                                         Algorithms::ModelObject::bounding_box_exact(model_object)
                                     )
            + grid_distance * Vec3d::Ones();

        scene_interactor.set_object_selection(object_selection);
        scene_interactor.set_selected_objects_instance_count(
            static_cast<int>(grid_size_x * grid_size_y)
        );

        SceneInteractor::ElementTransforms grid_transforms;
        size_t instance_index = 0;
        for (const ModelInstance* model_instance : model_object.instances) {
            const size_t grid_cell_x = instance_index / grid_size_y;
            const size_t grid_cell_y = instance_index % grid_size_y;

            Transform3d grid_cell_transform{Transform3d::Identity()};
            grid_cell_transform.translate(
                Vec3d{
                    grid_cell_size.x() * static_cast<double>(grid_cell_x),
                    grid_cell_size.y() * static_cast<double>(grid_cell_y),
                    0.
                }
            );

            grid_transforms.insert(
                {ElementRef{model_object.id().id, model_instance->id().id},
                 grid_cell_transform.matrix()}
            );

            ++instance_index;
        }

        scene_interactor.set_element_transforms(grid_transforms);
    }

    return true;
}

/**
 * @brief Centers the instances of every project around the given XY point and aligns
 * their common Z span to the bed.
 */
static void apply_center(
    CLIRuntime& runtime,
    const Vec2d& center_point,
    const std::vector<SelectionId>& project_ids
)
{
    ProjectInteractor& project_interactor = runtime.project_interactor();
    SceneInteractor& scene_interactor     = project_interactor.scene_interactor();

    for (const SelectionId project_id : project_ids) {
        project_interactor.select_project(project_id);

        center_selected_project_around_point(runtime, center_point);

        // We are interested in the Z span only, therefore, it is enough to measure
        // the bounding box of the 1st instance only.
        const Project& project = project_interactor.project(project_id);
        BoundingBox3d z_span_bounding_box;
        for (const ModelObject* model_object : project.model().objects) {
            z_span_bounding_box = Algorithms::BoundingBox::merge(
                z_span_bounding_box,
                Algorithms::ModelObject::instance_bounding_box(*model_object, 0, false)
            );
        }

        if (z_span_bounding_box.min.z() != 0.) {
            translate_selected_project_instances(
                scene_interactor,
                project,
                Vec3d{0., 0., -z_span_bounding_box.min.z()}
            );
        }
    }
}

/**
 * @brief Moves the instances of every project so the minimum of their bounding box
 * lies at the given XY point and at z = 0.
 */
static void apply_align_xy(
    CLIRuntime& runtime,
    const Vec2d& align_point,
    const std::vector<SelectionId>& project_ids
)
{
    ProjectInteractor& project_interactor = runtime.project_interactor();
    SceneInteractor& scene_interactor     = project_interactor.scene_interactor();

    for (const SelectionId project_id : project_ids) {
        project_interactor.select_project(project_id);

        const Project& project = project_interactor.project(project_id);
        const BoundingBox3d model_bounding_box =
            Algorithms::Model::bounding_box_exact(project.model());

        translate_selected_project_instances(
            scene_interactor,
            project,
            Vec3d{
                -(model_bounding_box.min.x() - align_point.x()),
                -(model_bounding_box.min.y() - align_point.y()),
                -model_bounding_box.min.z()
            }
        );
    }
}

/**
 * @brief Rotates the volumes of every object by the given angles (in degrees) around
 * the Z, X and Y axes in this order.
 */
static void apply_rotation(
    CLIRuntime& runtime,
    const Vec3d& rotation_angles,
    const std::vector<SelectionId>& project_ids
)
{
    ProjectInteractor& project_interactor = runtime.project_interactor();
    SceneInteractor& scene_interactor     = project_interactor.scene_interactor();

    if (rotation_angles.isZero()) {
        return;
    }

    // Eigen composes rotations on the right, so this applies Z, then X, then Y.
    Transform3d combined_rotation{Transform3d::Identity()};
    combined_rotation.rotate(Eigen::AngleAxisd(deg2rad(rotation_angles.y()), Vec3d::UnitY()));
    combined_rotation.rotate(Eigen::AngleAxisd(deg2rad(rotation_angles.x()), Vec3d::UnitX()));
    combined_rotation.rotate(Eigen::AngleAxisd(deg2rad(rotation_angles.z()), Vec3d::UnitZ()));

    for (const SelectionId project_id : project_ids) {
        project_interactor.select_project(project_id);

        const Project& project = project_interactor.project(project_id);
        for (const ModelObject* model_object : project.model().objects) {
            if (model_object->instances.empty()) {
                continue;
            }

            const Vec3d rotation_pivot = model_object->instances.front()->get_offset();

            Transform3d rotation_transform{combined_rotation};
            rotation_transform.pretranslate(rotation_pivot);
            rotation_transform.translate(-rotation_pivot);

            transform_object_volumes(scene_interactor, *model_object, rotation_transform.matrix());
        }
    }
}

/**
 * @brief Scales the volumes of every object by the given factor.
 */
static void apply_scale(
    CLIRuntime& runtime,
    const double scaling_factor,
    const std::vector<SelectionId>& project_ids
)
{
    ProjectInteractor& project_interactor = runtime.project_interactor();
    SceneInteractor& scene_interactor     = project_interactor.scene_interactor();

    for (const SelectionId project_id : project_ids) {
        project_interactor.select_project(project_id);

        const Project& project = project_interactor.project(project_id);
        for (const ModelObject* model_object : project.model().objects) {
            scale_object_volumes(scene_interactor, *model_object, scaling_factor);
        }
    }
}

/**
 * @brief Uniformly scales every object so it fits into the given size.
 */
static void apply_scale_to_fit(
    CLIRuntime& runtime,
    const Vec3d& fit_size,
    const std::vector<SelectionId>& project_ids
)
{
    ProjectInteractor& project_interactor = runtime.project_interactor();
    SceneInteractor& scene_interactor     = project_interactor.scene_interactor();

    for (const SelectionId project_id : project_ids) {
        project_interactor.select_project(project_id);

        const Project& project = project_interactor.project(project_id);
        for (const ModelObject* model_object : project.model().objects) {
            if (model_object->instances.empty()) {
                continue;
            }

            const Vec3d original_size = Algorithms::BoundingBox::sizes(
                Algorithms::ModelObject::bounding_box_exact(*model_object)
            );
            const double scaling_factor = std::min(
                fit_size.x() / original_size.x(),
                std::min(fit_size.y() / original_size.y(), fit_size.z() / original_size.z())
            );

            scale_object_volumes(scene_interactor, *model_object, scaling_factor);
        }
    }
}

/**
 * @brief Cuts every object with a horizontal plane at the given height and replaces
 * it in the scene with the resulting upper and lower parts.
 */
static void
apply_cut(CLIRuntime& runtime, const double cut_height, const std::vector<SelectionId>& project_ids)
{
    ProjectInteractor& project_interactor = runtime.project_interactor();
    SceneInteractor& scene_interactor     = project_interactor.scene_interactor();

    const Vec3d cut_plane_center = cut_height * Vec3d::UnitZ();
    for (const SelectionId project_id : project_ids) {
        project_interactor.select_project(project_id);

        const Project& project = project_interactor.project(project_id);

        // Align the whole project to z = 0 first.
        const BoundingBox3d model_bounding_box =
            Algorithms::Model::bounding_box_exact(project.model());
        if (model_bounding_box.min.z() != 0.) {
            translate_selected_project_instances(
                scene_interactor,
                project,
                Vec3d{0., 0., -model_bounding_box.min.z()}
            );
        }

        const ModelObjectPtrs objects_to_cut = project.model().objects;
        for (ModelObject* model_object : objects_to_cut) {
            if (model_object->instances.empty()) {
                continue;
            }

            const Vec3d cut_center_offset =
                cut_plane_center - model_object->instances.front()->get_offset();
            Cut object_cut{
                model_object,
                0,
                Domain::translation_transform(cut_center_offset),
                ModelObjectCutAttributes{
                    .keep_upper         = true,
                    .keep_lower         = true,
                    .keep_as_parts      = false,
                    .place_on_cut_upper = true
                }
            };

            const ModelObjectPtrs& cut_result_objects = object_cut.perform_with_plane();
            scene_interactor.delete_object(model_object);
            scene_interactor.add_new_objects(cut_result_objects);
        }
    }
}

/**
 * @brief Splits every multi-part object of every project into separate objects.
 */
static void apply_split(CLIRuntime& runtime, const std::vector<SelectionId>& project_ids)
{
    ProjectInteractor& project_interactor = runtime.project_interactor();
    SceneInteractor& scene_interactor     = project_interactor.scene_interactor();

    for (const SelectionId project_id : project_ids) {
        project_interactor.select_project(project_id);

        const Project& project                 = project_interactor.project(project_id);
        const ModelObjectPtrs objects_to_split = project.model().objects;
        for (const ModelObject* model_object : objects_to_split) {
            const ObjectSelection object_selection = make_object_selection(*model_object);
            scene_interactor.set_object_selection(object_selection);

            if (scene_interactor.can_split_selection_to_objects()) {
                scene_interactor.split_selection_to_objects();
            }
        }
    }
}

/**
 * @brief Drops every object of every project onto the bed.
 */
static void apply_ensure_on_bed(CLIRuntime& runtime, const std::vector<SelectionId>& project_ids)
{
    ProjectInteractor& project_interactor = runtime.project_interactor();
    SceneInteractor& scene_interactor     = project_interactor.scene_interactor();

    for (const SelectionId project_id : project_ids) {
        project_interactor.select_project(project_id);

        const Project& project = project_interactor.project(project_id);
        for (const ModelObject* model_object : project.model().objects) {
            const double object_min_z = model_object->min_z();
            if (object_min_z == 0.) {
                continue;
            }

            const ObjectSelection object_selection = make_object_selection(*model_object);
            scene_interactor.set_object_selection(object_selection);

            Transform3d drop_transform{Transform3d::Identity()};
            drop_transform.translate(Vec3d{0., 0., -object_min_z});
            scene_interactor.transform_selection(drop_transform.matrix(), false);
        }
    }
}

bool process_transform(
    CLIRuntime& runtime,
    const InitParams& init_params,
    std::vector<SelectionId>& project_ids
)
{
    const TransformParams& transform = init_params.transform;

    if (transform.merge.has_value() && transform.merge.value() && !project_ids.empty()) {
        apply_merge(runtime, !transform.dont_arrange.value_or(false), project_ids);
    }

    if (transform.duplicate.has_value()) {
        apply_duplicate(runtime, transform.duplicate.value(), project_ids);
    }

    if (transform.duplicate_grid.has_value()) {
        if (!apply_duplicate_grid(runtime, transform.duplicate_grid.value(), project_ids)) {
            return false;
        }
    }

    if (transform.center.has_value()) {
        apply_center(runtime, transform.center.value(), project_ids);
    }

    if (transform.align_xy.has_value()) {
        apply_align_xy(runtime, transform.align_xy.value(), project_ids);
    }

    if (transform.rotation.has_value()) {
        apply_rotation(runtime, transform.rotation.value(), project_ids);
    }

    if (transform.scale.has_value()) {
        apply_scale(runtime, transform.scale->get_abs_value(1.), project_ids);
    }

    if (transform.scale_to_fit.has_value()) {
        apply_scale_to_fit(runtime, transform.scale_to_fit.value(), project_ids);
    }

    if (transform.cut_z.has_value()) {
        apply_cut(runtime, transform.cut_z.value(), project_ids);
    }

    if (transform.split.has_value() && transform.split.value()) {
        apply_split(runtime, project_ids);
    }

    // All transforms have been dealt with. Now ensure that the objects are on bed.
    // (Unless the user said otherwise.)
    if (!transform.ensure_on_bed.has_value() || transform.ensure_on_bed.value()) {
        apply_ensure_on_bed(runtime, project_ids);
    }

    return true;
}

} // namespace Slic3r::App::CLI
