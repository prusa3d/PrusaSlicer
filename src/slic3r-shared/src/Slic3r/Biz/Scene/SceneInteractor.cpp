#include "Slic3r/Biz/Scene/SceneInteractor.hpp"

#include "Slic3r/Assert.hpp"
#include "Slic3r/Exception.hpp"
#include "Slic3r/Biz/Arrange/Arrange.hpp"
#include "Slic3r/Biz/Utils/Transformation.hpp"
#include "Slic3r/Domain/BedInstance.hpp"
#include "Slic3r/Domain/Types.hpp"

#include "Slic3r/Biz/Arrange/Arrange.hpp"
#include "Slic3r/Biz/Algorithms/ModelObject.hpp"
#include "Slic3r/Biz/Algorithms/ModelVolume.hpp"
#include "Slic3r/Biz/Scene/BedFactory.hpp"
#include "Slic3r/Biz/ISelectedBedInstanceChangedListener.hpp"
#include "Slic3r/Math.hpp"
#include "Slic3r/Biz/Algorithms/Bed.hpp"
#include "Slic3r/Biz/Algorithms/BoundingBox.hpp"
#include "Slic3r/Biz/Algorithms/ClipperUtils.hpp"
#include "Slic3r/Biz/Algorithms/Geometry/ConvexHull.hpp"
#include "Slic3r/Biz/Algorithms/ModelInstance.hpp"
#include "Slic3r/Biz/Algorithms/ModelObject.hpp"
#include "Slic3r/Biz/Algorithms/ModelVolume.hpp"
#include "Slic3r/Biz/Algorithms/Point.hpp"
#include "Slic3r/Biz/Algorithms/Polygon.hpp"
#include "Slic3r/Biz/Algorithms/PolygonUtils.hpp"
#include "Slic3r/Biz/Arrange/Arrange.hpp"
#include "Slic3r/Biz/ISelectedBedInstanceChangedListener.hpp"
#include "Slic3r/Biz/Scene/BedFactory.hpp"
#include "Slic3r/Biz/Scene/MeshRepairJob.hpp"
#include "Slic3r/Biz/Scene/SelectionExtents.hpp"
#include "Slic3r/Biz/Config/BedShape.hpp"
#include "Slic3r/Biz/Platform/JobManager/JobManager.hpp"
#include "Slic3r/Biz/Platform/PlatformServices.hpp"
#include "Slic3r/Biz/Utils/SetDiff.hpp"
#include "Slic3r/Directories.hpp"
#include "Slic3r/Domain/BedInstance.hpp"
#include "Slic3r/Domain/Types.hpp"
#include "Slic3r/Log.hpp"
#include "Slic3r/Math.hpp"
#include "Slic3r/Biz/Arrange/Arrange.hpp"

#include <algorithm>
#include <boost/filesystem/path.hpp>
#include <fmt/format.h>
#include <fmt/ostream.h>
#include <fmt/ranges.h>
#include <map>
#include <set>
#include <tuple>
#include <unordered_set>
#include <vector>

#include <tracy/Tracy.hpp>

using Eigen::Rotation2Dd;
using Slic3r::Biz::Algorithms::Bed::BedContainmentState;
using Slic3r::Biz::Algorithms::Bed::WipeTowerCollisionData;
using Slic3r::Biz::Slicing::WipeTowerGeometry;
using Slic3r::Domain::BedContainer;
using Slic3r::Domain::BedInstance;
using Slic3r::Domain::BedRef;
using Slic3r::Domain::BoundingBox2d;
using Slic3r::Domain::ConstModelInstanceList;
using Slic3r::Domain::ElementRef;
using Slic3r::Domain::ElementRefs;
using Slic3r::Domain::FacetsAnnotationKind;
using Slic3r::Domain::Model;
using Slic3r::Domain::ModelInstance;
using Slic3r::Domain::ModelObject;
using Slic3r::Domain::ModelObjectPtrs;
using Slic3r::Domain::ModelVolume;
using Slic3r::Domain::ModelVolumePtrs;
using Slic3r::Domain::Point;
using Slic3r::Domain::Points;
using Slic3r::Domain::Polygon;
using Slic3r::Domain::Polygons;
using Slic3r::Domain::Project;
using Slic3r::Domain::SelectionId;
using Slic3r::Domain::SquareMatrix4d;
using Slic3r::Domain::Transform3d;
using Slic3r::Domain::Vec2d;
using Slic3r::Domain::Vec2ds;
using Slic3r::Domain::Vec3d;

using namespace Slic3r::Biz;

namespace fmt {
template <>
struct formatter<Slic3r::Domain::ElementRef>
{
    // Parse format specifications (you can extend this if needed)
    constexpr auto parse(format_parse_context& ctx) -> decltype(ctx.begin())
    {
        return ctx.begin();
    }

    // Format the object
    template <typename FormatContext>
    auto format(const Slic3r::Domain::ElementRef& e, FormatContext& ctx) const -> decltype(ctx.out())
    {
        return fmt::format_to(ctx.out(), "{{obj: {}, inst: {}, vol: {}}}", e.object_id, e.instance_id, e.volume_id);
    }
};
} // namespace fmt

