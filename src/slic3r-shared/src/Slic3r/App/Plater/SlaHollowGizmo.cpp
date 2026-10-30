#include "Slic3r/App/Plater/SlaHollowGizmo.hpp"

#include "Slic3r/App/Plater/SlaHollowDialog.hpp"
#include "Slic3r/App/Render/GeometryBuilder.hpp"
#include "Slic3r/App/Scene/TriangleMeshManager.hpp"
#include "Slic3r/Assert.hpp"
#include "Slic3r/Biz/Algorithms/SlaPoints.hpp"
#include "Slic3r/Biz/Algorithms/TriangleMesh.hpp"
#include "Slic3r/Biz/I18N/I18N.hpp"
#include "Slic3r/Biz/ObjectSettingsInteractor.hpp"
#include "Slic3r/Biz/Preset/PresetInteractor.hpp"
#include "Slic3r/Biz/ProjectInteractor.hpp"
#include "Slic3r/Domain/Config.hpp"
#include "Slic3r/Domain/ModelObject.hpp"

#include <fmt/format.h>

#include <algorithm>

using Slic3r::Biz::_u8L;
using Slic3r::Biz::UndoSnapshotType;
using Slic3r::Domain::ColorRGBA;
using Slic3r::Domain::ModelObject;
using Slic3r::Domain::Transform3d;
using Slic3r::Domain::Vec3d;
using Slic3r::Domain::SLA::DrainHole;
using Slic3r::Domain::SLA::DrainHoles;

namespace SlaPoints = Slic3r::Biz::Algorithms::SlaPoints;
namespace TriMesh = Slic3r::Biz::Algorithms::TriangleMesh;

