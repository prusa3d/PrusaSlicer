#include "Slic3r/App/Plater/RotationDialog.hpp"
#include "Slic3r/App/Plater/DialogUtils.hpp"
#include "Slic3r/App/Plater/PlaceOnBedButton.hpp"
#include "Slic3r/App/Yoga/Text.hpp"
#include "Slic3r/App/Yoga/LayoutButton.hpp"
#include "Slic3r/App/Plater/TripleInput.hpp"
#include "Slic3r/Biz/I18N/I18N.hpp"
#include "Slic3r/App/Plater/PlaterGizmosHelper.hpp"
#include "Slic3r/Biz/Emboss/TextLines.hpp"
#include "Slic3r/Biz/Emboss/TextBender.hpp"
#include "Slic3r/Biz/Algorithms/TriangleMesh.hpp"
#include "Slic3r/Math.hpp"
#include <utility>

using namespace Slic3r::App::Yoga;

namespace Slic3r::App::Plater {

using Biz::_u8L;
using Yoga::Orientation;
using Yoga::Text;
using Domain::BoundingBox3d;
using Domain::SquareMatrix4d;
using Domain::SquareMatrix3d;
using Domain::Vec3d;

RotationDialog::RotationDialog(
    App::Plater::PlaterScenePresenter& scene_provider,
    Biz::ProjectInteractor& project_interactor
) :
    GizmoWindow(),
    m_scene_provider{scene_provider},
    m_project_interactor{project_interactor},
    m_projects{project_interactor}
{
    m_scene_provider.add_listener<App::Plater::ISelectionExtentsChangedListener>(this);
    m_project_interactor.add_listener<Biz::ISelectedProjectChangedListener>(this);
    m_project_interactor.scene_interactor()
        .add_listener<Biz::Scene::ISceneSelectionChangedListener>(this);

    content()->set_gap(2.f * gap_size());

    revert_button()->callbacks().action = [this]()
    {
        ProjectContext& project_context{m_projects.selected()};
        if (project_context.reset_rotation_candidates.empty()) {
            return;
        }
        const std::optional<Biz::Scene::SelectionExtents> selection_bounding_box{
            m_project_interactor.scene_interactor().selection_bounding_box()
        };
        const bool was_floating{selection_bounding_box && selection_bounding_box->is_floating()};
        m_project_interactor.scene_interactor().set_element_transforms(
            project_context.reset_rotation_candidates
        );
        if (selection_bounding_box && !was_floating) {
            Domain::SquareMatrix4d relative_transform_world{Domain::SquareMatrix4d::Identity()};
            relative_transform_world.col(3).z() =
                -m_project_interactor.scene_interactor().selection_bounding_box()->min_z();
            m_project_interactor.scene_interactor().transform_selection(relative_transform_world);
        }

        m_project_interactor.undo_provider().take_snapshot(
            Biz::UndoSnapshotType::RevertRotation
        );
    };

    auto rotation_section{content()->emplace_back<Yoga::Item>()};
    rotation_section->set_orientation(Orientation::Vertical);
    rotation_section->set_gap(10_fpx);

    auto title{rotation_section->emplace_back<Text>("Relative rotation")};
    title->set_font_type(Render::ImguiFontType::Bold);

    m_relative_input = rotation_section->emplace_back<TripleInput>(_u8L("°"));
    m_relative_input->on_change = [this](const Domain::Vec3d& value, int index)
    { add_rotation(Vec3d{deg2rad(value(0)), deg2rad(value(1)), deg2rad(value(2))}); };

    m_place_on_bed_button = rotation_section->emplace_back<PlaceOnBedButton>(m_project_interactor);

    add_separator(content());

    m_reference_frame_picker = content()->emplace_back<ReferenceFramePicker>(
        m_project_interactor,
        Biz::Scene::SelectionReferenceFrame::Volume
    );

    add_separator(content());

    m_bend_section = add_non_shrinked_wrap(content(), Orientation::Vertical, gap_size());

    auto bend_title{m_bend_section->emplace_back<Text>(_u8L("Text Bend & Curl"))};
    bend_title->set_font_type(Render::ImguiFontType::Bold);

    add_bend_control(AxisType::XAxis, _u8L("Vertical curl (X)"), _u8L("Reset vertical curl to zero"));
    add_bend_control(AxisType::YAxis, _u8L("Horizontal bend (Y)"), _u8L("Reset horizontal bend to zero"));
    add_bend_control(AxisType::ZAxis, _u8L("Vertical arc (Z)"), _u8L("Reset vertical arc to zero"));
    set_bend_values(0., 0., 0.);
    update_bend_locks();
    m_bend_section->set_visible(false);
}

void RotationDialog::add_bend_control(
    AxisType axis, const std::string& label, const std::string& reset_tooltip)
{
    const int index = *get_axis_index(axis);
    Item* row = add_row_with_slider(m_bend_section, &m_bend_inputs[index], label, _u8L("°"), reset_tooltip);
    auto* input = m_bend_inputs[index];
    set_limit_step(input, 180., 0.1);
    input->set_input_width(100);
    input->set_step_buttons_visible(true);
    input->set_default(0.);
    input->callbacks().value_changed = [this, axis](double value)
    {
        if (!m_updating_bend_controls && !is_bend_locked(axis))
            apply_bend_axis_change(axis, value);
    };
    input->revert_button()->callbacks().action = [this, axis]()
    {
        // Reset the stored angle even if a tiny 3D drag rounds to zero in the input.
        if (!is_bend_locked(axis))
            apply_bend_axis_change(axis, 0.);
    };

    auto* lock = row->emplace_back<LayoutButton>(std::string{}, Render::Icon::Unlock);
    m_bend_locks[index] = lock;
    lock->set_min_width(24);
    lock->set_min_height(24);
    lock->set_self_align(YGAlignCenter);
    lock->set_checkable(true);
    lock->callbacks().checked_changed = [this, index](bool checked)
    {
        if (m_updating_bend_controls)
            return;
        m_projects.selected().bend_locked[index] = checked;
        update_bend_locks();
        if (on_bend_locks_changed)
            on_bend_locks_changed();
    };
}

bool RotationDialog::is_bend_locked(AxisType axis) const
{
    const auto index = get_axis_index(axis);
    return index && m_projects.selected().bend_locked[*index];
}

void RotationDialog::update_bend_locks()
{
    const bool was_updating = std::exchange(m_updating_bend_controls, true);
    for (size_t i = 0; i < m_bend_inputs.size(); ++i) {
        const bool locked = m_projects.selected().bend_locked[i];
        m_bend_locks[i]->set_checked(locked);
        m_bend_locks[i]->set_icon(locked ? Render::Icon::Lock : Render::Icon::Unlock);
        m_bend_locks[i]->set_tooltip(locked
            ? _u8L("Unlock this angle for editing and dragging its 3D handle.")
            : _u8L("Lock this angle to prevent edits and dragging its 3D handle."));
        m_bend_inputs[i]->set_enabled(!locked);
        m_bend_inputs[i]->revert_button()->set_enabled(!locked);
    }
    m_updating_bend_controls = was_updating;
}

void RotationDialog::apply_bend_axis_change(AxisType axis, double degrees)
{
    const auto selected_text = Biz::Emboss::get_selected_text_volume(m_project_interactor);
    if (!selected_text.volume || !selected_text.volume->text_configuration)
        return;
    const auto& prop = selected_text.volume->text_configuration->style.prop;
    // Preserve the other axes at their stored precision, including values from 3D dragging.
    apply_bend_change(
        axis == AxisType::YAxis ? deg2rad(degrees) : prop.bend_horizontal.value_or(0.f),
        axis == AxisType::XAxis ? deg2rad(degrees) : prop.bend_vertical.value_or(0.f),
        axis == AxisType::ZAxis ? deg2rad(degrees) : prop.bend_arc.value_or(0.f)
    );
}

RotationDialog::~RotationDialog()
{
    m_scene_provider.remove_listener<App::Plater::ISelectionExtentsChangedListener>(this);
    m_project_interactor.remove_listener<Biz::ISelectedProjectChangedListener>(this);
    m_project_interactor.scene_interactor()
        .remove_listener<Biz::Scene::ISceneSelectionChangedListener>(this);
}

void RotationDialog::on_scene_selection_bounding_box_changed(
    Domain::SelectionId project_id,
    const std::optional<Biz::Scene::SelectionExtents>&
) {
    reload(project_id);
}

void RotationDialog::on_selected_project_changed_final(size_t index) {
    reload(index);
}

void RotationDialog::on_scene_selection_changed(
    Domain::SelectionId project_id,
    const Biz::Scene::ObjectSelection& selection
) {
    reload(project_id);
}

void RotationDialog::on_activated(Domain::SelectionId project_id) {
    m_projects.selected().activated = true;
    m_reference_frame_picker->on_activated();
    reload(project_id);
}

void RotationDialog::on_deactivated() {
    m_reference_frame_picker->on_deactivated();
    m_projects.selected().activated = false;
}

PlaceOnBedButton& RotationDialog::place_on_bed_button() {
    return *m_place_on_bed_button;
}

void RotationDialog::reload(std::optional<Domain::SelectionId> project_id) {
    ProjectContext& project_context{m_projects.selected()};
    if (!project_context.activated) {
        return;
    }
    if (project_id && project_id != m_project_interactor.selected_project_id()) {
        return;
    }

    m_relative_input->set_value({0, 0, 0});
    update_bend_locks();

    project_context.reset_rotation_candidates = get_reset_rotation_candidates();

    if (project_context.reset_rotation_candidates.empty()) {
        revert_button()->set_visible(false);
    } else {
        revert_button()->set_visible(true);
    }

    if (m_project_interactor.scene_interactor().object_selection().contains_wipe_tower()) {
        m_relative_input->set_visible({false, false, true});
    } else {
        m_relative_input->set_visible({true, true, true});
    }

    auto selected_text = Biz::Emboss::get_selected_text_volume(m_project_interactor);
    const bool is_text = selected_text.volume != nullptr && selected_text.volume->text_configuration.has_value();
    if (m_bend_section != nullptr) {
        m_bend_section->set_visible(is_text);
        if (is_text) {
            const auto& prop = selected_text.volume->text_configuration->style.prop;
            double h_deg = rad2deg(static_cast<double>(prop.bend_horizontal.value_or(0.0f)));
            double v_deg = rad2deg(static_cast<double>(prop.bend_vertical.value_or(0.0f)));
            double arc_deg = rad2deg(static_cast<double>(prop.bend_arc.value_or(0.0f)));
            set_bend_values(h_deg, v_deg, arc_deg);
        }
    }
}

void RotationDialog::set_bend_values(double horizontal_bend_deg, double vertical_curl_deg, double vertical_arc_deg)
{
    // SliderWithInput notifies on programmatic updates as well as user edits.
    const bool was_updating = std::exchange(m_updating_bend_controls, true);
    const std::array values{vertical_curl_deg, horizontal_bend_deg, vertical_arc_deg};
    for (size_t i = 0; i < m_bend_inputs.size(); ++i) {
        m_bend_inputs[i]->set_value(values[i]);
        m_bend_inputs[i]->revert_button()->set_visible(values[i] != 0.);
    }
    m_updating_bend_controls = was_updating;
}

void RotationDialog::apply_bend_change(double horizontal_bend_rad, double vertical_curl_rad, double vertical_arc_rad)
{
    horizontal_bend_rad = Biz::Emboss::TextBender::clamp_angle(horizontal_bend_rad);
    vertical_curl_rad = Biz::Emboss::TextBender::clamp_angle(vertical_curl_rad);
    vertical_arc_rad = Biz::Emboss::TextBender::clamp_angle(vertical_arc_rad);
    set_bend_values(rad2deg(horizontal_bend_rad), rad2deg(vertical_curl_rad), rad2deg(vertical_arc_rad));

    auto selected_text = Biz::Emboss::get_selected_text_volume(m_project_interactor);
    if (!selected_text.volume || !selected_text.volume->text_configuration)
        return;

    auto& mutable_volume = const_cast<Domain::ModelVolume&>(*selected_text.volume);
    float prev_h = mutable_volume.text_configuration->style.prop.bend_horizontal.value_or(0.0f);
    float prev_v = mutable_volume.text_configuration->style.prop.bend_vertical.value_or(0.0f);
    float prev_arc = mutable_volume.text_configuration->style.prop.bend_arc.value_or(0.0f);
    if (prev_h == static_cast<float>(horizontal_bend_rad) && prev_v == static_cast<float>(vertical_curl_rad)
        && prev_arc == static_cast<float>(vertical_arc_rad))
        return;

    indexed_triangle_set its = mutable_volume.mesh().its;
    auto bbox = mutable_volume.mesh().bounding_box();
    if (std::abs(prev_h) > 1e-4f || std::abs(prev_v) > 1e-4f || std::abs(prev_arc) > 1e-4f) {
        Biz::Emboss::BendParams prev_params{prev_h, prev_v, prev_arc};
        if (const auto& reference = mutable_volume.text_configuration->bend_reference)
            Biz::Emboss::TextBender::restore_mesh(its, prev_params, *reference);
        else
            Biz::Emboss::TextBender::unbend_mesh(its, prev_params, bbox);
    }

    auto unbent_bbox = mutable_volume.text_configuration->bend_reference.value_or(Domain::bounding_box(its));

    Biz::Emboss::BendParams params{
        .horizontal_bend = static_cast<float>(horizontal_bend_rad),
        .vertical_curl = static_cast<float>(vertical_curl_rad),
        .vertical_arc = static_cast<float>(vertical_arc_rad)
    };
    Biz::Emboss::TextBender::bend_mesh(its, params, unbent_bbox);
    auto bent_mesh = Biz::Algorithms::TriangleMesh::construct(std::move(its));
    mutable_volume.text_configuration->style.prop.bend_horizontal = params.horizontal_bend;
    mutable_volume.text_configuration->style.prop.bend_vertical = params.vertical_curl;
    mutable_volume.text_configuration->style.prop.bend_arc = params.vertical_arc;
    mutable_volume.text_configuration->bend_reference = unbent_bbox;

    Domain::ElementRef ref(
        selected_text.volume->get_object()->id().id,
        selected_text.instance_id,
        selected_text.volume->id().id
    );
    Biz::Scene::SceneInteractor::RefMeshes ref_meshes;
    ref_meshes.emplace_back(ref, std::move(bent_mesh));
    m_project_interactor.scene_interactor().change_volume_meshes(std::move(ref_meshes));
    m_project_interactor.undo_provider().take_snapshot(Biz::UndoSnapshotType::Rotate);
}

void RotationDialog::add_rotation(Domain::Vec3d rotate_by_rads)
{
    const std::optional<Biz::Scene::SelectionExtents> selection_bounding_box{
        m_project_interactor.scene_interactor().selection_bounding_box()
    };
    if (!selection_bounding_box) {
        return;
    }

    const Biz::Scene::OrientedBoundingBox& bounding_box{selection_bounding_box->oriented_bounding_box()};

    rotate_by_rads(1) = -rotate_by_rads(1);

    const bool was_floating{selection_bounding_box->is_floating()};
    Biz::Scene::SceneInteractor& scene_interactor{m_project_interactor.scene_interactor()};
    scene_interactor.transform_selection(
        get_rotation_matrix(
            bounding_box.rotation,
            bounding_box.center,
            rotate_by_rads
        ),
        !was_floating
    );
    m_project_interactor.undo_provider().take_snapshot(Biz::UndoSnapshotType::SetRotation);
}

Domain::SquareMatrix4d remove_rotation(
    const Domain::Transform3d& matrix,
    const Domain::Vec3d& center
)
{
    Domain::SquareMatrix4d result{matrix.matrix()};
    result.col(3).head<3>() -= center;

    const SquareMatrix3d rotation{matrix.rotation()};
    SquareMatrix4d inverse_rotation{SquareMatrix4d::Identity()};
    inverse_rotation.block(0, 0, 3, 3) = rotation.transpose();
    result = inverse_rotation * result;

    result.col(3).head<3>() += center;

    return result;
}

Domain::SquareMatrix4d remove_rotation(
    const Domain::Transform3d& volume_trafo,
    const Domain::Transform3d& instance_trafo,
    const Domain::Vec3d& center
)
{
    Domain::SquareMatrix3d linear_part{volume_trafo.rotation().transpose()};

    Domain::SquareMatrix4d local{Domain::SquareMatrix4d::Identity()};
    local.block<3, 3>(0, 0) = linear_part;
    local.block<3, 1>(0, 3) = center - linear_part * center;

    local = (instance_trafo.inverse() * local * instance_trafo).matrix();

    return (local * volume_trafo).matrix();
}

Biz::Scene::SceneInteractor::ElementTransforms RotationDialog::get_reset_rotation_candidates() const
{
    Biz::Scene::SceneInteractor& scene_interactor{m_project_interactor.scene_interactor()};
    Biz::Scene::SceneInteractor::ElementTransforms result;

    using Biz::Scene::SelectionState;
    const SelectionState state{scene_interactor.object_selection().state()};

    if (state != SelectionState::SingleVolume
        && state != SelectionState::MultipleVolumes
        && state != SelectionState::WholeInstance)
    {
        return {};
    }

    std::optional<Biz::Scene::SelectionExtents> selection_bounding_box{
        m_project_interactor.scene_interactor().selection_bounding_box()
    };
    if (!selection_bounding_box) {
        return {};
    }

    const Biz::Scene::OrientedBoundingBox& bounding_box{selection_bounding_box->oriented_bounding_box()};

    for (const Domain::ElementRef& element : scene_interactor.object_selection().elements) {
        ASSERT(element.has_instance());

        const Domain::ModelInstance* instance{
            m_project_interactor.workbench()
                .project(m_project_interactor.selected_project_id())
                .find_instance_by_id(element.object_id, element.instance_id)
        };
        const Domain::Transform3d instance_matrix{instance->get_matrix()};

        if (element.has_volume()) {
            const Domain::ElementRef ref{element.object_id, 0, element.volume_id};
            const Domain::ModelVolume* volume{
                m_project_interactor.workbench()
                    .project(m_project_interactor.selected_project_id())
                    .find_volume_by_id(element.object_id, element.volume_id)
            };
            const Domain::Transform3d volume_matrix{volume->get_matrix()};

            const SquareMatrix4d no_rotation{
                remove_rotation(volume_matrix, instance_matrix, bounding_box.center)
            };
            if (!volume_matrix.matrix().isApprox(no_rotation)) {
                result.insert_or_assign(ref, no_rotation);
            }
        } else {
            const Domain::ElementRef instance_ref{element.object_id, instance->id().id};
            const SquareMatrix4d instance_no_rotation{
                remove_rotation(instance_matrix, bounding_box.center)
            };
            if (!instance_matrix.matrix().isApprox(instance_no_rotation)) {
                result.insert_or_assign(instance_ref, instance_no_rotation);
            }

            for (const Domain::ModelVolume* volume : instance->get_object()->volumes) {
                const Domain::ElementRef volume_ref{element.object_id, 0, volume->id().id};
                const Domain::Transform3d volume_matrix{volume->get_matrix()};
                const SquareMatrix4d no_rotation{remove_rotation(
                    volume_matrix,
                    Domain::Transform3d{instance_no_rotation},
                    bounding_box.center
                )};
                if (!volume_matrix.matrix().isApprox(no_rotation)) {
                    result.insert_or_assign(volume_ref, no_rotation);
                }
            }
        }
    }

    return result;
}
} // namespace Slic3r::App::Plater