namespace Slic3r::Biz::Scene {

static const Vec2d BED_GAP = {20.0, 20.0};
using Domain::Transformation;
using Domain::TriangleMesh;

namespace {
Transformation transform_product(const SceneInteractor::Transform& orig_xform, const SceneInteractor::Transform& delta)
{
    DEBUG_ASSERT(fabs(delta.determinant()) > 1e-9);
    DEBUG_ASSERT(fabs(orig_xform.determinant()) > 1e-9);

    SceneInteractor::Transform xform = delta * orig_xform;

    DEBUG_ASSERT(fabs(xform.determinant()) > 1e-9);

    return Transformation{Transform3d{xform}};
}

static Domain::SquareMatrix4d get_wipe_tower_transform(const Domain::BedInstance& bed_instance)
{
    Domain::Transform3d result{bed_instance.transformation.get_matrix()};
    const Vec2d position{bed_instance.wipe_tower.position};
    result.translate(Vec3d{position.x(), position.y(), 0.0});
    result.rotate(
        Eigen::AngleAxisd{Slic3r::deg2rad(bed_instance.wipe_tower.rotation), Vec3d::UnitZ()}
    );

    return result.matrix();
};

static double get_xy_rotation(
    const Transformation& transformation
) {
    const Domain::SquareMatrix3d rotation{transformation.get_rotation_matrix().rotation()};
    return std::atan2(rotation(1, 0), rotation(0, 0));
}

static void set_wipe_tower_transformation(
    const Transformation& transformation,
    Domain::BedInstance& bed_instance
)
{
    if (changes_z_rotation_or_position(transformation.get_matrix().matrix())) {
        return;
    }
    Domain::ModelWipeTower& wipe_tower{bed_instance.wipe_tower};
    wipe_tower.position =
        transformation.get_offset().head<2>() - bed_instance.transformation.get_offset().head<2>();
    const double angle =
        get_xy_rotation(transformation) - get_xy_rotation(bed_instance.transformation);
    wipe_tower.rotation = Slic3r::rad2deg(Slic3r::angle_to_0_2PI(angle));
}

SelectionMode transform_selection_instance_mode(
    const SceneInteractorProjectContext& proj,
    const SceneInteractor::Transform& relative_transform,
    TransformMemento& memento
)
{
    ZoneScoped;

    const bool initialize_memento = memento.elements.empty();
    const auto& sel               = proj.object_selection;
    DEBUG_ASSERT(sel.mode == SelectionMode::Instance);

    const bool volume_transform_mode = memento.forced_volume_mode || changes_z_rotation_or_position(relative_transform);

    if (initialize_memento) {
        memento.elements.reserve(sel.elements.size());

        for (const auto& e : sel.elements) {
            if (!e.is_wipe_tower()) {
                continue;
            }
            Domain::BedInstance* bed_instance =
                proj.project.find_bed_instance_by_id(e.wipe_tower_id.bed_instance_id);
            ASSERT(bed_instance);
            memento.elements.insert({e, {e, get_wipe_tower_transform(*bed_instance)}});
        }
    }

    for (const auto& [_, e] : memento.elements) {
        if (!e.element.is_wipe_tower()) {
            continue;
        }
        Domain::BedInstance* bed_instance =
            proj.project.find_bed_instance_by_id(e.element.wipe_tower_id.bed_instance_id);
        ASSERT(bed_instance);
        set_wipe_tower_transformation(
            transform_product(e.original_xform, relative_transform),
            *bed_instance
        );
    }

    std::set<size_t> object_ids;
    for (const auto& e : sel.elements) {
        ModelInstance* inst = proj.project.find_instance_by_id(e.object_id, e.instance_id);
        if (!inst) {
            continue;
        }
        if (volume_transform_mode) {
            object_ids.insert(inst->get_object()->id().id);
        } else {
            if (initialize_memento) {
                memento.elements.insert({e, {e, inst->get_matrix().matrix()}});
            }
            inst->set_transformation(
                transform_product(memento.elements[e].original_xform, relative_transform)
            );
        }
    }

    if (!volume_transform_mode)
        return SelectionMode::Instance;

    memento.forced_volume_mode = true;

    for (size_t object_id : object_ids) {
        auto* obj = proj.project.find_object_by_id(object_id);

        auto first_inst_it = std::ranges::find_if(
            proj.object_selection.elements,
            [object_id](const auto& e) { return e.object_id == object_id; }
        );
        ASSERT(first_inst_it != proj.object_selection.elements.end());
        auto* first_inst = proj.project.find_instance_by_id(first_inst_it->object_id, first_inst_it->instance_id);
        const auto parent = first_inst->get_matrix();

        // We need to take the `relative_transform` and turn it into local one
        const SceneInteractor::Transform
            volume_relative_transform = (parent.inverse() * relative_transform * parent).matrix();
        for (auto* vol : obj->volumes) {
            Domain::ElementRef e{object_id, first_inst->id().id, vol->id().id};
            if (!memento.elements.contains(e))
                memento.elements.insert({e, {e, vol->get_matrix().matrix()}});
            vol->set_transformation(transform_product(memento.elements[e].original_xform, volume_relative_transform));
        }
    }

    return SelectionMode::Volume;
}

void transform_selection_volume_mode(const SceneInteractorProjectContext& proj, const SceneInteractor::Transform& relative_transform, TransformMemento& memento)
{
    ZoneScoped;

    const bool initialize_memento = memento.elements.empty();
    const auto& sel               = proj.object_selection;
    DEBUG_ASSERT(sel.mode == SelectionMode::Volume);
    ASSERT(!sel.elements.empty());
    const auto& first_el = sel.elements[0];
    // assert that all elements are of same instance
    DEBUG_ASSERT(
        std::all_of(
            ++sel.elements.begin(),
            sel.elements.end(),
            [inst_id = first_el.instance_id](const auto& el) { return el.instance_id == inst_id; }
        )
    );
    const auto* first_inst = proj.project.find_instance_by_id(first_el.object_id, first_el.instance_id);
    const auto parent = first_inst->get_matrix();
    // We need to take the `relative_transform` which is in world space and turn it into local space
    const SceneInteractor::Transform
        volume_relative_transform = (parent.inverse() * relative_transform * parent).matrix();
    if (initialize_memento)
        memento.elements.reserve(sel.elements.size());
    for (const auto& e : sel.elements) {
        DEBUG_ASSERT(e.volume_id != 0);
        auto* vol = proj.project.find_volume_by_id(e.object_id, e.volume_id);
        if (initialize_memento)
            memento.elements.insert({e, {e, vol->get_matrix().matrix()}});
        vol->set_transformation(transform_product(memento.elements[e].original_xform, volume_relative_transform));
    }
}

} // namespace

SceneInteractor::SceneInteractor(Domain::Workbench& workbench) : m_workbench(workbench) {}

void SceneInteractor::on_selected_project_changed(size_t index)
{

    auto& project = m_workbench.project(index);
    if (m_projects.count(index) == 0) {
        BedSelection selection{};
        selection.on_change = [this, index](const BedSelection& bed_selection)
        {
            invoke_listeners<ISelectedBedInstancesChangedListener>(
                [&](auto* l) { l->on_selected_bed_instances_changed(index, bed_selection); }
            );
        };
        m_projects.emplace(index, SceneInteractorProjectContext{project, selection});
    }
    m_selected_project_id = index;
}

void SceneInteractor::on_selected_config_container_changed(Domain::SelectionId project_id, Domain::SelectionId container_id)
{
    DEBUG_ASSERT(project_id == m_selected_project_id);
    if (m_selected_config_container_id == container_id)
        return;

    m_selected_config_container_id = container_id;
    const auto& cc = m_workbench.project(project_id).find_config_container(container_id);
    ASSERT(cc->bed_instances().size() >= 1);
    const auto& bed_instances = cc->bed_instances();
    auto& selection           = bed_selection();
    ASSERT(!selection.empty());
    if (selection.last_selected_bed().config_container_id != container_id) {
        // Select CC's first bed if not already selected any of CC's
        selection.select_one(
            {container_id, bed_instances.front()->id().id},
            CameraActionOnBedSelection::CenterOnBed
        );
    }
}

const ObjectSelection& SceneInteractor::object_selection() const
{
    return object_selection(m_selected_project_id);
}

const ObjectSelection& SceneInteractor::object_selection(Domain::SelectionId project_id) const
{
    ASSERT(project_id != Domain::INVALID_ID);
    const auto it = m_projects.find(project_id);
    ASSERT(it != m_projects.end());
    return it->second.object_selection;
}

void SceneInteractor::set_object_selection(const ObjectSelection& raw_selection){
    set_object_selection(raw_selection, m_selected_project_id);
}

void SceneInteractor::set_object_selection(
    const ObjectSelection& raw_selection,
    Domain::SelectionId project_id) 
{
    const auto it = m_projects.find(project_id);
    ASSERT(it != m_projects.end());

    ObjectSelection selection = raw_selection;
    normalize_object_selection(selection);

    auto& project_context = it->second;
    ObjectSelection sel{.mode = selection.mode};
    for (const auto& e : selection.elements) {
        if (e.has_instance() || e.is_wipe_tower()) {
            sel.elements.push_back(e);
            continue;
        }
        const auto* obj = project_context.project.find_object_by_id(e.object_id);
        ASSERT(obj != nullptr);
        for (const auto& inst : obj->instances)
            sel.elements.emplace_back(e.object_id, inst->id().id);
    }

    DEBUG_ASSERT(sel.is_valid());

    if (project_context.object_selection == sel) {
        return;
    }
    project_context.object_selection = sel;

    invoke_listeners<ISceneSelectionChangedListener>(
        [&](auto* l) { l->on_scene_selection_changed(m_selected_project_id, sel); }
    );
    update_selection_bounding_box();
}

Domain::ElementRefs SceneInteractor::selected_instance_all_volumes() const
{
    ASSERT(m_selected_project_id != Domain::INVALID_ID);
    const auto it = m_projects.find(m_selected_project_id);
    ASSERT(it != m_projects.end());

    const ObjectSelection& selection{it->second.object_selection};

    Domain::ElementRefs result;
    if (selection.mode != SelectionMode::Instance || selection.elements.size() != 1)
        return result;

    const Domain::ElementRef& element = selection.elements.front();
    const Domain::ModelObject* object{
        m_workbench.project(m_selected_project_id).find_object_by_id(element.object_id)
    };

    ASSERT(element.has_instance());
    for (const Domain::ModelVolume* volume : object->volumes) {
        const Domain::ElementRef ref{element.object_id, element.instance_id, volume->id().id};
        result.push_back(ref);
    }

    return result;
}

static Domain::SquareMatrix3d reset_shear(const Domain::SquareMatrix3d& matrix)
{
    const Eigen::JacobiSVD<Domain::SquareMatrix3d> svd(
        matrix,
        Eigen::ComputeFullU | Eigen::ComputeFullV
    );

    const Domain::SquareMatrix3d rotation{svd.matrixU() * svd.matrixV().transpose()};
    const Domain::SquareMatrix3d symetric_scale{
        svd.matrixV() * svd.singularValues().asDiagonal() * svd.matrixV().transpose()
    };

    const Domain::SquareMatrix3d scale{symetric_scale.diagonal().asDiagonal()};

    return rotation * scale;
}

static bool has_shear(const Domain::SquareMatrix4d& matrix)
{
    Domain::SquareMatrix4d no_shear{matrix};
    no_shear.block(0, 0, 3, 3) = reset_shear(no_shear.block(0, 0, 3, 3));
    return ((no_shear - matrix).cwiseAbs().array() > 1e-6 * Domain::SquareMatrix4d::Ones().array())
        .any();
}

Domain::ElementRefs SceneInteractor::selected_volumes_with_shear() const
{
    ASSERT(m_selected_project_id != Domain::INVALID_ID);
    const auto it = m_projects.find(m_selected_project_id);
    ASSERT(it != m_projects.end());

    const ObjectSelection& selection{it->second.object_selection};

    Domain::ElementRefs result;
    for (const Domain::ElementRef& element : selection.elements) {
        const Domain::ModelInstance* instance{
            m_workbench.project(m_selected_project_id)
                .find_instance_by_id(element.object_id, element.instance_id)
        };

        if (element.has_volume()) {
            const Domain::ElementRef ref{element.object_id, 0, element.volume_id};
            const Domain::ModelVolume* volume{m_workbench.project(m_selected_project_id)
                                                  .find_volume_by_id(ref.object_id, ref.volume_id)};
            if (has_shear(volume->get_matrix().matrix())) {
                result.push_back(ref);
            }
        } else {
            ASSERT(element.has_instance());
            for (const Domain::ModelVolume* volume : instance->get_object()->volumes) {
                const Domain::ElementRef ref{element.object_id, 0, volume->id().id};
                if (has_shear(volume->get_matrix().matrix())) {
                    result.push_back(ref);
                }
            }
        }
    }

    return result;
}

ElementRefs SceneInteractor::selected_volumes() const
{
    const ObjectSelection& selection = this->object_selection();
    const Project& project           = m_workbench.project(m_selected_project_id);

    ElementRefs volume_refs;
    for (const ElementRef& element : selection.elements) {
        if (element.is_wipe_tower()) {
            continue;
        }

        if (selection.mode == SelectionMode::Volume) {
            if (project.find_volume_by_id(element.object_id, element.volume_id) != nullptr) {
                volume_refs.push_back(element);
            }

            continue;
        }

        const ModelObject* object = project.find_object_by_id(element.object_id);
        if (object == nullptr) {
            continue;
        }

        for (const ModelVolume* volume : object->volumes) {
            volume_refs.emplace_back(element.object_id, element.instance_id, volume->id().id);
        }
    }

    const auto volume_key = [](const ElementRef& element)
    { return std::tie(element.object_id, element.volume_id); };

    std::ranges::sort(volume_refs, {}, volume_key);
    volume_refs.erase(std::ranges::unique(volume_refs, {}, volume_key).begin(), volume_refs.end());

    return volume_refs;
}

std::set<SelectionReferenceFrame> SceneInteractor::object_selection_reference_frame_options() const
{
    using SelectionReferenceFrame::Bed;
    using SelectionReferenceFrame::Instance;
    using SelectionReferenceFrame::Volume;

    ASSERT(m_selected_project_id != Domain::INVALID_ID);
    const auto it = m_projects.find(m_selected_project_id);
    ASSERT(it != m_projects.end());

    const ObjectSelection& selection{it->second.object_selection};

    switch (selection.state()) {
    case SelectionState::SingleVolume: {
        if (!selected_volumes_with_shear().empty()) {
            return {Bed, Instance};
        }
        return {Bed, Instance, Volume};
    }
    case SelectionState::WholeInstance: {
        ASSERT(!selection.empty());
        if (selection.elements.size() == 1) {
            ModelObject* model_object{
                m_workbench.project(m_selected_project_id)
                    .find_object_by_id(selection.elements.front().object_id)
            };
            ASSERT(model_object);
            if (model_object->volumes.size() == 1) {
                return {Bed, Instance, Volume};
            }
        }
        return {Bed, Instance};
    }
    default:
        return {Bed};
    }
}

SelectionReferenceFrame SceneInteractor::object_selection_reference_frame() const {
    ASSERT(m_selected_project_id != Domain::INVALID_ID);
    const auto it = m_projects.find(m_selected_project_id);
    ASSERT(it != m_projects.end());

    return it->second.object_selection_reference_frame;
}

bool SceneInteractor::reload_object_selection_reference_frame(SelectionReferenceFrame preferred_frame)
{
    ASSERT(m_selected_project_id != Domain::INVALID_ID);
    const auto it = m_projects.find(m_selected_project_id);
    ASSERT(it != m_projects.end());

    const SelectionReferenceFrame previous{it->second.object_selection_reference_frame};
    const std::set<SelectionReferenceFrame> options{object_selection_reference_frame_options()};
    if (options.contains(preferred_frame)) {
        it->second.object_selection_reference_frame = preferred_frame;
    } else {
        if (options.contains(SelectionReferenceFrame::Instance)) {
            it->second.object_selection_reference_frame = SelectionReferenceFrame::Instance;
        } else {
            it->second.object_selection_reference_frame = SelectionReferenceFrame::Bed;
        }
    }

    const bool changed{previous != it->second.object_selection_reference_frame};
    if (changed) {
        update_selection_bounding_box();
    }
    return changed;
}

void SceneInteractor::normalize_object_selection(ObjectSelection& selection) const
{
    ASSERT(m_selected_project_id != Domain::INVALID_ID);
    const auto it = m_projects.find(m_selected_project_id);
    ASSERT(it != m_projects.end());

    selection.mode = SelectionMode::Volume;
    if (selection.elements.empty())
        return;

    // verify if promoting to Instance mode is needed
    const auto inst_id = selection.elements.front().instance_id;
    bool requires_instance_mode = std::any_of(
        selection.elements.begin(),
        selection.elements.end(),
        [inst_id](const auto& e) { return e.volume_id == 0 || e.instance_id != inst_id; }
    );

    if (!requires_instance_mode) {
        // are all volumes of a single instance selected ?
        bool single_instance = std::all_of(selection.elements.begin(), selection.elements.end(),
            [inst_id](const auto& e) { return e.instance_id == inst_id; });
        if (single_instance) {
            const auto object = it->second.project.find_object_by_id(selection.elements.front().object_id);
            if (object->volumes.size() == selection.elements.size())
                requires_instance_mode = true;
        }
    }

    if (requires_instance_mode) {
        selection.mode = SelectionMode::Instance;
        std::unordered_set<Domain::ElementRef> unique_inst_elements;

        for (const auto& e : selection.elements) {
            if (e.is_wipe_tower()) {
                unique_inst_elements.insert(e);
                continue;
            }
            unique_inst_elements.insert(Domain::ElementRef{e.object_id, e.instance_id, 0});
        }

        selection.elements.clear();
        selection.elements.insert(selection.elements.end(), unique_inst_elements.begin(), unique_inst_elements.end());
    }
}

void SceneInteractor::clear_object_selection()
{
    set_object_selection(ObjectSelection());
}

void SceneInteractor::modify_selection(const std::function<void(ObjectSelection&)>& modifier)
{
    const auto it = m_projects.find(m_selected_project_id);
    ASSERT(it != m_projects.end());
    auto& selection = it->second.object_selection;
    modifier(selection);
    DEBUG_ASSERT(selection.is_valid());
    update_selection_bounding_box();
    invoke_listeners<ISceneSelectionChangedListener>(
        [&](auto* l) { l->on_scene_selection_changed(m_selected_project_id, selection); }
    );
}

const BedSelection& SceneInteractor::bed_selection() const
{
    const BedSelection* result{bed_selection(m_selected_project_id)};
    return *ASSERT_VAL(result);
}

BedSelection& SceneInteractor::bed_selection()
{
    BedSelection* result{bed_selection(m_selected_project_id)};
    return *ASSERT_VAL(result);
}

const BedSelection* SceneInteractor::bed_selection(const Domain::SelectionId project_id) const
{
    ASSERT(project_id != Domain::INVALID_ID);
    const auto it{m_projects.find(project_id)};
    if (it == m_projects.end()) {
        return nullptr;
    }
    return &it->second.bed_selection;
}

BedSelection* SceneInteractor::bed_selection(const Domain::SelectionId project_id)
{
    ASSERT(project_id != Domain::INVALID_ID);
    const auto it{m_projects.find(project_id)};
    if (it == m_projects.end()) {
        return nullptr;
    }
    return &it->second.bed_selection;
}

ElementRefs SceneInteractor::new_object_from_mesh(
    TriangleMesh&& mesh,
    const std::string& name,
    const std::optional<ModelVolume::Source>& volume_source
)
{
    UpdateObjectFn update_object = [&name, &volume_source](ModelObject& object)
    {
        object.name                  = name;
        object.volumes.front()->name = name;
        if (volume_source.has_value()) {
            object.input_file              = volume_source->input_file;
            object.volumes.front()->source = *volume_source;
        }

        //for (Domain::ModelVolume* volume : object.volumes)
        //    volume->name = name;
    };

    return new_object_from_mesh(std::move(mesh), m_selected_project_id, update_object);
}

ElementRefs SceneInteractor::new_object_from_mesh(
    TriangleMesh&& mesh,
    SelectionId project_id,
    UpdateObjectFn update_object
)
{
    auto& project = m_workbench.project(project_id);
    auto& obj     = *project.model().add_object();
    Algorithms::ModelObject::add_volume(&obj, std::move(mesh));
    auto& inst    = *obj.add_instance();
    if (update_object) {
        update_object(obj);

        for (auto* v : obj.volumes) {
            if (v->get_convex_hull_shared_ptr() == nullptr) {
                Algorithms::ModelVolume::calculate_convex_hull(*v);
            }
        }
    }

    ASSERT(Algorithms::ModelObject::are_volumes_sorted(&obj));

    const Domain::ElementRefs updated{{obj.id().id, inst.id().id}};
    // const Domain::ElementRefs updated_vols{{obj.id().id, inst.id().id, vol.id().id}};
    auto changes = m_bed_tracking.update_instances_bed_placement(project, updated);

    for (const auto& bed_ref : changes.updated_beds) {
        invoke_slicing_input_changed(bed_ref);
    }
    invoke_listeners<ISceneChangedListener>([&](auto* l) {
        l->on_instance_added(m_selected_project_id, updated);
    });

    if (!changes.updated_instances.empty()) {
        invoke_listeners<ISceneChangedListener>(
            [&](ISceneChangedListener* l)
            {
                l->on_instances_last_bed_updated(
                    {changes.updated_instances.cbegin(), changes.updated_instances.cend()}
                );
            }
        );
    }

    set_object_selection({ SelectionMode::Instance, updated }, project_id);

    return updated;
}

ElementRefs SceneInteractor::add_new_objects(const std::vector<Domain::ModelObject*>& objects)
{
    auto& project = m_workbench.project(m_selected_project_id);

    Domain::ModelObjectPtrs new_objects;
    for (Domain::ModelObject* object : objects) {
        Algorithms::ModelObject::sort_volumes(object);
        new_objects.emplace_back(project.model().add_object(*object));
    }

    notify_listener_on_objects(new_objects);

    ElementRefs new_instances;
    for (const ModelObject* new_object : new_objects) {
        for (const ModelInstance* instance : new_object->instances) {
            new_instances.emplace_back(new_object->id().id, instance->id().id, 0);
        }
    }

    return new_instances;
}

void SceneInteractor::add_volume_from_mesh(
    TriangleMesh&& mesh,
    Domain::ModelVolumeType volume_type,
    const std::string& name,
    const Transform& xform,
    const std::optional<ModelVolume::Source>& volume_source
)
{
    auto& project              = m_workbench.project(m_selected_project_id);
    const ObjectSelection& sel = object_selection();
    ASSERT(sel.mode == SelectionMode::Volume || sel.only_single_object());
    const size_t obj_id  = sel.elements[0].object_id;
    const size_t inst_id = sel.elements[0].instance_id;
    Domain::ElementRefs updated;

    auto& obj = *project.find_object_by_id(obj_id);
    auto& vol = *Algorithms::ModelObject::add_volume(&obj, std::move(mesh), volume_type);
    vol.name  = name;

    if (volume_source.has_value()) {
        vol.source = *volume_source;
    }

    if (xform != Domain::SquareMatrix4d::Identity()) {
        // Apply transformations only if explicitly set.
        // By default, the object controls the transformation of the volume added to it.
        vol.set_transformation(Transform3d{xform});
    }
    updated.emplace_back(obj.id().id, inst_id, vol.id().id);

    Algorithms::ModelObject::sort_volumes(&obj);

    invoke_listeners<ISceneChangedListener>(
        [&](auto* l) { l->on_volume_added(m_selected_project_id, updated); }
    );

    set_object_selection({SelectionMode::Volume, updated});
    update_elements_bed_placement(sel.elements, sel.mode == SelectionMode::Volume);
}

void SceneInteractor::add_volume_into_selected_object(const Domain::ModelVolume& volume)
{
    const ObjectSelection& sel = object_selection();
    ASSERT(sel.mode == SelectionMode::Volume || sel.only_single_object());
    add_volume(m_selected_project_id, sel.elements[0].instance_id,
        [&volume](Domain::ModelObject& object){
            return object.add_volume(volume);
        });
}

void SceneInteractor::add_volume(
    Domain::SelectionId project_id,
    Domain::SelectionId instance_id,
    const std::function<Domain::ModelVolume*(Domain::ModelObject&)>& factory
)
{
    auto& p = m_workbench.project(project_id);
    auto* instance = p.find_instance_by_id(instance_id);
    ASSERT(instance != nullptr);
    auto* obj = instance->get_object();

    auto* volume = factory(*obj);

    // verify that the factory added volume to the object
    ASSERT(std::ranges::find(obj->volumes, volume) != obj->volumes.end());

    Algorithms::ModelObject::sort_volumes(obj);

    Domain::ElementRefs updated = {
        Domain::ElementRef(obj->id().id, instance_id, volume->id().id)
    };

    invoke_listeners<ISceneChangedListener>([&](auto* l) {
        l->on_volume_added(project_id, updated);
    });
    set_object_selection({ SelectionMode::Volume, updated}, project_id);

    auto changes = m_bed_tracking.update_instances_bed_placement(p, updated);
    for (const auto& bed_ref : changes.updated_beds)
        invoke_slicing_input_changed(bed_ref);
}

void SceneInteractor::set_selected_volume_type(Domain::ModelVolumeType volume_type)
{
    const ObjectSelection& selection = object_selection();
    ASSERT(selection.mode == Biz::Scene::SelectionMode::Volume && !selection.empty());

    auto& project                      = m_workbench.project(m_selected_project_id);
    const Domain::ElementRefs& volumes = selection.elements;

    Domain::ModelObject* object = project.find_object_by_id(volumes.front().object_id);
    ASSERT(object);

    for (const Domain::ElementRef& el : volumes) {
        Domain::ModelVolume* volume = project.find_volume_by_id(el.object_id, el.volume_id);
        ASSERT(volume);
        volume->set_type(volume_type);
    }
    ASSERT(object->parts_count() > 0);
    Algorithms::ModelObject::sort_volumes(object);

    invoke_listeners<ISceneChangedListener>(
        [&](auto* l) { l->on_volume_type_changed(m_selected_project_id, volumes); }
    );

    const BedTrackingChanges changes = update_elements_bed_placement(volumes, true);
    for (const auto& bed_ref : changes.updated_beds)
        invoke_slicing_input_changed(bed_ref);
}

ElementRefs SceneInteractor::add_instance(const Vec2d& offset)
{
    auto& project              = m_workbench.project(m_selected_project_id);
    const ObjectSelection& sel = object_selection();
    DEBUG_ASSERT(sel.elements.size() >= 1);
    size_t obj_id = sel.elements[0].object_id;
    Domain::ElementRefs updated;

    auto& obj         = *project.find_object_by_id(obj_id);
    Transform3d trafo = obj.instances.back()->get_matrix();
    trafo.pretranslate(Domain::Vec3d(offset.x(), offset.y(), 0.));
    auto& inst = *obj.add_instance();
    inst.set_transformation(Transformation{trafo});
    updated.emplace_back(obj.id().id, inst.id().id);

    auto changes = m_bed_tracking.update_instances_bed_placement(project, updated);
    for (const auto& bed_ref : changes.updated_beds) {
        invoke_slicing_input_changed(bed_ref);
    }

    invoke_listeners<ISceneChangedListener>(
        [&](auto* l) { l->on_instance_added(m_selected_project_id, updated); }
    );

    if (!changes.updated_instances.empty()) {
        invoke_listeners<ISceneChangedListener>(
            [&](ISceneChangedListener* l)
            {
                l->on_instances_last_bed_updated(
                    {changes.updated_instances.cbegin(), changes.updated_instances.cend()}
                );
            }
        );
    }

    set_object_selection({SelectionMode::Instance, updated});

    return updated;
}

ModelObjectPtrs SceneInteractor::clone_objects_from_project(
    SelectionId source_project_id,
    const std::vector<ElementRef>& source_elements
)
{
    const Project& source_project = m_workbench.project(source_project_id);
    Project& selected_project     = m_workbench.project(m_selected_project_id);

    std::map<size_t, std::vector<size_t>> instances_per_object;
    for (const ElementRef& source_element : source_elements) {
        instances_per_object[source_element.object_id].push_back(source_element.instance_id);
    }

    ModelObjectPtrs new_objects;
    for (const auto& [object_id, instance_ids] : instances_per_object) {
        const ModelObject* source_model_object = source_project.find_object_by_id(object_id);
        if (source_model_object == nullptr) {
            continue;
        }

        std::unique_ptr<ModelObject> new_model_object =
            ModelObject::new_clone(*source_model_object, instance_ids);
        ASSERT(!new_model_object->instances.empty());

        Algorithms::ModelObject::sort_volumes(new_model_object.get());
        ModelObject* added_model_object =
            selected_project.model().add_object(std::move(new_model_object));
        new_objects.push_back(added_model_object);
    }

    this->notify_listener_on_objects(new_objects);

    return new_objects;
}

void SceneInteractor::delete_selected_object_last_instance()
{
    auto& project              = m_workbench.project(m_selected_project_id);
    const ObjectSelection& sel = object_selection();
    DEBUG_ASSERT(!sel.elements.empty()
        && sel.only_single_object()
        && sel.mode == Slic3r::Biz::Scene::SelectionMode::Instance);

    Domain::ModelObject* object   = project.find_object_by_id(sel.elements[0].object_id);

    BedTrackingChanges changes;
    remove_instance_from_bed(project, object->instances.back(), changes);

    Domain::ElementRefs to_remove({ {sel.elements[0].object_id, object->instances.back()->id().id, 0} });
    object->delete_last_instance();
    Domain::ElementRefs to_select({ {sel.elements[0].object_id, object->instances.back()->id().id, 0} });

    invoke_listeners<ISceneChangedListener>(
        [&](auto* l) { l->on_instance_removed(m_selected_project_id, to_remove); }
    );

    for (const auto& bed_ref : changes.updated_beds) {
        invoke_slicing_input_changed(bed_ref);
    }

    set_object_selection({SelectionMode::Instance, to_select});
}

ElementRefs SceneInteractor::set_selected_objects_instance_count(int count)
{
    auto& project = m_workbench.project(m_selected_project_id);
    ObjectSelection sel = object_selection();
    DEBUG_ASSERT(!sel.empty()
        && sel.mode == Biz::Scene::SelectionMode::Instance
    );

    std::set<size_t> object_ids;
    for (const auto& el : sel.elements) {
        object_ids.emplace(el.object_id);
    }

    const Vec2d offset{ 10.f,5.f };

    Domain::ElementRefs to_add;
    Domain::ElementRefs to_remove;
    BedTrackingChanges changes;

    for (size_t object_id : object_ids) {
        Domain::ModelObject* object = project.find_object_by_id(object_id);
        if (int diff = count - static_cast<int>(object->instances.size()); diff == 0)
            continue;
        else if (diff > 0) {
            // increase instances
            while (diff > 0) {
                Transform3d trafo = object->instances.back()->get_matrix();
                trafo.pretranslate(Domain::Vec3d(offset.x(), offset.y(), 0.));
                Domain::ModelInstance* inst = object->add_instance();
                inst->set_transformation(Transformation{ trafo });
                Domain::ElementRef add_el = { object_id, inst->id().id };
                changes.append(m_bed_tracking.update_instances_bed_placement(project, {add_el}));
                to_add.emplace_back(add_el);
                diff--;
            }
        } else {
            // decrease_instances
            while (diff < 0) {
                Domain::ModelInstance* last_instance = object->instances.back();
                Domain::ElementRef del_el = { object_id, last_instance->id().id };
                to_remove.emplace_back(del_el);
                remove_instance_from_bed(project, last_instance, changes);
                object->delete_last_instance();
                std::erase_if(sel.elements, [del_el](const Domain::ElementRef& el) {return el == del_el; });
                diff++;
            }
        }
    }

    if (!to_add.empty()) {
        invoke_listeners<ISceneChangedListener>(
            [&](auto* l) { l->on_instance_added(m_selected_project_id, to_add); }
        );
    }

    if (!to_remove.empty()) {
        invoke_listeners<ISceneChangedListener>(
            [&](auto* l) { l->on_instance_removed(m_selected_project_id, to_remove); }
        );
    }

    for (const auto& bed_ref : changes.updated_beds) {
        invoke_slicing_input_changed(bed_ref);
    }

    set_object_selection(sel);

    return to_add;
}

void SceneInteractor::notify_listener_on_objects(const Domain::ModelObjectPtrs& objects)
{
    auto& project = m_workbench.project(m_selected_project_id);
    Domain::ElementRefs updated;
    for (const Domain::ModelObject* object : objects) {
        SPDLOG_DEBUG("Notify listener obj {}", object->id().id);
        for (const Domain::ModelInstance* inst : object->instances)
            updated.emplace_back(object->id().id, inst->id().id, 0);
    }

    auto changes = m_bed_tracking.update_instances_bed_placement(project, updated);
    for (const auto& bed_ref : changes.updated_beds)
        invoke_slicing_input_changed(bed_ref);

    if (updated.size()) {
        SPDLOG_DEBUG("- on_instance_added: {}", fmt::join(updated, ", "));
        invoke_listeners<ISceneChangedListener>(
            [&](auto* l) { l->on_instance_added(m_selected_project_id, updated); }
        );
    }
    set_object_selection({SelectionMode::Instance, {updated}});
}

void SceneInteractor::notify_listener_on_objects(const Domain::Project& project)
{
    notify_listener_on_objects(project.model().objects);
}

void SceneInteractor::change_volume_meshes(RefMeshes&& meshes)
{
    VolumeMeshReplacements replacements;
    replacements.reserve(meshes.size());
    for (RefMesh& mesh : meshes) {
        replacements.push_back(VolumeMeshReplacement{mesh.first, std::move(mesh.second)});
    }

    this->change_volume_meshes(std::move(replacements));
}

ElementRefs SceneInteractor::change_volume_meshes(VolumeMeshReplacements&& replacements)
{
    Project& project = m_workbench.project(m_selected_project_id);

    ModelVolumePtrs volumes;
    volumes.reserve(replacements.size());
    for (const VolumeMeshReplacement& replacement : replacements) {
        ModelVolume* volume_ptr =
            project.find_volume_by_id(replacement.element.object_id, replacement.element.volume_id);

        if (volume_ptr == nullptr) {
            return {};
        }

        volumes.push_back(volume_ptr);
    }

    ElementRefs removed_ids;
    ElementRefs updated_ids;
    removed_ids.reserve(replacements.size());
    updated_ids.reserve(replacements.size());
    std::vector<size_t> object_ids;
    object_ids.reserve(replacements.size());

    for (VolumeMeshReplacement& replacement : replacements) {
        const size_t replacement_idx = &replacement - replacements.data();
        const ElementRef& id         = replacement.element;

        ModelVolume& volume = *volumes[replacement_idx];
        volume.reset_extra_facets();
        volume.set_mesh(std::move(replacement.mesh));

        if (replacement.new_transformation.has_value()) {
            volume.set_transformation(replacement.new_transformation.value());
        }

        if (replacement.new_source.has_value()) {
            volume.source = replacement.new_source.value();
        }

        if (replacement.new_name.has_value()) {
            volume.name = replacement.new_name.value();

            ModelObject* object = project.find_object_by_id(id.object_id);
            if (object != nullptr && object->volumes.size() == 1) {
                object->name = replacement.new_name.value();
            }
        }

        Algorithms::ModelVolume::calculate_convex_hull(volume);
        volume.set_new_unique_id();

        object_ids.push_back(id.object_id);
        // Invalidate the instance id, so the volume node is removed from all instances.
        removed_ids.emplace_back(id.object_id, 0, id.volume_id);
        updated_ids.emplace_back(id.object_id, id.instance_id, volume.id().id);
    }

    std::sort(object_ids.begin(), object_ids.end());
    object_ids.erase(std::unique(object_ids.begin(), object_ids.end()), object_ids.end());
    for (size_t object_id : object_ids)
        project.find_object_by_id(object_id)->invalidate_bounding_box();

    invoke_listeners<ISceneChangedListener>(
        [&removed_ids, &updated_ids, project_id = m_selected_project_id](auto* l)
        {
            l->on_volume_removed(project_id, removed_ids);
            l->on_volume_added(project_id, updated_ids);
            l->on_volume_mesh_changed(project_id, updated_ids);
        }
    );

    set_object_selection({SelectionMode::Volume, updated_ids});
    if (object_ids.size() == 1
        && project.find_object_by_id(object_ids.front())->volumes.size() == 1)
    {
        // For single-volume objects, set_object_selection() does not trigger
        // selection changes, so the bounding box must be updated manually.
        update_selection_bounding_box();
    }
    auto changes = m_bed_tracking.update_instances_bed_placement(project, updated_ids);
    for (const auto& bed_ref : changes.updated_beds)
        invoke_slicing_input_changed(bed_ref);

    return updated_ids;
}

#if SLIC3R_ENABLE_WIN10_MESH_REPAIR
bool SceneInteractor::can_repair_selected_object() const
{
    const Biz::Scene::ObjectSelection& selection = object_selection();
    if (selection.elements.size() != 1 || selection.contains_wipe_tower())
        return false;

    const Domain::Project& project = m_workbench.project(m_selected_project_id);
    return project.find_object_by_id(selection.elements.front().object_id) != nullptr;
}

void SceneInteractor::repair_selected_object()
{
    const Biz::Scene::ObjectSelection& selection = object_selection();
    if (selection.elements.size() != 1 || selection.contains_wipe_tower())
        return;

    const Domain::ElementRef& sel_element = selection.elements.front();
    Domain::Project& project    = m_workbench.project(m_selected_project_id);
    Domain::ModelObject* object = project.find_object_by_id(sel_element.object_id);
    if (object == nullptr)
        return;

    const size_t sel_instance_id =
        sel_element.instance_id == 0 ? object->instances.front()->id().id : sel_element.instance_id;

    RefMeshes meshes;
    meshes.reserve(object->volumes.size());
    for (Domain::ModelVolume* volume : object->volumes)
        meshes.emplace_back(
            Domain::ElementRef{sel_element.object_id, sel_instance_id, volume->id().id},
            volume->mesh()
        );

    if (meshes.empty())
        return;

    const Domain::SelectionId project_id = m_selected_project_id;
    Platform::PlatformServices::instance()
        .job_manager()
        .create_job("RepairObjectMesh", repair_meshes, std::move(meshes))
        .set_project_id(project_id)
        .on_result(
            [this, project_id](RefMeshes&& result)
            {
                if (m_workbench.find_project_by_id(project_id) == nullptr)
                    return;
                change_volume_meshes(std::move(result));
                undo_provider().take_snapshot(UndoSnapshotType::RepairObjectMesh);
            }
        )
        .on_exception(
            [](const std::exception_ptr& exception)
            {
                // Unlike most job algorithms, mesh repair calls into an external OS
                // service (WinRT on Windows) that is expected to fail sometimes (no
                // WinRT runtime present, the service rejecting the mesh, allocation
                // failure on a corrupt/huge mesh, ...). Report it through the "Repair
                // Failed" notification instead of letting it fall through to the
                // generic unhandled-exception path (there is no dedicated handling
                // for Slic3r::Exception/CriticalException anywhere in this codebase;
                // an uncaught exception here is handled the same way regardless of
                // its type).
                try {
                    std::rethrow_exception(exception);
                } catch (const std::exception& e) {
                    SPDLOG_WARN("Mesh repair failed: {}", e.what());
                } catch (...) {
                    throw;
                }
            }
        )
        .start();
}
#endif // SLIC3R_ENABLE_WIN10_MESH_REPAIR

void SceneInteractor::modify_facets_annotations(
    const Domain::ElementRefs& volume_refs,
    FacetsAnnotationKind kind,
    const std::function<bool(const Domain::ElementRef&, ModelVolume&)>& modifier
)
{
    if (volume_refs.empty()) {
        return;
    }

    Project& project = m_workbench.project(m_selected_project_id);

    Domain::ElementRefs instances_to_update;
    for (const Domain::ElementRef& volume_ref : volume_refs) {
        ModelObject* model_object = project.find_object_by_id(volume_ref.object_id);
        ModelVolume* model_volume =
            project.find_volume_by_id(volume_ref.object_id, volume_ref.volume_id);

        ASSERT(model_object != nullptr);

        const bool modified{modifier(volume_ref, *model_volume)};
        for (const ModelInstance* model_instance : model_object->instances) {
            if (modified) {
                instances_to_update.emplace_back(
                    volume_ref.object_id,
                    model_instance->id().id,
                    volume_ref.volume_id
                );
            }
        }
    }

    BedTrackingChanges changes =
        m_bed_tracking.update_instances_bed_placement(project, instances_to_update);
    for (const Domain::BedRef& bed_ref : changes.updated_beds) {
        this->invoke_slicing_input_changed(bed_ref);
    }

    if (!instances_to_update.empty()) {
        invoke_listeners<ISceneChangedListener>(
            [&](ISceneChangedListener* l)
            {
                l->on_volume_facets_annotations_changed(
                    m_selected_project_id,
                    kind,
                    instances_to_update
                );
            }
        );
    }
}

void SceneInteractor::modify_layer_height_profile(
    const Domain::ElementRef& object_ref,
    const std::function<void(ModelObject&)>& modifier
)
{
    Project& project          = m_workbench.project(m_selected_project_id);
    ModelObject* model_object = project.find_object_by_id(object_ref.object_id);
    ASSERT(model_object != nullptr);

    modifier(*model_object);

    Domain::ElementRefs instance_refs;
    for (const ModelInstance* model_instance : model_object->instances) {
        instance_refs.emplace_back(object_ref.object_id, model_instance->id().id);
    }

    BedTrackingChanges changes =
        m_bed_tracking.update_instances_bed_placement(project, instance_refs);
    for (const Domain::BedRef& bed_ref : changes.updated_beds) {
        this->invoke_slicing_input_changed(bed_ref);
    }
}

void SceneInteractor::modify_layer_config_ranges(
    const Domain::ElementRef& object_ref,
    const std::function<void(ModelObject&)>& modifier
)
{
    Project& project          = m_workbench.project(m_selected_project_id);
    ModelObject* model_object = project.find_object_by_id(object_ref.object_id);
    ASSERT(model_object != nullptr);

    modifier(*model_object);

    Domain::ElementRefs instance_refs;
    for (const ModelInstance* model_instance : model_object->instances) {
        instance_refs.emplace_back(object_ref.object_id, model_instance->id().id);
    }

    BedTrackingChanges changes =
        m_bed_tracking.update_instances_bed_placement(project, instance_refs);
    for (const Domain::BedRef& bed_ref : changes.updated_beds) {
        this->invoke_slicing_input_changed(bed_ref);
    }
}

void SceneInteractor::edit_name(const Domain::ElementRef& id, const std::string& new_name)
{
    Domain::Project& project = m_workbench.project(m_selected_project_id);
    if (id.volume_id == 0)
        project.find_object_by_id(id.object_id)->name = new_name;
    else
        project.find_volume_by_id(id.object_id, id.volume_id)->name = new_name;
}

void SceneInteractor::set_printable(const Domain::ElementRef& id, bool is_printable)
{
    ASSERT(id.volume_id == 0);
    Domain::ElementRefs updated;

    Domain::Project& project = m_workbench.project(m_selected_project_id);
    if (id.instance_id == 0) {
        auto obj       = project.find_object_by_id(id.object_id);
        updated.reserve(obj->instances.size());
        for (auto& inst : obj->instances) {
            inst->printable = is_printable;
            updated.emplace_back(id.object_id, inst->id().id);
        }
    } else {
        project.find_instance_by_id(id.object_id, id.instance_id)->printable = is_printable;
        updated                                                              = {id};
    }

    auto changes = m_bed_tracking.update_instances_bed_placement(project, updated);

    invoke_listeners<ISceneChangedListener>(
        [&](auto* l)
        {
            l->on_instance_transformed(
                m_selected_project_id,
                updated,
                TransformState::Completed,
                changes
            );
        }
    );

    for (const auto& bed_ref : changes.updated_beds)
        invoke_slicing_input_changed(bed_ref);
}

void SceneInteractor::set_selected_instances_printable(bool is_printable)
{
    const Biz::Scene::ObjectSelection& selection = object_selection();

    if (!selection.empty() && selection.mode == Biz::Scene::SelectionMode::Instance) {
        Domain::Project& project = m_workbench.project(m_selected_project_id);

        Domain::ElementRefs updated;
        for (const Domain::ElementRef& id : selection.elements) {
            if (id.is_wipe_tower())
                continue;
            if (id.instance_id == 0) {
                auto obj = project.find_object_by_id(id.object_id);
                updated.reserve(obj->instances.size());
                for (auto& inst : obj->instances) {
                    inst->printable = is_printable;
                    updated.emplace_back(id.object_id, inst->id().id);
                }
            } else {
                project.find_instance_by_id(id.object_id, id.instance_id)->printable = is_printable;
                updated                                                              = {id};
            }
        }

        auto changes = m_bed_tracking.update_instances_bed_placement(project, updated);

        invoke_listeners<ISceneChangedListener>(
            [&](auto* l)
            {
                l->on_instance_transformed(
                    m_selected_project_id,
                    updated,
                    TransformState::Completed,
                    changes
                );
            }
        );

        for (const auto& bed_ref : changes.updated_beds)
            invoke_slicing_input_changed(bed_ref);
    }
}

bool SceneInteractor::selected_instances_printable() const
{
    bool is_printable{false};
    const Biz::Scene::ObjectSelection& selection = object_selection();
    if (!selection.empty() && selection.mode == Biz::Scene::SelectionMode::Instance) {
        Domain::Project& project = m_workbench.project(m_selected_project_id);
        for (const Domain::ElementRef& id : selection.elements) {
            if (id.is_wipe_tower())
                continue;
            if (id.instance_id == 0) {
                if (const Domain::ModelObject* obj = project.find_object_by_id(id.object_id)) {
                    for (const Domain::ModelInstance* instance : obj->instances) {
                        if (instance->printable) {
                            is_printable = true;
                            break;
                        }
                    }
                    if (is_printable) {
                        break;
                    }
                }
            } else {
                if (project.find_instance_by_id(id.object_id, id.instance_id)->printable) {
                    is_printable = true;
                    break;
                }
            }
        }
    }
    return is_printable;
}

void SceneInteractor::extract_selected_instances()
{
    const ObjectSelection& scene_selection = object_selection();
    if (scene_selection.empty() || scene_selection.mode != SelectionMode::Instance)
        return;

    bool all_instances_from_one_object = true;
    size_t object_id                   = scene_selection.elements[0].object_id;
    for (const auto& el : scene_selection.elements) {
        if (object_id != el.object_id) {
            all_instances_from_one_object = false;
            break;
        }
    }
    ASSERT(all_instances_from_one_object);

    Domain::Project& project = m_workbench.project(m_selected_project_id);
    Domain::Model& model     = project.model();

    ObjectSelection::ElementRefs to_remove = scene_selection.elements;
    Domain::ModelObjectPtrs new_objects;
    Domain::ModelObject* old_object = project.find_object_by_id(object_id);
    size_t sel_object_id            = old_object->id().id;

    BedTrackingChanges changes;
    if (old_object->instances.size() == to_remove.size()) {
        // split old_object instances into separate objects

        for (int inst_cnt = int(old_object->instances.size()) - 1; inst_cnt > 0; inst_cnt--) {
            // make a copy of the active object
            Domain::ModelObject* new_object = model.add_object(*old_object);
            new_objects.emplace_back(new_object);
            // delete no needed instances from new_object
            for (int idx = int(old_object->instances.size() - 1); idx >= 0; idx--) {
                if (inst_cnt != idx)
                    new_object->delete_instance(idx);
            }
        }
        // delete no needed instances from old_object
        for (size_t idx = old_object->instances.size() - 1; idx > 0; idx--) {
            remove_instance_from_bed(project, old_object->instances[idx], changes);
            old_object->delete_instance(idx);
        }

        Domain::ElementRef stay_el{sel_object_id, old_object->instances[0]->id().id};
        to_remove.erase(
            std::remove_if(
                to_remove.begin(),
                to_remove.end(),
                [stay_el](const Domain::ElementRef& el) { return el == stay_el; }
            ),
            to_remove.end()
        );
    } else {
        // extract selected instances into separate object

        // make a copy of the active object
        Domain::ModelObject* new_object = model.add_object(*old_object);
        new_objects.emplace_back(new_object);

        // delete no needed instances from both objects
        for (size_t idx = old_object->instances.size() - 1; idx != size_t(-1); idx--) {
            if (scene_selection.is_selected({ sel_object_id, old_object->instances[idx]->id().id })) {
                remove_instance_from_bed(project, old_object->instances[idx], changes);
                old_object->delete_instance(idx);
            }
            else
                new_object->delete_instance(idx);
        }
    }

    if (!changes.updated_instances.empty()) {
        invoke_listeners<ISceneChangedListener>(
            [&](ISceneChangedListener* l)
            {
                l->on_instances_last_bed_updated(
                    {changes.updated_instances.cbegin(), changes.updated_instances.cend()}
                );
            }
        );
    }

    // notify listener on changes

    notify_listener_on_objects(new_objects);
    invoke_listeners<ISceneChangedListener>(
        [&](auto* l) { l->on_instance_removed(m_selected_project_id, to_remove); }
    );
}

bool SceneInteractor::can_extract_selected_instances() const
{
    const Biz::Scene::ObjectSelection& selection = object_selection();
    if (!selection.empty()
        && selection.mode == Slic3r::Biz::Scene::SelectionMode::Instance
        && selection.only_single_object())
    {
        for (const Domain::ElementRef& el : selection.elements) {
            if (el.is_wipe_tower())
                continue;
            Domain::Project& project    = m_workbench.project(m_selected_project_id);
            Domain::ModelObject* object = project.find_object_by_id(el.object_id);
            ASSERT(object);
            return object->instances.size() > 1;
        }
    }
    return false;
}

bool SceneInteractor::can_split_selection_to_objects() const
{
    const Biz::Scene::ObjectSelection& selection = object_selection();
    if (!selection.empty()
        && selection.mode == Slic3r::Biz::Scene::SelectionMode::Instance
        && selection.only_single_object())
    {
        for (const Domain::ElementRef& el : selection.elements) {
            if (el.is_wipe_tower())
                continue;
            Domain::Project& project    = m_workbench.project(m_selected_project_id);
            Domain::ModelObject* object = project.find_object_by_id(el.object_id);
            ASSERT(object);
            const size_t volumes_cnt = object->volumes.size();
            return object->is_cut() ?
                false :
                volumes_cnt == 1 ?
                Biz::Algorithms::ModelVolume::is_splittable(*object->volumes[0]) :
                volumes_cnt == object->parts_count();
        }
    }
    return false;
}

void SceneInteractor::split_selection_to_objects()
{
    const Biz::Scene::ObjectSelection& selection = object_selection();
    if (selection.empty()
        || selection.mode != Slic3r::Biz::Scene::SelectionMode::Instance
        || !selection.only_single_object())
        return;

    for (const Domain::ElementRef& el : selection.elements) {
        if (el.is_wipe_tower())
            continue;
        Domain::ModelObject* object =
            m_workbench.project(m_selected_project_id).find_object_by_id(el.object_id);
        Domain::ModelObjectPtrs new_objects;
        Algorithms::ModelObject::split(object, &new_objects);

        delete_object(object);
        notify_listener_on_objects(new_objects);
        break;
    }
}

bool SceneInteractor::can_split_selection_to_volumes() const
{
    const Biz::Scene::ObjectSelection& selection = object_selection();
    if (selection.elements.size() == 1 && !selection.contains_wipe_tower()) {
        const Domain::ElementRef& el = selection.elements.front();
        Domain::Project& project     = m_workbench.project(m_selected_project_id);

        Domain::ModelObject* object = project.find_object_by_id(el.object_id);
        if (object->is_cut()) {
            return false;
        }

        Domain::ModelVolume* volume = selection.mode == Slic3r::Biz::Scene::SelectionMode::Volume ?
            project.find_volume_by_id(el.object_id, el.volume_id) :
            nullptr;
        if (!volume) {
            if (object->volumes.size() != 1) {
                return false;
            }
            volume = object->volumes.front();
        }
        ASSERT(volume);
        return Biz::Algorithms::ModelVolume::is_splittable(*volume);
    }
    return false;
}

void SceneInteractor::split_selection_to_volumes()
{
    const Biz::Scene::ObjectSelection& selection = object_selection();
    if (selection.elements.size() != 1 || selection.contains_wipe_tower())
        return;

    const Domain::ElementRef& sel_element = selection.elements.front();
    Domain::Project& project              = m_workbench.project(m_selected_project_id);

    Domain::ModelObject* object = project.find_object_by_id(sel_element.object_id);
    Domain::SelectionId sel_instance_id =
        sel_element.instance_id == 0 ? object->instances.front()->id().id : sel_element.instance_id;
    Domain::SelectionId sel_volume_id =
        sel_element.volume_id == 0 ? object->volumes.front()->id().id : sel_element.volume_id;

    Domain::ModelVolume* volume = selection.mode == Slic3r::Biz::Scene::SelectionMode::Volume ?
        project.find_volume_by_id(sel_element.object_id, sel_element.volume_id) :
        nullptr;
    if (!volume) {
        ASSERT(object->volumes.size() == 1);
        volume = object->volumes.front();
    }
    ASSERT(volume);

    Domain::ModelVolumePtrs& volumes = object->volumes;
    // get volume index in volumes before splitting
    size_t ivolume =
        std::distance(volumes.begin(), std::find(volumes.begin(), volumes.end(), volume));

    // Split the volume
    unsigned int max_extruders{1}; // ToDo get this value from config
    size_t created_volumes_cnt = Biz::Algorithms::ModelVolume::split(volume, max_extruders);

    // Remove a node associated with this volume, because it's id will be changed after spliting
    Domain::ElementRefs removed = {Domain::ElementRef(object->id().id, 0, sel_volume_id)};
    invoke_listeners<ISceneChangedListener>(
        [&](auto* l) { l->on_volume_removed(m_selected_project_id, removed); }
    );
    auto changes = m_bed_tracking.update_instances_bed_placement(project, removed);

    Domain::ElementRefs added;
    while (created_volumes_cnt != 0) {
        added.push_back(
            {Domain::ElementRef(object->id().id, sel_instance_id, volumes[ivolume++]->id().id)}
        );
        created_volumes_cnt--;
    }

    invoke_listeners<ISceneChangedListener>([&](auto* l)
                                            { l->on_volume_added(m_selected_project_id, added); });

    changes.append(m_bed_tracking.update_instances_bed_placement(project, added));
    for (const auto& bed_ref : changes.updated_beds)
        invoke_slicing_input_changed(bed_ref);

    set_object_selection({SelectionMode::Volume, added});
}

bool SceneInteractor::can_merge_selection_into_object() const
{
    const Biz::Scene::ObjectSelection& selection = object_selection();
    if (selection.mode == Slic3r::Biz::Scene::SelectionMode::Instance
        && selection.elements.size() > 1)
    {
        for (const Domain::ElementRef& el : selection.elements) {
            const Domain::ModelObject* object{
                m_workbench.project(m_selected_project_id).find_object_by_id(el.object_id)
            };
            if (!object || object->is_cut()) {
                return false;
            }
        }
        return true;
    }

    return false;
}

void SceneInteractor::merge_selection_into_object()
{
    const Biz::Scene::ObjectSelection& selection = object_selection();
    ASSERT(
        selection.mode == Slic3r::Biz::Scene::SelectionMode::Instance
        && selection.elements.size() > 1
    );

    Domain::Project& project = m_workbench.project(m_selected_project_id);

    std::vector<const Domain::ModelInstance*> instances;
    for (const Domain::ElementRef& el : selection.elements) {
        instances.push_back(project.find_instance_by_id(el.instance_id));
    }
    ASSERT(!instances.empty());
    Domain::ModelObject* new_object = Biz::Algorithms::ModelObject::merge(instances);

    delete_selected_elements();
    notify_listener_on_objects({new_object});
}

bool SceneInteractor::can_invalidate_cut_info() const
{
    const Biz::Scene::ObjectSelection& selection = object_selection();
    if (selection.mode == Slic3r::Biz::Scene::SelectionMode::Instance
        && selection.elements.size() == 1
        && !selection.contains_wipe_tower())
    {
        return m_workbench.project(m_selected_project_id)
            .find_object_by_id(selection.elements.front().object_id)
            ->is_cut();
    }
    return false;
}

bool SceneInteractor::can_export_selection_as_mesh() const
{
    const ObjectSelection& selection = this->object_selection();
    if (selection.empty() || selection.contains_wipe_tower() || !selection.only_single_object()) {
        return false;
    }

    if (selection.mode == SelectionMode::Volume) {
        ASSERT(selection.elements.front().has_volume());
        return selection.elements.size() == 1;
    }

    const Project& project    = m_workbench.project(m_selected_project_id);
    const ModelObject* object = project.find_object_by_id(selection.elements.front().object_id);
    if (object == nullptr) {
        return false;
    }

    return selection.elements.size() == 1 || selection.elements.size() == object->instances.size();
}

bool SceneInteractor::can_replace_selected_volume() const
{
    const ObjectSelection& selection = this->object_selection();
    if (selection.empty() || selection.contains_wipe_tower() || !selection.only_single_object()) {
        return false;
    }

    const ElementRefs volume_refs = this->selected_volumes();
    if (volume_refs.size() != 1) {
        return false;
    }

    const ModelObject* object =
        m_workbench.project(m_selected_project_id).find_object_by_id(volume_refs.front().object_id);

    return object != nullptr && !object->is_cut();
}

bool SceneInteractor::can_reload_selection_from_disk() const
{
    const ObjectSelection& selection = this->object_selection();
    if (selection.empty() || selection.contains_wipe_tower()) {
        return false;
    }

    const Project& project = m_workbench.project(m_selected_project_id);
    for (const ElementRef& element : selection.elements) {
        const ModelObject* object = project.find_object_by_id(element.object_id);
        if (object == nullptr || object->is_cut()) {
            return false;
        }
    }

    const ElementRefs volume_refs = selected_volumes();
    return std::ranges::any_of(
        volume_refs,
        [&project](const ElementRef& element)
        {
            const ModelVolume* volume =
                project.find_volume_by_id(element.object_id, element.volume_id);

            if (volume == nullptr) {
                return false;
            }

            return Algorithms::ModelVolume::is_reloadable_from_disk(*volume);
        }
    );
}

void SceneInteractor::invalidate_cut_info()
{
    const Biz::Scene::ObjectSelection& selection = object_selection();
    ASSERT(
        selection.mode == Slic3r::Biz::Scene::SelectionMode::Instance
        && selection.elements.size() == 1
        && !selection.contains_wipe_tower()
    );

    Domain::Project& project = m_workbench.project(m_selected_project_id);
    Domain::Model& model     = project.model();

    Domain::ModelObject* init_object =
        project.find_object_by_id(selection.elements.front().object_id);

    const Domain::CutId cut_id = init_object->cut_id;
    // invalidate cut for related objects (which have the same cut_id)
    for (Domain::ModelObject* object : model.objects) {
        if (object->cut_id.is_equal(cut_id)) {
            object->invalidate_cut();
        }
    }
}

std::optional<std::string> SceneInteractor::delete_selected_elements()
{
    Domain::Project& project               = m_workbench.project(m_selected_project_id);
    Domain::Model& model                   = project.model();
    const ObjectSelection& scene_selection = object_selection();
    ObjectSelection::ElementRefs to_remove = scene_selection.elements;

    for (const Domain::ElementRef& element : to_remove) {
        if (element.is_wipe_tower()) {
            return std::nullopt;
        }
    }

    std::optional<std::string> last_solid_part_name;
    ObjectSelection new_selection = {};

    BedTrackingChanges changes;

    // Remove selected elements from the model

    if (scene_selection.mode == SelectionMode::Instance) {
        // Delete selected instances from its objects
        for (const auto& el : to_remove) {
            Domain::ModelObject* object = project.find_object_by_id(el.object_id);
            for (size_t idx = object->instances.size() - 1; idx != size_t(-1); idx--) {
                if (object->instances[idx]->id().id == el.instance_id) {
                    remove_instance_from_bed(project, object->instances[idx], changes);
                    object->delete_instance(idx);
                    break;
                }
            }
        }

        // Identify objects with no associated instances.
        // Delete such objects if any are found.
        for (size_t idx = model.objects.size() - 1; idx != size_t(-1); idx--) {
            if (model.objects[idx]->instances.size() == 0) {
                model.delete_object(idx);
            }
        }

        // There is nothing to select

    } else if (scene_selection.mode == SelectionMode::Volume) {
        // Delete selected volumes from the object.
        Domain::ModelObject* object = project.find_object_by_id(to_remove.front().object_id);

        // We must track the number of "solid part" volumes in the object
        // and prevent deletion of the last remaining one.
        int solid_cnt = 0;
        for (auto vol : object->volumes)
            if (vol->is_model_part())
                ++solid_cnt;

        Domain::ElementRef last_solid_part_el;
        for (const auto& el : to_remove) {
            for (size_t idx = object->volumes.size() - 1; idx != size_t(-1); idx--) {
                const Domain::ModelVolume* volume = object->volumes[idx];
                if (volume->id().id == el.volume_id) {
                    if (volume->is_model_part()) {
                        if (solid_cnt == 1) {
                            // If the user attempts to delete the last solid part,
                            // store its name and related element in selection.
                            // And display an error dialog afterward.
                            last_solid_part_name = volume->name;
                            last_solid_part_el   = el;
                            continue;
                        }
                        --solid_cnt;
                    }
                    object->delete_volume(idx);
                    break;
                }
            }
        }

//        normalize_single_volume_object(*object);

        if (last_solid_part_name && last_solid_part_name.has_value()) {
            // Don't remove last_solid_part_el from the scene graph
            to_remove.erase(
                std::remove_if(
                    to_remove.begin(),
                    to_remove.end(),
                    [last_solid_part_el](const Domain::ElementRef& el)
                    { return el == last_solid_part_el; }
                ),
                to_remove.end()
            );
        }

        // Select the object from which volumes were deleted.
        new_selection.mode = SelectionMode::Instance;
        for (const Domain::ModelInstance* instance : object->instances) {
            new_selection.elements.emplace_back(Domain::ElementRef(object->id().id, instance->id().id));
        }
        changes = m_bed_tracking.update_instances_bed_placement(project, to_remove);
    }

    // Notify listeners on changes

    invoke_listeners<ISceneChangedListener>(
        [&](auto* l)
        {
            if (scene_selection.mode == SelectionMode::Instance) {
                l->on_instance_removed(m_selected_project_id, to_remove);
            } else if (scene_selection.mode == SelectionMode::Volume) {
                for (Domain::ElementRef& el : to_remove) {
                    // invalidate instance id to garanty volume removing from all instances
                    el.instance_id = 0;
                }
                l->on_volume_removed(m_selected_project_id, to_remove);
            }
        }
    );

    if (!changes.updated_instances.empty()) {
        invoke_listeners<ISceneChangedListener>(
            [&](ISceneChangedListener* l)
            {
                l->on_instances_last_bed_updated(
                    {changes.updated_instances.cbegin(), changes.updated_instances.cend()}
                );
            }
        );
    }

    set_object_selection(new_selection);

    for (const auto& bed_ref : changes.updated_beds)
        invoke_slicing_input_changed(bed_ref);

    return last_solid_part_name;
}

bool SceneInteractor::delete_object(Domain::ModelObject* object)
{
    // ToDo: Check if object cen be delete (for example if this object ia a part of cut set)
    // return false if delete is impossible

    Domain::Project& project = m_workbench.project(m_selected_project_id);
    Domain::Model& model     = project.model();

    ObjectSelection::ElementRefs to_remove;
    BedTrackingChanges changes;

    // Remove object instances from its object and scene
    for (auto instance : object->instances) {
        remove_instance_from_bed(project, instance, changes);
        to_remove.push_back({ object->id().id, instance->id().id });
    }
    // Remove object from the model
    model.delete_object(object);

    // Notify listeners on changes

    invoke_listeners<ISceneChangedListener>(
        [&](auto* l) { l->on_instance_removed(m_selected_project_id, to_remove); }
    );

    for (const auto& bed_ref : changes.updated_beds)
        invoke_slicing_input_changed(bed_ref);

    return true;
}

void SceneInteractor::prepare_added_project(Domain::SelectionId project_id)
{
    Domain::Project& project = m_workbench.project(project_id);
    m_bed_tracking.update_instances_bed_placement(project);
    for (auto& cc : project.config_containers()) {
        update_config_container_bed(project_id, cc->id().id);
        size_t index = 0;
        for (auto& bed_instance : cc->bed_instances()) {
            bed_instance->set_index(++index);
        }
    }

    notify_listener_on_objects(project);
    layout_after_project_load(project);
}

Domain::BedInstance& SceneInteractor::add_bed_instance(size_t config_container_id)
{
    SceneInteractorProjectContext& project_context{m_projects.find(m_selected_project_id)->second};
    auto& project               = project_context.project;
    Domain::ConfigContainer* cc = project.find_config_container(config_container_id);
    Domain::BedInstance& ret    = cc->add_bed_instance();
    ret.set_index(cc->bed_instances().size());

    auto changed_elements = m_bed_placement.layout(project, BED_GAP);

    // make copy
    Domain::ModelInstanceList unplaced = project.unplaced_model_instances();
    project.unplaced_model_instances().clear();
    auto changes = m_bed_tracking.update_instances_bed_placement(project, unplaced, false);
    const Domain::BedRef updated{cc->id().id, ret.id().id};
    changes.updated_beds.insert(updated);

    if (project_context.bed_selection.empty()) {
        project_context.bed_selection.select_one(
            Domain::BedRef{cc->id().id, ret.id().id},
            CameraActionOnBedSelection::CenterOnBed
        );
    }

    invoke_listeners<ISceneChangedListener>(
        [&](auto* l)
        {
            l->on_instance_transformed(
                m_selected_project_id,
                changed_elements,
                TransformState::Completed,
                changes
            );
        }
    );

    if (!changes.updated_instances.empty()) {
        invoke_listeners<ISceneChangedListener>(
            [&](ISceneChangedListener* l)
            {
                l->on_instances_last_bed_updated(
                    {changes.updated_instances.cbegin(), changes.updated_instances.cend()}
                );
            }
        );
    }

    for (const auto& bed_ref : changes.updated_beds)
        invoke_slicing_input_changed(bed_ref);

    invoke_listeners<ISceneBedInstanceChangedListener>(
        [&](auto* l) { l->on_bed_instance_updated(m_selected_project_id, {updated}); }
    );

    return ret;
}

void SceneInteractor::show_virtual_bed_preview(Domain::SelectionId config_container_id)
{
    auto it = m_projects.find(m_selected_project_id);
    if (it == m_projects.end())
        return;
    SceneInteractorProjectContext& ctx = it->second;

    std::optional<Domain::Transform3d> placement =
        m_bed_placement.next_bed_placement(ctx.project, config_container_id, BED_GAP);
    if (!placement) {
        hide_virtual_bed_preview();
        return;
    }

    ctx.virtual_bed_preview = VirtualBedPreview{config_container_id, *placement};

    invoke_listeners<ISceneBedInstanceChangedListener>(
        [&](auto* l) {
            l->on_virtual_bed_preview_changed(m_selected_project_id, ctx.virtual_bed_preview);
        }
    );
}

void SceneInteractor::hide_virtual_bed_preview()
{
    auto it = m_projects.find(m_selected_project_id);
    if (it == m_projects.end())
        return;
    SceneInteractorProjectContext& ctx = it->second;
    if (!ctx.virtual_bed_preview)
        return;

    ctx.virtual_bed_preview.reset();
    invoke_listeners<ISceneBedInstanceChangedListener>(
        [&](auto* l) {
            l->on_virtual_bed_preview_changed(m_selected_project_id, ctx.virtual_bed_preview);
        }
    );
}

const std::optional<VirtualBedPreview>& SceneInteractor::virtual_bed_preview() const
{
    static const std::optional<VirtualBedPreview> empty;
    auto it = m_projects.find(m_selected_project_id);
    if (it == m_projects.end())
        return empty;
    return it->second.virtual_bed_preview;
}

bool SceneInteractor::virtual_bed_preview_accepts_selection()
{
    auto it = m_projects.find(m_selected_project_id);
    if (it == m_projects.end())
        return false;
    const SceneInteractorProjectContext& ctx = it->second;
    if (!ctx.virtual_bed_preview)
        return false;

    const Domain::ConfigContainer* cc =
        ctx.project.find_config_container(ctx.virtual_bed_preview->config_container_id);
    if (!cc)
        return false;

    // Synthesize a bed instance at the preview transform; the containment test
    // uses its transformation offset against the CC's bed contour.
    Domain::BedInstance probe{cc->bed()};
    probe.transformation = Domain::Transformation(ctx.virtual_bed_preview->transform);

    for (const Domain::ElementRef& e : ctx.object_selection.elements) {
        if (!e.has_instance())
            continue;
        const Domain::ModelInstance* inst =
            ctx.project.find_instance_by_id(e.object_id, e.instance_id);
        if (inst == nullptr)
            continue;
        const auto state = m_bed_tracking.check_instance_containment_2d(
            ctx.project, *inst, cc->bed(), probe
        );
        if (state == Algorithms::Bed::BedContainmentState::Inside)
            return true;
    }
    return false;
}

void SceneInteractor::insert_bed_instance(
    Domain::SelectionId project_id,
    Domain::SelectionId config_container_id,
    std::size_t position,
    std::unique_ptr<BedInstance> bed_instance
)
{
    Domain::Project& project{m_projects.find(project_id)->second.project};
    Domain::ConfigContainer* config_container{project.find_config_container(config_container_id)};
    ASSERT(config_container);

    const Domain::BedRef bed_ref{config_container_id, bed_instance->id().id};

    ASSERT(position <= config_container->bed_instances().size());
    config_container->bed_instances().insert(
        config_container->bed_instances().begin() + position,
        std::move(bed_instance)
    );

    invoke_listeners<ISceneBedInstanceChangedListener>(
        [&](auto* l) { l->on_bed_instance_updated(project_id, {bed_ref}); }
    );
}

void SceneInteractor::clear_beds(Domain::Project& project)
{
    ASSERT(
        std::all_of(
            project.config_containers().begin(),
            project.config_containers().end(),
            [](const std::unique_ptr<Domain::ConfigContainer>& cc)
            { return !cc->bed_instances().empty(); }
        )
    );

    Domain::BedRefs bed_refs;

    for (const std::unique_ptr<Domain::ConfigContainer>& config_container :
         project.config_containers())
    {
        for (const auto& bed_instance : config_container->bed_instances()) {
            const size_t bed_id = bed_instance->id().id;
            bed_refs.push_back(
                BedRef{.config_container_id = config_container->id().id, .instance_id = bed_id}
            );
        }
    }

    invoke_listeners<ISceneBedInstanceChangedListener>(
        [&](auto* l) { l->on_bed_instance_removed(m_selected_project_id, bed_refs); }
    );
}

void SceneInteractor::layout_after_project_load(Domain::Project& added_project)
{
    ASSERT(std::all_of(added_project.config_containers().begin(), added_project.config_containers().end(),
           [](const std::unique_ptr<Domain::ConfigContainer>& cc){ return ! cc->bed_instances().empty(); }));
    m_bed_tracking.update_instances_bed_placement(added_project);
    auto updated = m_bed_placement.layout(added_project, BED_GAP);

    const auto& config_container = added_project.config_containers().front();
    bed_selection().select_one(
        Domain::BedRef(
            {config_container->id().id,
             config_container->bed_instances().front()->id().id}
        ),
        CameraActionOnBedSelection::CenterOnBed
    );
    auto changes = m_bed_tracking.update_instances_bed_placement(added_project);

    Domain::BedRefs bed_refs;
    for (auto& cc : added_project.config_containers()) {
        for (auto& bed_instance : cc->bed_instances())
            bed_refs.push_back({cc->id().id, bed_instance->id().id});
    }

    invoke_listeners<ISceneChangedListener>(
        [&](auto* l)
        {
            l->on_instance_transformed(
                m_selected_project_id,
                updated,
                TransformState::Completed,
                changes
            );
        }
    );

    if (!changes.updated_instances.empty()) {
        invoke_listeners<ISceneChangedListener>(
            [&](ISceneChangedListener* l)
            {
                l->on_instances_last_bed_updated(
                    {changes.updated_instances.cbegin(), changes.updated_instances.cend()}
                );
            }
        );
    }

    invoke_listeners<ISceneBedInstanceChangedListener>(
        [&](auto* l) { l->on_bed_instance_updated(m_selected_project_id, bed_refs); }
    );
}

void SceneInteractor::update_beds(
    Domain::SelectionId project_id,
    Domain::SelectionId config_container_id
)
{
    const Domain::ConfigContainer* config_container{
        m_workbench.project(project_id).find_config_container(config_container_id)
    };

    if (!config_container) {
        return;
    }

    Domain::BedRefs bed_refs{};
    for (const std::unique_ptr<BedInstance>& bed_instance : config_container->bed_instances()) {
        bed_refs.push_back(BedRef{config_container_id, bed_instance->id().id});
    }

    invoke_listeners<ISceneBedInstanceChangedListener>(
        [&](auto* l) { l->on_bed_instance_updated(project_id, bed_refs); }
    );

    for (const Domain::BedRef& bed_ref : bed_refs) {
        const Domain::SlicingId wipe_tower_id{project_id, bed_ref.instance_id};
        invoke_listeners<ISceneChangedListener>([&](auto listener)
                                                { listener->on_wipe_tower_moved(wipe_tower_id); });
    }
}

BedInstances SceneInteractor::selected_bed_instances() const
{
    BedInstances result;
    const Biz::Scene::BedSelection& selection{bed_selection()};

    auto it{m_projects.find(m_selected_project_id)};
    if (it == m_projects.end()) {
        return result;
    }
    Project& project{it->second.project};

    for (const Domain::BedRef& bed_ref : selection.selected_beds()) {
        const Domain::BedInstance* bed_instance{project.find_bed_instance_by_id(bed_ref.instance_id)};
        if (!bed_instance) {
            continue;
        }
        result.push_back(*bed_instance);
    }

    return result;
}

void
SceneInteractor::erase_bed_instance(Domain::SelectionId project_id, const Domain::BedRef& bed_ref)
{
    Domain::Project& project{m_projects.find(project_id)->second.project};
    Domain::ConfigContainer* config_container{
        project.find_config_container(bed_ref.config_container_id)
    };
    ASSERT(config_container);
    std::erase_if(
        config_container->bed_instances(),
        [&](const auto& bed_instance) { return bed_ref.instance_id == bed_instance->id().id; }
    );

    invoke_listeners<ISlicingInputChangedListener>(
        [&](auto* l) { l->on_slicing_input_removed(bed_ref); }
    );
    invoke_listeners<ISceneBedInstanceChangedListener>(
        [&](auto* l) { l->on_bed_instance_removed(m_selected_project_id, {bed_ref}); }
    );
}

void SceneInteractor::remove_bed_instance(const Domain::BedRef& instance, bool allow_to_remove_last_one)
{
    auto& project               = m_projects.find(m_selected_project_id)->second.project;
    Domain::ConfigContainer* cc = project.find_config_container(instance.config_container_id);
    ASSERT(cc != nullptr);
    auto* bed_inst = Domain::find_by_id(cc->bed_instances(), instance.instance_id);

    // model instances to be removed toghether with the bed instance containing them
    auto insts = bed_inst->model_instances;

    auto& selection = bed_selection();
    selection.remove(instance);
    cc->remove_bed_instance_by_id(instance.instance_id);

    // update bed_instance index
    size_t idx = 1;
    Domain::BedRefs updated;
    for (auto& inst : cc->bed_instances()) {
        auto index = idx++;
        if (index != inst->index()) {
            inst->set_index(index);
            updated.emplace_back(cc->id().id, inst->id().id);
        }
    }
    auto updated_instances = m_bed_placement.layout(project, BED_GAP);

    if (bed_selection().empty()) {
        if (allow_to_remove_last_one) {
            if (cc->bed_instances().empty()) {
                auto& ccs = project.config_containers();
                ASSERT(ccs.size() > 1);
                auto it = std::find_if(
                    ccs.begin(),
                    ccs.end(),
                    [config_container_id = instance.config_container_id](const auto& cc_ptr)
                    { return cc_ptr->id().id == config_container_id; }
                );
                if (++it == ccs.end())
                    it = ccs.begin();
                selection.select_one(
                    {it->get()->id().id, it->get()->bed_instances().front()->id().id},
                    CameraActionOnBedSelection::CenterOnBed
                );
            }
        } else {
            // ensure one bed instance is selected
            ASSERT(!cc->bed_instances().empty());
            selection.select_one(
                {cc->id().id, cc->bed_instances().front()->id().id},
                CameraActionOnBedSelection::CenterOnBed
            );
        }
    }

    invoke_listeners<ISceneBedInstanceChangedListener>(
        [&](auto* l) { l->on_bed_instance_updated(m_selected_project_id, updated); }
    );

    auto changes = m_bed_tracking.update_instances_bed_placement(project, updated_instances);
    for (const auto& bed_ref : changes.updated_beds)
        invoke_slicing_input_changed(bed_ref);

    invoke_listeners<ISceneChangedListener>(
        [&](auto* l)
        {
            l->on_instance_transformed(
                m_selected_project_id,
                updated_instances,
                TransformState::Completed,
                changes
            );
        }
    );

    invoke_listeners<ISlicingInputChangedListener>(
        [&](auto* l) { l->on_slicing_input_removed(instance); }
    );
    invoke_listeners<ISceneBedInstanceChangedListener>(
        [&](auto* l) { l->on_bed_instance_removed(m_selected_project_id, {instance}); }
    );

    if (!changes.updated_instances.empty()) {
        invoke_listeners<ISceneChangedListener>(
            [&](ISceneChangedListener* l)
            {
                l->on_instances_last_bed_updated(
                    {changes.updated_instances.cbegin(), changes.updated_instances.cend()}
                );
            }
        );
    }

    if (!allow_to_remove_last_one && cc->bed_instances().empty()) {
        add_bed_instance(cc->id().id);
    }

    // temporary object selection containing model instances to be removed
    ObjectSelection model_instances_to_remove;
    model_instances_to_remove.elements.reserve(insts.size());
    for (const auto inst : insts) {
        model_instances_to_remove.elements.emplace_back(Domain::ElementRef{ inst->get_object()->id().id, inst->id().id });
    }

    if (!model_instances_to_remove.empty()) {
        // removes the model instances from the current object selection
        ObjectSelection curr_scene_selection = object_selection();
        for (const auto& e : model_instances_to_remove.elements) {
            curr_scene_selection.remove(e);
        }

        // delete the model instances from the project
        set_object_selection(model_instances_to_remove);
        delete_selected_elements();
        // restore modified object selection
        set_object_selection(curr_scene_selection);
    }
}

void SceneInteractor::transform_bed_instance(const Domain::BedRef& instance, const Transform& xform)
{
    auto& proj                  = m_projects.find(m_selected_project_id)->second;
    Domain::ConfigContainer* cc = proj.project.find_config_container(instance.config_container_id);
    Domain::BedInstance& inst   = cc->find_bed_instance(instance.instance_id);
    inst.transformation         = Transformation{Transform3d{xform}};

    auto updated = inst.model_instances;
    inst.model_instances.clear();
    auto changes = m_bed_tracking.update_instances_bed_placement(proj.project, updated, false);
    for (const auto& bed_ref : changes.updated_beds)
        invoke_slicing_input_changed(bed_ref);

    invoke_listeners<ISceneBedInstanceChangedListener>(
        [&](auto* l)
        {
            l->on_bed_instance_transformed(m_selected_project_id, {instance}, TransformState::Completed);
        }
    );

    if (!changes.updated_instances.empty()) {
        invoke_listeners<ISceneChangedListener>(
            [&](ISceneChangedListener* l)
            {
                l->on_instances_last_bed_updated(
                    {changes.updated_instances.cbegin(), changes.updated_instances.cend()}
                );
            }
        );
    }
}

void SceneInteractor::update_config_container_bed(Domain::SelectionId project_id, Domain::SelectionId config_container_id)
{
    Domain::Project& project = m_workbench.project(project_id);
    Domain::ConfigContainer* config_container{project.find_config_container(config_container_id)};
    if (config_container == nullptr)
        return;

    Domain::Bed& bed{
        get_or_create_bed(
            project.bed_container(), *config_container, resources_dir(), project_id, config_container_id,
            [this](Domain::SelectionId project_id, Domain::SelectionId config_container_id) {
                return (m_preset_visual_getter != nullptr) ?
                    m_preset_visual_getter->system_preset_bed_shape(project_id, config_container_id) : Domain::Vec2ds();
            }
        )
    };

    const Domain::Bed* previous_bed{&config_container->bed()};
    if (previous_bed != &bed) {
        config_container->set_bed(bed);

        Domain::BedRefs bed_refs;
        Domain::ElementRefs changed_instances;
        for (auto& bed_instance : config_container->bed_instances()) {
            bed_instance->bed = bed;
            bed_refs.push_back({config_container->id().id, bed_instance->id().id});
            for (const auto* mi : bed_instance->model_instances) {
                changed_instances.emplace_back(mi->get_object()->id().id, mi->id().id);
            }
        }
        for (const auto* mi : project.unplaced_model_instances()) {
            changed_instances.emplace_back(mi->get_object()->id().id, mi->id().id);
        }
        auto updated = m_bed_placement.layout(project, BED_GAP);

        const auto it{std::ranges::find_if(
            project.config_containers(),
            [&](const auto& config_container) { return &config_container->bed() == previous_bed; }
        )};

        if (it == project.config_containers().end()) {
            project.bed_container().remove(previous_bed);
        }

        auto changes = m_bed_tracking.update_instances_bed_placement(project, changed_instances);


        invoke_listeners<ISceneChangedListener>(
            [&](auto* l)
            {
                l->on_instance_transformed(
                    project_id,
                    updated,
                    TransformState::Completed,
                    changes
                );
            }
        );

        invoke_listeners<ISceneBedInstanceChangedListener>(
            [&](auto* l) { l->on_bed_instance_updated(project_id, bed_refs); }
        );

        if (!changes.updated_instances.empty()) {
            invoke_listeners<ISceneChangedListener>(
                [&](ISceneChangedListener* l)
                {
                    l->on_instances_last_bed_updated(
                        {changes.updated_instances.cbegin(), changes.updated_instances.cend()}
                    );
                }
            );
        }
    }
}

void SceneInteractor::on_preset_selection_changed(Domain::SelectionId project_id, Domain::SelectionId config_container_id, Preset::PresetItemType type)
{
    if (type != Preset::PresetItemType::PrinterPreset) {
        return;
    }

    update_config_container_bed(project_id, config_container_id);

    // When one changes selection, layout() is called and it may reposition instances.
    update_selection_bounding_box();
}

static std::vector<Domain::SelectionId> get_bed_ids(
    const Domain::Workbench& workbench,
    Domain::SelectionId project_id,
    Domain::SelectionId config_container_id
)
{
    const Domain::Project* project{workbench.find_project_by_id(project_id)};
    if (!project) {
        return {};
    }
    const Domain::ConfigContainer* config_container{
        project->find_config_container(config_container_id)
    };
    if (!config_container) {
        return {};
    }
    std::vector<Domain::SelectionId> result;
    result.reserve(config_container->bed_instances().size());

    std::ranges::transform(
        config_container->bed_instances(),
        std::back_inserter(result),
        [](const std::unique_ptr<BedInstance>& instance) { return instance->id().id; }
    );
    return result;
}

void SceneInteractor::on_preset_value_changed(
    Domain::SelectionId project_id,
    Domain::SelectionId config_container_id,
    const Domain::ConfigItem& item
)
{
    const std::vector<std::string> bed_related_keys{
        "bed_shape",
        "max_print_height",
        "bed_custom_model",
        "bed_custom_texture",
    };
    if (std::ranges::find(bed_related_keys, item.def().name) != bed_related_keys.end()) {
        update_config_container_bed(project_id, config_container_id);
        for (const Domain::SelectionId& bed_id :
             get_bed_ids(m_workbench, project_id, config_container_id))
        {
            const WipeTowerGeometry* geometry{wipe_tower_geometry(bed_id)};
            if (!geometry) {
                continue;
            }
            resolve_wipe_tower_outside_bed(*geometry, {project_id, bed_id});
        }
    }
}

const Project& SceneInteractor::selected_project() const
{
    return m_projects.find(m_selected_project_id)->second.project;
}

const Project::ConfigContainerList& SceneInteractor::selected_project_config_containers() const
{
    return this->selected_project().config_containers();
}

const Domain::ModelInstanceList& SceneInteractor::unplaced_model_instances(const Domain::SelectionId project_id) const
{
    return m_projects.find(project_id)->second.project.unplaced_model_instances();
}

const Domain::ModelInstanceList& SceneInteractor::selected_project_unplaced_model_instances() const
{
    return unplaced_model_instances(m_selected_project_id);
}

const BedContainer::BedList& SceneInteractor::selected_project_beds() const
{
    const Project& project = this->selected_project();
    return project.bed_container().beds();
}

void SceneInteractor::set_element_transforms(const SceneInteractor::ElementTransforms& transforms)
{
    Domain::ElementRefs elements;

    Project& project{m_workbench.project(m_selected_project_id)};

    for (const auto& [element, transform] : transforms) {
        elements.push_back(element);
        if (element.has_volume()) {
            ASSERT(element.has_volume());
            ASSERT(!element.has_instance());
            Domain::ModelVolume* volume{
                project.find_volume_by_id(element.object_id, element.volume_id)
            };
            volume->set_transformation(Transformation{Transform3d{transform}});
        } else {
            ASSERT(!element.has_volume());
            ASSERT(element.has_instance());
            Domain::ModelInstance* instance{
                project.find_instance_by_id(element.object_id, element.instance_id)
            };
            instance->set_transformation(Transformation{Transform3d{transform}});
        }
    }

    update_selection_bounding_box();

    const BedTrackingChanges changes = update_elements_bed_placement(elements, true);
    invoke_listeners<ISceneChangedListener>(
        [&](ISceneChangedListener* l)
        {
            l->on_volume_transformed(
                m_selected_project_id,
                elements,
                TransformState::Completed,
                changes
            );
            l->on_instance_transformed(
                m_selected_project_id,
                elements,
                TransformState::Completed,
                changes
            );
        }
    );
    invoke_listeners<ISceneSelectionChangedListener>(
        [&](ISceneSelectionChangedListener* l)
        { l->on_scene_selection_transformed(m_selected_project_id, object_selection()); }
    );

    if (!changes.updated_instances.empty()) {
        invoke_listeners<ISceneChangedListener>(
            [&](ISceneChangedListener* l)
            {
                l->on_instances_last_bed_updated(
                    {changes.updated_instances.cbegin(), changes.updated_instances.cend()}
                );
            }
        );
    }
}

void SceneInteractor::transform_selection(
    const Transform& relative_transform,
    bool place_on_bed
)
{
    TransformMemento memento;
    transform_selection(relative_transform, memento, place_on_bed);
    finalize_transform_selection(memento, false);
}

void SceneInteractor::transform_selection(
    const SquareMatrix4d& relative_transform,
    TransformMemento& memento,
    bool place_on_bed
)
{
    ZoneScoped;

    DEBUG_ASSERT(fabs(relative_transform.determinant()) > 1e-9);
    auto& proj         = m_projects.find(m_selected_project_id)->second;
    bool instance_mode = proj.object_selection.mode == SelectionMode::Instance;
    auto selection     = proj.object_selection;

    if (instance_mode) {
        auto final_mode = transform_selection_instance_mode(proj, relative_transform, memento);
        if (final_mode == SelectionMode::Volume) {
            instance_mode  = false;
            selection.mode = SelectionMode::Volume;
            selection.elements.clear();
            for (const auto& e : memento.elements) {
                selection.elements.push_back(e.first);
            }
        }
    } else
        transform_selection_volume_mode(proj, relative_transform, memento);

    std::optional<SelectionExtents> bounding_box{get_selection_extents(
        m_selected_project_id,
        proj.object_selection,
        *this,
        m_workbench
    )};

    if (bounding_box
        && place_on_bed
        && proj.object_selection.state() != SelectionState::SingleVolume
        && bounding_box->is_floating()
        && selection.mode == SelectionMode::Volume)
    {
        ZoneScopedN("set_transform")
        for (const auto& e : selection.elements) {
            if (!e.has_instance() || !e.has_volume()) {
                continue;
            }
            const auto* instance{proj.project.find_instance_by_id(e.object_id, e.instance_id)};
            const auto instance_matrix{instance->get_matrix()};
            ModelVolume* volume = proj.project.find_volume_by_id(e.object_id, e.volume_id);
            if (!volume) {
                continue;
            }
            Domain::Transform3d transformation{Domain::Transform3d::Identity()};
            transformation.translate(Domain::Vec3d{0.0, 0.0, -bounding_box->min_z()});

            const Domain::Transform3d volume_relative_transform =
                instance_matrix.inverse() * transformation * instance_matrix;

            volume->set_transformation(
                Transformation{volume_relative_transform * volume->get_matrix()}
            );
        }
        bounding_box->reset_z();
    }

    update_selection_bounding_box(bounding_box);


    const BedTrackingChanges changes = update_elements_bed_placement(
        proj.object_selection.elements,
        selection.mode == SelectionMode::Volume || memento.forced_volume_mode,
        true
    );

    memento.changes.append(changes);

    {
        ZoneScopedN("invoke_listeners");
        for (const auto& [_, e] : memento.elements) {
            if (e.element.is_wipe_tower()) {
                invoke_listeners<ISceneChangedListener>(
                    [&](auto listener) { listener->on_wipe_tower_moved(e.element.wipe_tower_id); }
                );
            }
        }
        invoke_listeners<ISceneChangedListener>(
            [&](ISceneChangedListener* l)
            {
                if (instance_mode)
                    l->on_instance_transformed(m_selected_project_id, selection.elements, TransformState::InProgress, changes);
                else
                    l->on_volume_transformed(m_selected_project_id, selection.elements, TransformState::InProgress, changes);
            }
        );
        invoke_listeners<ISceneSelectionChangedListener>(
            [&](ISceneSelectionChangedListener* l)
            { l->on_scene_selection_transformed(m_selected_project_id, selection); }
        );

        if (!changes.updated_instances.empty()) {
            invoke_listeners<ISceneChangedListener>(
                [&](ISceneChangedListener* l)
                {
                    l->on_instances_last_bed_updated(
                        {changes.updated_instances.cbegin(), changes.updated_instances.cend()}
                    );
                }
            );
        }
    }
}

void SceneInteractor::transform_instances(const std::vector<Arrange::InstanceTransform2D>& transformations)
{
    Project& project{m_projects.find(m_selected_project_id)->second.project};

    std::vector<Domain::ElementRef> elements;
    for (const Arrange::InstanceTransform2D& trafo : transformations) {
        if (trafo.instance_ref.is_wipe_tower()) {
            const Domain::SlicingId wipe_tower_id{trafo.instance_ref.wipe_tower_id};
            Domain::BedInstance* bed_instance{
                project.find_bed_instance_by_id(wipe_tower_id.bed_instance_id)
            };
            if (!bed_instance) {
                continue;
            }

            const WipeTowerGeometry* geometry{
                wipe_tower_geometry(wipe_tower_id.bed_instance_id)
            };
            if (!geometry) {
                continue;
            }

            Domain::Transform3d transformation{Domain::Transform3d::Identity()};
            transformation.translate(
                Vec3d{trafo.absolute_offset.x(), trafo.absolute_offset.y(), 0.0}
            );
            transformation.rotate(
                Eigen::AngleAxisd(
                    trafo.rotation_delta + Slic3r::deg2rad(bed_instance->wipe_tower.rotation),
                    Eigen::Vector3d::UnitZ()
                )
            );

            set_wipe_tower_transformation(Domain::Transformation{transformation}, *bed_instance);
            invoke_listeners<ISceneChangedListener>(
                [&](auto listener) { listener->on_wipe_tower_moved(wipe_tower_id); }
            );
        } else {
            ModelInstance* instance{
                project.find_instance_by_id(trafo.instance_ref.object_id, trafo.instance_ref.instance_id)
            };
            if (instance == nullptr) {
                continue;
            }
            Domain::Transform3d offset_trafo{instance->get_transformation().get_matrix()};
            offset_trafo.translation().x() = trafo.absolute_offset.x();
            offset_trafo.translation().y() = trafo.absolute_offset.y();

            auto rotation_trafo{Domain::Transform3d::Identity()};
            rotation_trafo.rotate(Eigen::AngleAxisd(trafo.rotation_delta, Eigen::Vector3d::UnitZ()));

            instance->set_transformation(Transformation{offset_trafo * rotation_trafo});
        }
        elements.push_back(trafo.instance_ref);
    }

    update_selection_bounding_box();

    const BedTrackingChanges changes{update_elements_bed_placement(elements, false)};

    invoke_listeners<ISceneChangedListener>(
        [&](ISceneChangedListener* l)
        { l->on_instance_transformed(m_selected_project_id, elements, TransformState::Completed, changes); }
    );
}

static WipeTowerCollisionData get_wipe_tower_collision_data(
    const WipeTowerGeometry& wipe_tower_geometry,
    const BedInstance& bed_instance
)
{
    const Domain::ExPolygon footprint_scaled{
        wipe_tower_geometry.get_outline(bed_instance.wipe_tower)
    };

    const Domain::BedType bed_type{bed_instance.bed.get().type()};
    Vec2ds footprint_unscaled;
    if (bed_type == Domain::BedType::Rectangle
        || bed_type == Domain::BedType::Circle
        || bed_type == Domain::BedType::Convex)
    {
        const Polygon convex_hull{Algorithms::Geometry::convex_hull({footprint_scaled})};
        footprint_unscaled = Algorithms::Point::unscaled(convex_hull.points);
    } else {
        ASSERT(footprint_scaled.holes.empty());
        footprint_unscaled = Algorithms::Point::unscaled(footprint_scaled.contour.points);
    }

    const Vec2d position{
        bed_instance.wipe_tower.position + bed_instance.transformation.get_offset().head<2>()
    };
    BoundingBox2d bounding_box;
    Vec2ds footprint_transformed;
    footprint_transformed.reserve(footprint_unscaled.size());
    for (const Vec2d& point : footprint_unscaled) {
        const Vec2d transformed_point{point + position};
        footprint_transformed.push_back(transformed_point);
        bounding_box = Algorithms::BoundingBox::merge(bounding_box, transformed_point);
    }
    return {bounding_box, std::move(footprint_transformed)};
}

void SceneInteractor::finalize_transform_selection(
    TransformMemento& memento,
    bool canceled
)
{
    auto& proj = m_projects.find(m_selected_project_id)->second;

    std::vector<TransformMemento::Element> removed_wipe_towers;
    std::vector<TransformMemento::Element> wipe_towers_outside;
    for (const auto& [_, e] : memento.elements) {
        if (!e.element.is_wipe_tower()) {
            continue;
        }
        const Domain::BedInstance* bed_instance{
            proj.project.find_bed_instance_by_id(e.element.wipe_tower_id.bed_instance_id)
        };
        if (!bed_instance) {
            removed_wipe_towers.push_back(e);
            continue;
        }
        const auto it{proj.wipe_tower_geometries.find(e.element.wipe_tower_id.bed_instance_id)};
        if (it == proj.wipe_tower_geometries.end()){
            removed_wipe_towers.push_back(e);
            continue;
        }

        // Here the wipe tower geometry is not updated yet. The backend callback comes after this.
        // Make sure to check with the position and rotation after transformation.
        // Also, the wipe tower position and rotation are bed relative.
        const WipeTowerCollisionData collision_data =
            get_wipe_tower_collision_data(it->second, *bed_instance);

        // Check whether the wipe tower lies within the bed contour.
        const BedContainmentState containment_state{m_bed_tracking.check_containment_2d(
            bed_instance->bed.get(),
            *bed_instance,
            collision_data.bounding_box,
            collision_data.footprint_2d
        )};
        if (containment_state != BedContainmentState::Inside) {
            wipe_towers_outside.push_back(e);
        }
    }

    const bool vol_mode = memento.forced_volume_mode || proj.object_selection.mode == SelectionMode::Volume;

    auto selected_elements = proj.object_selection.elements;
    if (memento.forced_volume_mode) {
        selected_elements.clear();
        for (const auto& e : memento.elements)
            selected_elements.push_back(e.first);
    }

    std::vector<TransformMemento::Element> wipe_towers_to_restore;
    if (canceled) {
        for (const auto& [_, e] : memento.elements) {
            if (!e.element.is_wipe_tower()) {
                continue;
            }
            wipe_towers_to_restore.push_back(e);
        }
    } else {
        wipe_towers_to_restore.insert(
            wipe_towers_to_restore.end(),
            removed_wipe_towers.begin(),
            removed_wipe_towers.end()
        );
        wipe_towers_to_restore.insert(
            wipe_towers_to_restore.end(),
            wipe_towers_outside.begin(),
            wipe_towers_outside.end()
        );
    }

    for (const TransformMemento::Element& e : wipe_towers_to_restore) {
        const Transformation xform{Transform3d{e.original_xform}};
        Domain::BedInstance* bed_instance =
            proj.project.find_bed_instance_by_id(e.element.wipe_tower_id.bed_instance_id);
        ASSERT(bed_instance);
        set_wipe_tower_transformation(xform, *bed_instance);
        invoke_listeners<ISceneChangedListener>(
            [&](auto listener) { listener->on_wipe_tower_moved(e.element.wipe_tower_id); }
        );
    }

    for (const TransformMemento::Element& e : removed_wipe_towers) {
        proj.object_selection.remove(Domain::ElementRef{e.element.wipe_tower_id});
    }
    if (!removed_wipe_towers.empty()) {
        invoke_listeners<ISceneSelectionChangedListener>(
            [&](auto* l)
            { l->on_scene_selection_changed(m_selected_project_id, proj.object_selection); }
        );
    }

    BedTrackingChanges changes;
    if (canceled || !wipe_towers_outside.empty()) {
        for (const auto& [_, e] : memento.elements) {
            const Transformation xform{Transform3d{e.original_xform}};
            if (e.element.is_wipe_tower()) {
                continue;
            }
            if (vol_mode) {
                auto* vol = proj.project.find_volume_by_id(e.element.object_id, e.element.volume_id);
                vol->set_transformation(xform);
            } else {
                auto* inst = proj.project.find_instance_by_id(e.element.object_id, e.element.instance_id);
                inst->set_transformation(xform);
            }
        }

        changes = update_elements_bed_placement(proj.object_selection.elements, vol_mode, false);
    } else if (!canceled) {
        for (const auto& bed_ref : memento.changes.updated_beds) {
            invoke_slicing_input_changed(bed_ref);
        }
    }

    update_selection_bounding_box();

    invoke_listeners<ISceneChangedListener>(
        [&](ISceneChangedListener* l)
        {
            if (vol_mode)
                l->on_volume_transformed(m_selected_project_id, selected_elements, canceled ? TransformState::Canceled : TransformState::Completed, changes);
            else
                l->on_instance_transformed(m_selected_project_id, selected_elements, canceled ? TransformState::Canceled : TransformState::Completed, changes);
        }
    );

    if (!canceled && !memento.elements.empty()) {
        invoke_listeners<ISceneSelectionChangedListener>(
            [&](ISceneSelectionChangedListener* l)
            { l->on_scene_selection_transformed(m_selected_project_id, proj.object_selection); }
        );
    }

    if (!changes.updated_instances.empty()) {
        invoke_listeners<ISceneChangedListener>(
            [&](ISceneChangedListener* l)
            {
                l->on_instances_last_bed_updated(
                    {changes.updated_instances.cbegin(), changes.updated_instances.cend()}
                );
            }
        );
    }

    memento.reset();
}

void SceneInteractor::on_removed_config_container(Domain::Project& project)
{
    layout_after_project_load(project);
}

void SceneInteractor::on_wipe_tower_geometry_changed(
    Slicing::OptWipeTowerGeometry wipe_tower,
    const Domain::SlicingId slicing_id
)
{
    if (m_workbench.find_project_by_id(slicing_id.project_id) == nullptr) {
        // The project may have been deleted before the queued callback was executed.
        return;
    }
    if (!wipe_tower) {
        remove_wipe_tower(slicing_id);
        return;
    }
    change_wipe_tower(*wipe_tower, slicing_id);
}

void SceneInteractor::remove_wipe_tower(const Domain::SlicingId slicing_id)
{
    const auto& project_it{m_projects.find(slicing_id.project_id)};
    if (project_it == m_projects.end()) {
        return;
    }
    SceneInteractorProjectContext& context{project_it->second};
    context.wipe_tower_geometries.erase(slicing_id.bed_instance_id);

    if (object_selection().is_selected(Domain::ElementRef{slicing_id})) {
        update_selection_bounding_box();
    }
    invoke_listeners<ISceneChangedListener>([&](auto listener)
                                            { listener->on_wipe_tower_removed(slicing_id); });
}

void SceneInteractor::change_wipe_tower(
    const WipeTowerGeometry& wipe_tower,
    const Domain::SlicingId slicing_id
)
{
    const auto& project_it{m_projects.find(m_selected_project_id)};
    if (project_it == m_projects.end()) {
        return;
    }
    SceneInteractorProjectContext& context{project_it->second};
    const bool was_present{context.wipe_tower_geometries.contains(slicing_id.bed_instance_id)};
    context.wipe_tower_geometries[slicing_id.bed_instance_id] = wipe_tower;

    // Recheck bed containment, the changed geometry (brim, cone) may have resized the footprint.
    const ElementRefs wipe_tower_element{ElementRef{slicing_id}};
    BedTrackingChanges changes;
    this->update_wipe_tower_containment(context, wipe_tower_element, changes);

    if (object_selection().is_selected(Domain::ElementRef{slicing_id})) {
        update_selection_bounding_box();
    }

    if (!was_present) {
        resolve_wipe_tower_outside_bed(wipe_tower, slicing_id);
    }

    invoke_listeners<ISceneChangedListener>(
        [&](auto listener) { listener->on_wipe_tower_changed(slicing_id, wipe_tower); }
    );
}

void SceneInteractor::resolve_wipe_tower_outside_bed(
    const WipeTowerGeometry& wipe_tower,
    Domain::SlicingId slicing_id
)
{
    const auto& project_it{m_projects.find(m_selected_project_id)};
    if (project_it == m_projects.end()) {
        return;
    }
    SceneInteractorProjectContext& context{project_it->second};
    BedInstance* bed_instance{context.project.find_bed_instance_by_id(slicing_id.bed_instance_id)};
    if (!bed_instance) {
        return;
    }

    const WipeTowerCollisionData collision_data{
        get_wipe_tower_collision_data(wipe_tower, *bed_instance)
    };
    const BedContainmentState containment_state{m_bed_tracking.check_containment_2d(
        bed_instance->bed.get(),
        *bed_instance,
        collision_data.bounding_box,
        collision_data.footprint_2d
    )};

    if (containment_state == BedContainmentState::Inside) {
        return;
    }

    const Domain::Vec2d wipe_tower_center{wipe_tower.get_center(bed_instance->wipe_tower)};
    const Domain::Vec2d bed_center{bed_instance->bed.get().center()};
    const Domain::Vec2d offset_to_center{bed_center - wipe_tower_center};
    const Domain::Vec2d wipe_tower_position{bed_instance->wipe_tower.position + offset_to_center};

    Domain::Transform3d transformation{bed_instance->transformation.get_matrix()};
    transformation.translate(
        Domain::Vec3d{wipe_tower_position.x(), wipe_tower_position.y(), 0.0}
    );
    transformation.rotate(
        Eigen::AngleAxisd(
            Slic3r::deg2rad(bed_instance->wipe_tower.rotation),
            Eigen::Vector3d::UnitZ()
        )
    );
    set_wipe_tower_transformation(Domain::Transformation{transformation}, *bed_instance);
    bed_instance->wipe_tower_is_outside = false;
    invoke_listeners<ISceneChangedListener>([&](auto listener)
                                            { listener->on_wipe_tower_moved(slicing_id); });
}

void SceneInteractor::update_custom_gcode(
    const Domain::SlicingId slicing_id,
    const Domain::CustomGCode::Info& custom_gcode
)
{
    auto it{m_projects.find(slicing_id.project_id)};
    if (it == m_projects.end()) {
        return;
    }
    SceneInteractorProjectContext& project{it->second};

    Domain::BedInstance* bed_instance{
        project.project.find_bed_instance_by_id(slicing_id.bed_instance_id)
    };
    if (bed_instance == nullptr) {
        return;
    }
    bed_instance->custom_gcode = custom_gcode;

    const Domain::ConfigContainer* config_container{
        project.project.find_config_container_by_bed_instance_id(slicing_id.bed_instance_id)
    };
    if (config_container == nullptr) {
        return;
    }
    invoke_slicing_input_changed(Domain::BedRef{config_container->id().id, bed_instance->id().id});
}

void SceneInteractor::update_selection_bounding_box(
    const std::optional<SelectionExtents>& bounding_box
)
{
    ZoneScoped;

    const auto& project_it{m_projects.find(m_selected_project_id)};
    if (project_it == m_projects.end()) {
        return;
    }
    project_it->second.object_selection_bounding_box = std::nullopt;
    if (!project_it->second.object_selection.empty() && !bounding_box) {
        project_it->second.object_selection_bounding_box = Scene::get_selection_extents(
            m_selected_project_id,
            project_it->second.object_selection,
            *this,
            m_workbench
        );
    } else if (bounding_box) {
        project_it->second.object_selection_bounding_box = bounding_box;
    }
    invoke_listeners<ISceneSelectionChangedListener>(
        [&](auto* l)
        {
            l->on_scene_selection_bounding_box_updated(
                m_selected_project_id,
                project_it->second.object_selection
            );
        }
    );
}

const std::optional<SelectionExtents> SceneInteractor::selection_bounding_box() const
{
    const auto& project_it{m_projects.find(m_selected_project_id)};
    if (project_it == m_projects.end()) {
        return std::nullopt;
    }
    return project_it->second.object_selection_bounding_box;
}

const WipeTowerGeometry* SceneInteractor::wipe_tower_geometry(
    std::size_t bed_instance_id
) const
{
    const auto& project_it{m_projects.find(m_selected_project_id)};
    if (project_it == m_projects.end()) {
        return nullptr;
    }
    const auto it{project_it->second.wipe_tower_geometries.find(bed_instance_id)};
    if (it == project_it->second.wipe_tower_geometries.end()) {
        return nullptr;
    }
    return &it->second;
}

void SceneInteractor::on_extruder_candidates_changed(
    std::vector<unsigned> extruder_candidates,
    const Domain::SlicingId slicing_id
)
{
    auto it{m_projects.find(slicing_id.project_id)};
    if (it == m_projects.end()) {
        return;
    }
    SceneInteractorProjectContext& project{it->second};

    const Domain::ConfigContainer* config_container{
        project.project.find_config_container_by_bed_instance_id(slicing_id.bed_instance_id)
    };
    if (config_container == nullptr) {
        return;
    }
    Domain::BedInstance* bed_instance{
        project.project.find_bed_instance_by_id(slicing_id.bed_instance_id)
    };
    if (bed_instance == nullptr) {
        return;
    }

    bed_instance->extruder_candidates = std::move(extruder_candidates);
    invoke_listeners<ISceneBedInstanceChangedListener>(
        [&](auto* l)
        {
            l->on_bed_instance_extruder_candidates_changed(
                slicing_id.project_id,
                Domain::BedRef{config_container->id().id, slicing_id.bed_instance_id},
                bed_instance->extruder_candidates
            );
        }
    );
}

static std::set<Domain::ElementRef> get_instance_refs(const Model& model)
{
    std::set<Domain::ElementRef> result;
    for (const Domain::ModelObject* object : model.objects) {
        for (const Domain::ModelInstance* instance : object->instances) {
            result.insert(Domain::ElementRef{object->id().id, instance->id().id});
        }
    }
    return result;
}

static std::set<Domain::ElementRef> get_volume_refs(const Model& model)
{
    std::set<Domain::ElementRef> result;
    for (const Domain::ModelObject* object : model.objects) {
        for (const Domain::ModelInstance* instance : object->instances) {
            for (const Domain::ModelVolume* volume : object->volumes) {
                result.insert(
                    Domain::ElementRef{object->id().id, instance->id().id, volume->id().id}
                );
            }
        }
    }
    return result;
}

std::vector<Domain::ElementRef>& keep_only_unique(std::vector<Domain::ElementRef>& refs)
{
    std::ranges::sort(refs);
    refs.erase(std::ranges::unique(refs).begin(), refs.end());
    return refs;
}

std::vector<Domain::ElementRef>& clear_instances(std::vector<Domain::ElementRef>& refs)
{
    for (Domain::ElementRef& ref : refs) {
        ref.instance_id = 0;
    }
    return refs;
}

void SceneInteractor::set_state(
    Domain::SelectionId project_id,
    Domain::Model model,
    ObjectSelection object_selection
)
{
    const auto& project_it{m_projects.find(project_id)};
    if (project_it == m_projects.end()) {
        return;
    }

    Domain::Model& active_model{project_it->second.project.model()};
    const std::set<Domain::ElementRef> old_volumes{get_volume_refs(active_model)};
    const std::set<Domain::ElementRef> old_instances{get_instance_refs(active_model)};

    active_model = std::move(model);

    const std::set<Domain::ElementRef> new_volumes{get_volume_refs(active_model)};
    const std::set<Domain::ElementRef> new_instances{get_instance_refs(active_model)};

    Utils::SetDiff<ElementRef> volumes_diff{Utils::get_sets_diff(old_volumes, new_volumes)};
    const Utils::SetDiff<ElementRef> instances_diff{
        Utils::get_sets_diff(old_instances, new_instances)
    };

    const std::set<Domain::ElementRef> removed_instances{
        instances_diff.removed.begin(),
        instances_diff.removed.end()
    };
    const std::set<Domain::ElementRef> added_instances{
        instances_diff.added.begin(),
        instances_diff.added.end()
    };

    std::erase_if(
        volumes_diff.removed,
        [&](const Domain::ElementRef& volume_ref)
        {
            return removed_instances.contains(
                Domain::ElementRef{volume_ref.object_id, volume_ref.instance_id}
            );
        }
    );
    std::erase_if(
        volumes_diff.added,
        [&](const Domain::ElementRef& volume_ref)
        {
            return added_instances.contains(
                Domain::ElementRef{volume_ref.object_id, volume_ref.instance_id}
            );
        }
    );

    keep_only_unique(clear_instances(volumes_diff.added));
    keep_only_unique(clear_instances(volumes_diff.removed));
    keep_only_unique(clear_instances(volumes_diff.changed));

    const auto changes{m_bed_tracking.update_instances_bed_placement(project_it->second.project)};
    invoke_listeners<ISceneChangedListener>(
        [&](ISceneChangedListener* l)
        {
            if (!volumes_diff.removed.empty()) {
                l->on_volume_removed(m_selected_project_id, volumes_diff.removed);
            }
            if (!instances_diff.removed.empty()) {
                l->on_instance_removed(m_selected_project_id, instances_diff.removed);
            }

            if (!instances_diff.added.empty()) {
                l->on_instance_added(m_selected_project_id, instances_diff.added);
            }
            if (!volumes_diff.added.empty()) {
                l->on_volume_added(m_selected_project_id, volumes_diff.added);
            }

            if (!instances_diff.changed.empty()) {
                l->on_instance_transformed(
                    m_selected_project_id,
                    instances_diff.changed,
                    TransformState::Completed,
                    changes
                );
            }
            if (!volumes_diff.changed.empty()) {
                l->on_volume_transformed(
                    m_selected_project_id,
                    volumes_diff.changed,
                    TransformState::Completed,
                    changes
                );
                l->on_volume_type_changed(
                    m_selected_project_id,
                    volumes_diff.changed
                );
            }
        }
    );

    set_object_selection(object_selection);

    invoke_listeners<ISceneChangedListener>([&](ISceneChangedListener* l)
                                            { l->on_model_reloaded(m_selected_project_id); });

    for (const std::unique_ptr<Slic3r::Domain::ConfigContainer>& config_container :
         project_it->second.project.config_containers())
    {
        for (const auto& bed_instance : config_container->bed_instances()) {
            invoke_slicing_input_changed(BedRef{config_container->id().id, bed_instance->id().id});
        }
    }
}

BedTrackingChanges SceneInteractor::update_elements_bed_placement(
    const Domain::ElementRefs& elements,
    bool volume_mode,
    bool postpone_slicing_invalidation
)
{
    ZoneScoped;

    BedTrackingChanges changes;
    auto& proj          = m_projects.find(m_selected_project_id)->second;
    if (volume_mode) {
        std::set<size_t> object_ids;
        Domain::ElementRefs wipe_tower_refs;
        for (const auto& e : elements) {
            if (e.is_wipe_tower()) {
                wipe_tower_refs.push_back(e);
            } else {
                object_ids.insert(e.object_id);
            }
        }
        for (size_t obj_id : object_ids) {
            changes.append(
                m_bed_tracking.update_instances_bed_placement(proj.project, proj.project.find_object_by_id(obj_id)->instances)
            );
        }
        changes.append(
            m_bed_tracking.update_instances_bed_placement(proj.project, wipe_tower_refs)
        );
    } else {
        changes = m_bed_tracking.update_instances_bed_placement(proj.project, elements);
    }

    this->update_wipe_tower_containment(proj, elements, changes);

    if (!postpone_slicing_invalidation) {
        for (const BedRef& bed_ref : changes.updated_beds) {
            invoke_slicing_input_changed(bed_ref);
        }
    }

    return changes;
}

void SceneInteractor::update_wipe_tower_containment(
    SceneInteractorProjectContext& project_context,
    const ElementRefs& elements,
    BedTrackingChanges& changes
)
{
    ZoneScoped;

    for (const ElementRef& element : elements) {
        if (!element.is_wipe_tower()) {
            continue;
        }

        BedInstance* bed_instance =
            project_context.project.find_bed_instance_by_id(element.wipe_tower_id.bed_instance_id);
        if (bed_instance == nullptr) {
            continue;
        }

        const auto geometry_it =
            project_context.wipe_tower_geometries.find(element.wipe_tower_id.bed_instance_id);
        if (geometry_it == project_context.wipe_tower_geometries.end()) {
            continue;
        }

        const WipeTowerCollisionData collision_data =
            get_wipe_tower_collision_data(geometry_it->second, *bed_instance);
        const BedContainmentState containment_state = m_bed_tracking.check_containment_2d(
            bed_instance->bed.get(),
            *bed_instance,
            collision_data.bounding_box,
            collision_data.footprint_2d
        );
        const bool is_outside = (containment_state != BedContainmentState::Inside);
        if (bed_instance->wipe_tower_is_outside != is_outside) {
            bed_instance->wipe_tower_is_outside = is_outside;
            // Signal that colliding state changed so PlaterScenePresenter triggers material refresh.
            ++changes.colliding_instances_updated_count;
        }
    }
}

void SceneInteractor::normalize_single_volume_object(Domain::ModelObject& object)
{
    if (object.volumes.size() != 1)
        return;
    
    // if the object constains only one volume
    // the volume transform is collapsed into the instance transforms

    Domain::ModelVolume* vol = object.volumes.front();
    Domain::Transform3d vol_trafo = vol->get_transformation().get_matrix();
    Domain::ElementRefs instance_refs;
    instance_refs.reserve(object.instances.size());
    Domain::ElementRefs volume_refs;
    volume_refs.reserve(object.instances.size());
    for (Domain::ModelInstance* inst : object.instances) {
        inst->set_transformation(Domain::Transformation(inst->get_transformation().get_matrix() * vol_trafo));
        instance_refs.emplace_back(object.id().id, inst->id().id);
        volume_refs.emplace_back(object.id().id, inst->id().id, vol->id().id);
    }
    vol->set_transformation(Domain::Transformation(Domain::Transform3d::Identity()));
    const auto changes = m_bed_tracking.update_instances_bed_placement(m_workbench.project(m_selected_project_id), instance_refs);

    invoke_listeners<ISceneChangedListener>(
        [&](ISceneChangedListener* l) {
            l->on_instance_transformed(m_selected_project_id, instance_refs, TransformState::Completed, changes);
            l->on_volume_transformed(m_selected_project_id, volume_refs, TransformState::Completed, changes);
        }
    );

    if (!changes.updated_instances.empty()) {
        invoke_listeners<ISceneChangedListener>(
            [&](ISceneChangedListener* l)
            {
                l->on_instances_last_bed_updated(
                    {changes.updated_instances.cbegin(), changes.updated_instances.cend()}
                );
            }
        );
    }
}

void SceneInteractor::invoke_slicing_input_changed(const Domain::BedRef& bed_instance)
{
    ZoneScoped;
    invoke_listeners<ISlicingInputChangedListener>(
        [&](auto listener) { listener->on_slicing_input_changed(bed_instance); }
    );
}

ElementRefs
SceneInteractor::add_object_to_active_bed(const indexed_triangle_set& its, const std::string& name)
{
    const auto& project = m_workbench.project(m_selected_project_id);
    Domain::BedRef selected_bed = bed_selection().last_selected_bed();
    const Domain::ConfigContainer* cc =
        project.find_config_container(selected_bed.config_container_id);
    const Domain::BedInstance& inst = cc->find_bed_instance(selected_bed.instance_id);

    const Domain::Vec2d& bed_center =
        cc->bed().center() + Biz::Algorithms::Point::to_2d(inst.transformation.get_offset());

    Domain::TriangleMeshStats mesh_stats;
    {
        Domain::BoundingBox3d bbox = Domain::bounding_box(its);
        mesh_stats.min = bbox.min.cast<float>();
        mesh_stats.max = bbox.max.cast<float>();
        mesh_stats.size = mesh_stats.max - mesh_stats.min;
    }

    auto mesh = Domain::TriangleMesh(its, mesh_stats);

    Biz::Config::BedShape bed_shape(cc->bed().contour());
    mesh.scale(std::max(10., std::round(0.1 * bed_shape.get_size().x())));
    Domain::BoundingBox3d bbox = mesh.bounding_box();

    ElementRefs new_instances = new_object_from_mesh(std::move(mesh), name);

    if (bbox.defined) {
        using namespace Biz::Algorithms::BoundingBox;
        const Vec3d bb_center{center(bbox)};

        // Transform in volume mode.
        Domain::Transform3d z_transform = Domain::Transform3d::Identity();
        z_transform.translate(Domain::Vec3d(0., 0., sizes(bbox).z() * 0.5 - bb_center.z()));
        transform_selection(z_transform.matrix());

        // Transform in instance mode.
        Domain::Transform3d xy_transform = Domain::Transform3d::Identity();
        xy_transform.translate(
            Domain::Vec3d{bed_center.x() - bb_center.x(), bed_center.y() - bb_center.y(), 0.});
        transform_selection(xy_transform.matrix());
    }

    return new_instances;
}

void SceneInteractor::add_volume_to_active_object(
    const indexed_triangle_set& its,
    Domain::ModelVolumeType volume_type,
    const std::string& name
)
{
    const Biz::Scene::ObjectSelection& selection = object_selection();
    ASSERT(selection.mode == Biz::Scene::SelectionMode::Instance && selection.only_single_object());
    const Domain::ElementRef& element = selection.elements.front();

    const auto& project = m_workbench.project(m_selected_project_id);
    const Domain::ModelInstance* instance =
        project.find_instance_by_id(element.object_id, element.instance_id);

    using namespace Biz::Algorithms;
    Domain::BoundingBox3d inst_bbox =
        ModelObject::instance_bounding_box(*instance->get_object(), *instance, true);

    const Domain::ConfigContainer* cc =
        project.find_config_container(bed_selection().last_selected_bed().config_container_id);
    Biz::Config::BedShape bed_shape(cc->bed().contour());
    float scale = static_cast<float>(std::max(10., std::round(0.1 * bed_shape.get_size().x())));

    Domain::TriangleMeshStats mesh_stats;
    {
        Domain::BoundingBox3d bbox = Domain::bounding_box(its);
        mesh_stats.min             = bbox.min.cast<float>();
        mesh_stats.max             = bbox.max.cast<float>();
        mesh_stats.size            = mesh_stats.max - mesh_stats.min;
    }

    auto mesh = Domain::TriangleMesh(its, mesh_stats);
    mesh.scale(scale);
    Domain::Transform3d xform  = Domain::Transform3d::Identity();
    Domain::BoundingBox3d bbox = mesh.bounding_box();
    if (bbox.defined) {
        xform.translate(-BoundingBox::center(bbox));
        xform.translate(
            Domain::Vec3d(inst_bbox.max.x(), inst_bbox.max.y(), BoundingBox::sizes(bbox).z() * 0.5)
        );
    }

    add_volume_from_mesh(std::move(mesh), volume_type, name, xform.matrix());
}

} // namespace Slic3r::Biz::Scene