namespace Slic3r::App::Plater {

static const ColorRGBA HoleColor{0.9f, 0.9f, 0.9f, 1.f};

static constexpr const char* HollowingEnableKey   = "hollowing_enable";
static constexpr const char* WallThicknessKey     = "hollowing_min_thickness";
static constexpr const char* AccuracyKey          = "hollowing_quality";
static constexpr const char* ClosingDistanceKey   = "hollowing_closing_distance";

SlaHollowGizmo::SlaHollowGizmo(
    Render::Device& device,
    Biz::ProjectInteractor& project_interactor,
    PlaterScenePresenter& scene_presenter
) :
    SlaGizmoBase(device, project_interactor, scene_presenter)
{
    m_dialog     = std::make_unique<SlaHollowDialog>();
    m_dialog_ptr = m_dialog.get();
    m_dialog->callbacks().hollowing_enabled_changed = [this](bool enabled)
    { this->set_hollowing_option(HollowingEnableKey, Domain::ConfigValue{enabled}); };
    m_dialog->callbacks().wall_thickness_changed = [this](double value)
    { this->set_hollowing_option(WallThicknessKey, Domain::ConfigValue{value}); };
    m_dialog->callbacks().accuracy_changed = [this](double value)
    { this->set_hollowing_option(AccuracyKey, Domain::ConfigValue{value}); };
    m_dialog->callbacks().closing_distance_changed = [this](double value)
    { this->set_hollowing_option(ClosingDistanceKey, Domain::ConfigValue{value}); };
    m_dialog->callbacks().hole_diameter_changed = [this](double diameter)
    { this->set_hole_size(diameter, m_hole_depth); };
    m_dialog->callbacks().hole_depth_changed = [this](double depth)
    { this->set_hole_size(m_hole_diameter, depth); };
    m_dialog->callbacks().remove_all = [this]() { this->remove_all(); };
    m_dialog->callbacks().apply      = [this]() { this->apply(); };
    m_dialog->callbacks().clipping_of_view_value_changed = [this](double value)
    { this->set_clipping_position(value); };
    m_dialog->callbacks().clipping_of_view_reset_direction = [this]()
    { this->reset_clipping_direction(); };
}

SlaHollowGizmo::~SlaHollowGizmo() = default;

Scene::ToolType SlaHollowGizmo::type() const
{
    return Scene::ToolType::SlaHollowGizmo;
}

GizmoWindowPtr SlaHollowGizmo::release_ui_window()
{
    return std::move(m_dialog);
}

std::optional<Undo::ToolState> SlaHollowGizmo::get_tool_state() const
{
    // Only while active: undo can't restore the state of a tool of another printer technology.
    if (!this->active()) {
        return std::nullopt;
    }
    // Applied holes are in the model, only the unapplied ones are the gizmo's own state.
    return Undo::SlaHollowGizmoState{
        this->object_id(),
        this->dirty() ? m_holes : std::vector<Hole>{},
        this->dirty()
    };
}

void SlaHollowGizmo::set_tool_state(const Undo::ToolState& tool_state)
{
    ASSERT(std::holds_alternative<Undo::SlaHollowGizmoState>(tool_state));
    const auto& state = std::get<Undo::SlaHollowGizmoState>(tool_state);
    if (!this->active() || state.object_id != this->object_id()) {
        return;
    }

    if (state.dirty) {
        m_holes = state.holes;
    }
    this->restore_items(state.dirty);
}

size_t SlaHollowGizmo::item_count() const
{
    return m_holes.size();
}

bool SlaHollowGizmo::item_selected(const size_t index) const
{
    return m_holes[index].selected;
}

void SlaHollowGizmo::set_item_selected(const size_t index, const bool selected)
{
    m_holes[index].selected = selected;
}

Vec3d SlaHollowGizmo::item_position(const size_t index) const
{
    return m_holes[index].hole.pos.cast<double>();
}

Vec3d SlaHollowGizmo::item_out_direction(const size_t index) const
{
    // The normal of a drain hole points into the object.
    return this->model_direction_to_world(-m_holes[index].hole.normal.cast<double>());
}

void SlaHollowGizmo::add_item(const MeshHit& hit)
{
    // The backend transforms the normal as a direction, so it is stored that way.
    m_holes.push_back(Hole{
        DrainHole{
            hit.position.cast<float>(),
            (-this->print_direction_to_model(hit.print_normal)).cast<float>(),
            float(m_hole_diameter / 2.),
            float(m_hole_depth)
        },
        false
    });
}

bool SlaHollowGizmo::move_item(const size_t index, const MeshHit& hit)
{
    DrainHole& hole = m_holes[index].hole;
    hole.pos        = hit.position.cast<float>();
    hole.normal     = (-this->print_direction_to_model(hit.print_normal)).cast<float>();
    return true;
}

size_t SlaHollowGizmo::erase_selected_items(bool)
{
    return std::erase_if(m_holes, [](const Hole& hole) { return hole.selected; });
}

void SlaHollowGizmo::clear_items()
{
    m_holes.clear();
}

void SlaHollowGizmo::load_items(const Biz::Slicing::Sla::Object*)
{
    m_holes.clear();
    if (const ModelObject* mo = this->model_object(); mo != nullptr) {
        for (const DrainHole& hole : SlaPoints::drain_holes_in_object(*mo)) {
            m_holes.push_back(Hole{hole, false});
        }
    }
}

void SlaHollowGizmo::write_items(ModelObject& model_object) const
{
    DrainHoles holes;
    holes.reserve(m_holes.size());
    for (const Hole& hole : m_holes) {
        holes.push_back(hole.hole);
    }
    SlaPoints::set_drain_holes_in_object(model_object, holes);
}

void SlaHollowGizmo::remember_item(const size_t index)
{
    m_hole_before_drag = m_holes[index];
}

bool SlaHollowGizmo::item_changed(const size_t index) const
{
    return m_hole_before_drag.has_value() && m_holes[index].hole != m_hole_before_drag->hole;
}

std::vector<SlaGizmoBase::ItemMesh> SlaHollowGizmo::item_meshes() const
{
    return {ItemMesh{m_cylinder_geometry.get(), m_cylinder_trimesh.get()}};
}

std::vector<std::optional<Transform3d>> SlaHollowGizmo::item_mesh_trafos(const size_t index) const
{
    const DrainHole& hole = m_holes[index].hole;
    const Vec3d position  = this->items_to_world() * hole.pos.cast<double>();
    const Vec3d direction = this->item_out_direction(index);

    // From the hole's depth inside the object to a bit above the surface, as it is drilled.
    Transform3d trafo = Transform3d::Identity();
    trafo.translate(position);
    trafo.rotate(Eigen::Quaterniond::FromTwoVectors(Vec3d::UnitZ(), direction));
    trafo.translate(-double(hole.height) * Vec3d::UnitZ());
    trafo.scale(Vec3d{hole.radius, hole.radius, hole.height + DrainHole::stick_out_length});
    return {trafo};
}

ColorRGBA SlaHollowGizmo::item_color(size_t) const
{
    return HoleColor;
}

void SlaHollowGizmo::on_item_selected(const size_t index)
{
    m_hole_diameter = 2. * m_holes[index].hole.radius;
    m_hole_depth    = m_holes[index].hole.height;
}

SLAPrintObjectStep SlaHollowGizmo::step_to_show() const
{
    return slaposAssembly;
}

SLAPrintObjectStep SlaHollowGizmo::step_after_apply() const
{
    return slaposDrillHoles;
}

UndoSnapshotType SlaHollowGizmo::snapshot_type(const Edit edit) const
{
    switch (edit) {
    case Edit::Add: return UndoSnapshotType::SlaDrainHolesAdd;
    case Edit::Delete: return UndoSnapshotType::SlaDrainHolesDelete;
    case Edit::Move: return UndoSnapshotType::SlaDrainHolesMove;
    case Edit::RemoveAll: return UndoSnapshotType::SlaDrainHolesRemoveAll;
    case Edit::Apply: return UndoSnapshotType::SlaDrainHolesApply;
    }
    return UndoSnapshotType::SlaDrainHolesApply;
}

const char* SlaHollowGizmo::command_prefix() const
{
    return "sla-hollow-gizmo";
}

void SlaHollowGizmo::on_target_activated()
{
    if (m_cylinder_trimesh == nullptr) {
        m_cylinder_trimesh  = std::make_unique<Scene::TriangleMesh>(TriMesh::its_make_cylinder(1., 1.));
        m_cylinder_geometry = Render::geometry_from_triangle_mesh(m_device, m_cylinder_trimesh->triangles());
    }
}

void SlaHollowGizmo::reset_dialog_clipping()
{
    m_dialog_ptr->set_clipping_of_view_value(0.);
}

const Domain::ConfigItem* SlaHollowGizmo::hollowing_option(const std::string& key) const
{
    const auto list = m_project_interactor.preset_interactor()
                          .object_settings_interactor()
                          .object_observable_list()
                          .lock();
    if (list == nullptr) {
        return nullptr;
    }

    for (size_t i = 0; i < list->size(); ++i) {
        const Biz::OverrideItem& item = list->at(i);
        if (item.name == key) {
            return item.config_item;
        }
    }
    return nullptr;
}

void SlaHollowGizmo::set_hollowing_option(const std::string& key, const Domain::ConfigValue& value)
{
    const Domain::ConfigItem* item = this->hollowing_option(key);
    if (item == nullptr) {
        return;
    }

    m_project_interactor.preset_interactor().set_item_value(*item, value, {0});
    // Show what the new settings do. The change reaches the object cache later.
    this->request_backend(slaposDrillHoles, false);
}

void SlaHollowGizmo::set_hole_size(const double diameter, const double depth)
{
    m_hole_diameter = diameter;
    m_hole_depth    = depth;

    bool changed = false;
    for (size_t index = 0; index < m_holes.size(); ++index) {
        if (m_holes[index].selected) {
            m_holes[index].hole.radius = float(diameter / 2.);
            m_holes[index].hole.height = float(depth);
            this->update_item_node(index);
            changed = true;
        }
    }

    if (changed) {
        this->edited(UndoSnapshotType::SlaDrainHolesSize);
    }
}

void SlaHollowGizmo::update_dialog()
{
    if (!this->active()) {
        return;
    }

    std::string status = this->common_status();
    if (status.empty()) {
        if (this->requested_step() == slaposDrillHoles) {
            status = _u8L("Hollowing and drilling the object...");
        } else if (m_holes.empty()) {
            status = _u8L("No drainage holes.");
        } else {
            const size_t count = m_holes.size();
            // TRN {} is the count of drainage holes
            status = fmt::vformat(_u8L("{} drainage holes"), fmt::make_format_args(count));
        }
    }
    if (this->dirty()) {
        status += "\n" + _u8L("The changes are not applied yet.");
    }
    m_dialog_ptr->set_status(status);

    m_dialog_ptr->set_input_enabled(this->input_enabled());
    m_dialog_ptr->set_apply_enabled(this->dirty());
    m_dialog_ptr->set_remove_all_enabled(!m_holes.empty());
    m_dialog_ptr->set_hole_size(m_hole_diameter, m_hole_depth);

    const Domain::ConfigItem* enable    = this->hollowing_option(HollowingEnableKey);
    const Domain::ConfigItem* thickness = this->hollowing_option(WallThicknessKey);
    const Domain::ConfigItem* accuracy  = this->hollowing_option(AccuracyKey);
    const Domain::ConfigItem* closing   = this->hollowing_option(ClosingDistanceKey);
    if (enable != nullptr && thickness != nullptr && accuracy != nullptr && closing != nullptr) {
        m_dialog_ptr->set_hollowing(
            enable->get<bool>(),
            thickness->get<double>(),
            accuracy->get<double>(),
            closing->get<double>()
        );
    }
}

} // namespace Slic3r::App::Plater
