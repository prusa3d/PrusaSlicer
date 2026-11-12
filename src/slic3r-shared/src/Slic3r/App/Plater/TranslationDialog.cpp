#include "Slic3r/App/Plater/TranslationDialog.hpp"
#include "Slic3r/App/Plater/PlaceOnBedButton.hpp"
#include "Slic3r/App/Plater/PlaterGizmosHelper.hpp"
#include "Slic3r/Biz/I18N/I18N.hpp"
#include "Slic3r/App/Yoga/Text.hpp"
#include "Slic3r/App/Yoga/LayoutButton.hpp"
#include "Slic3r/App/Yoga/ToggleButton.hpp"
#include "Slic3r/App/Yoga/RadioButton.hpp"
#include "Slic3r/Biz/ProjectInteractor.hpp"
#include "Slic3r/Biz/Scene/SceneInteractor.hpp"

using namespace Slic3r::App::Yoga;

namespace Slic3r::App::Plater {

using Biz::_u8L;
using Biz::Scene::ObjectSelection;
using Biz::Scene::SceneInteractor;
using Domain::BoundingBox3d;
using Domain::ElementRef;
using Domain::ElementRefs;
using Domain::SquareMatrix4d;
using Domain::Vec3d;

TranslationDialog::TranslationDialog(
    App::Plater::PlaterScenePresenter& scene_provider,
    Biz::ProjectInteractor& project_interactor
) :
    GizmoWindow(),
    m_scene_provider(scene_provider),
    m_project_interactor{project_interactor},
    m_projects{project_interactor}
{
    m_scene_provider.add_listener<App::Plater::ISelectionExtentsChangedListener>(this);
    m_project_interactor.scene_interactor().add_listener<Biz::ISelectedBedInstancesChangedListener>(
        this
    );
    m_project_interactor.add_listener<Biz::ISelectedProjectChangedListener>(this);
    m_project_interactor.scene_interactor().add_listener<Biz::Scene::ISceneSelectionChangedListener>(this);

    content()->set_padding({20_fpx, 20_fpx});
    content()->set_orientation(Yoga::Orientation::Vertical);
    content()->set_gap(20_fpx);

    m_absolute_input_row = content()->emplace_back<Yoga::Item>();
    m_absolute_input_row->set_orientation(Orientation::Vertical);
    m_absolute_input_row->set_gap(10_fpx);
    auto absolute_text{m_absolute_input_row->emplace_back<Text>("Translation")};
    absolute_text->set_font_type(Render::ImguiFontType::Bold);
    m_absolute_input = m_absolute_input_row->emplace_back<TripleInput>(_u8L("mm"));
    m_absolute_input->on_change = [this](const Domain::Vec3d& value, int)
    {
        const std::optional<Biz::Scene::SelectionExtents> selection_bounding_box{
            m_project_interactor.scene_interactor().selection_bounding_box()
        };

        const std::optional<Domain::Vec3d> reference_point{get_absolute_position_reference_point()};

        if (!selection_bounding_box || !reference_point) {
            return;
        }

        const Biz::Scene::OrientedBoundingBox& bounding_box{selection_bounding_box->oriented_bounding_box()};

        const Domain::Vec3d current_value{bounding_box.center - *reference_point};
        apply_relative_translation(value - current_value);
    };

    m_relative_input_row = content()->emplace_back<Yoga::Item>();
    m_relative_input_row->set_orientation(Orientation::Vertical);
    m_relative_input_row->set_gap(10_fpx);
    auto relative_text{m_relative_input_row->emplace_back<Text>("Relative translation")};
    relative_text->set_font_type(Render::ImguiFontType::Bold);
    m_relative_input = m_relative_input_row->emplace_back<TripleInput>(_u8L("mm"));
    m_relative_input->on_change = [this](const Domain::Vec3d& value, int)
    { apply_relative_translation(value); };

    auto place_on_bed_button{content()->emplace_back<PlaceOnBedButton>(m_project_interactor)};
    place_on_bed_button->set_margin({0, -10_fpx, 0, 0});

    add_separator(content());
    m_reference_frame_picker = content()->emplace_back<ReferenceFramePicker>(
        m_project_interactor,
        Biz::Scene::SelectionReferenceFrame::Bed
    );
}

TranslationDialog::~TranslationDialog()
{
    m_scene_provider.remove_listener<App::Plater::ISelectionExtentsChangedListener>(this);
    m_project_interactor.scene_interactor()
        .remove_listener<Biz::ISelectedBedInstancesChangedListener>(this);
    m_project_interactor.remove_listener<Biz::ISelectedProjectChangedListener>(this);
    m_project_interactor.scene_interactor().remove_listener<Biz::Scene::ISceneSelectionChangedListener>(this);
}

void TranslationDialog::on_selected_bed_instances_changed(
    Domain::SelectionId project_id,
    const Biz::Scene::BedSelection&
)
{
    reload(project_id);
}

void TranslationDialog::on_scene_selection_bounding_box_changed(
    Domain::SelectionId project_id,
    const std::optional<Biz::Scene::SelectionExtents>&
)
{
    reload(project_id);
}

void TranslationDialog::on_selected_project_changed_final(size_t index) {
    reload(index);
}

void TranslationDialog::on_scene_selection_changed(
    Domain::SelectionId project_id,
    const Biz::Scene::ObjectSelection& selection
) {
    reload(project_id);
}

void TranslationDialog::on_activated()
{
    m_projects.selected().activated = true;
    m_reference_frame_picker->on_activated();
    reload(m_project_interactor.selected_project_id());
}

void TranslationDialog::on_deactivated()
{
    m_projects.selected().activated = false;
    m_reference_frame_picker->on_deactivated();
}

Domain::Vec3d get_selected_bed_translation(const Biz::ProjectInteractor& project_interactor)
{
    const Domain::Workbench& workbench{project_interactor.workbench()};
    const Domain::Project& project{workbench.project(project_interactor.selected_project_id())};
    const Biz::Scene::SceneInteractor& scene_interactor{project_interactor.scene_interactor()};
    const Domain::BedRef bed_ref{scene_interactor.bed_selection().last_selected_bed()};
    const Domain::BedInstance* bed_instance{project.find_bed_instance_by_id(bed_ref.instance_id)};
    return ASSERT_VAL(bed_instance)->transformation.get_matrix().translation();
}

std::optional<Vec3d> TranslationDialog::get_absolute_position_reference_point() const
{
    using Biz::Scene::SelectionReferenceFrame;
    using Biz::Scene::SelectionState;

    const SceneInteractor& scene_interactor{m_project_interactor.scene_interactor()};
    const Biz::Scene::ObjectSelection& selection{scene_interactor.object_selection()};
    const Biz::Scene::SelectionReferenceFrame reference_frame{
        scene_interactor.object_selection_reference_frame()
    };

    if (selection.empty()) {
        return std::nullopt;
    }

    const Domain::Vec3d bed_offset{get_selected_bed_translation(m_project_interactor)};

    if (selection.state() == SelectionState::SingleVolume) {
        ASSERT(selection.elements.size() == 1);

        if (reference_frame == SelectionReferenceFrame::Bed) {
            return bed_offset;
        } else if (reference_frame == SelectionReferenceFrame::Instance) {
            const Domain::ElementRef& element{selection.elements.front()};
            ASSERT(element.has_instance());
            const Domain::ModelInstance* instance{
                m_project_interactor.workbench()
                    .project(m_project_interactor.selected_project_id())
                    .find_instance_by_id(element.object_id, element.instance_id)
            };
            return instance->get_matrix().translation();
        }
    }

    if (selection.state() == SelectionState::WholeInstance
        && reference_frame == SelectionReferenceFrame::Bed)
    {
        return bed_offset;
    }

    return std::nullopt;
}

void TranslationDialog::reload(Domain::SelectionId project_id)
{
    if (!m_projects.selected().activated) {
        return;
    }
    if (project_id != m_project_interactor.selected_project_id()) {
        return;
    }

    const std::optional<Biz::Scene::SelectionExtents> selection_bounding_box{
        m_project_interactor.scene_interactor().selection_bounding_box()
    };
    if (!selection_bounding_box) {
        return;
    }

    const Biz::Scene::OrientedBoundingBox& bounding_box{selection_bounding_box->oriented_bounding_box()};

    m_relative_input_row->set_visible(false);
    m_absolute_input_row->set_visible(false);

    const std::optional<Domain::Vec3d> reference_point{get_absolute_position_reference_point()};
    if (!reference_point) {
        m_relative_input_row->set_visible(true);
        m_relative_input->set_value({0, 0, 0});
    } else {
        m_absolute_input_row->set_visible(true);
        m_absolute_input->set_value(bounding_box.center - *reference_point);
    }

    if (m_project_interactor.scene_interactor().object_selection().contains_wipe_tower()) {
        m_relative_input->set_visible({true, true, false});
        m_absolute_input->set_visible({true, true, false});
    } else {
        m_relative_input->set_visible({true, true, true});
        m_absolute_input->set_visible({true, true, true});
    }
}

void TranslationDialog::apply_relative_translation(const Domain::Vec3d& translation)
{
    const std::optional<Biz::Scene::SelectionExtents> selection_bounding_box{
        m_project_interactor.scene_interactor().selection_bounding_box()
    };
    if (!selection_bounding_box) {
        return;
    }

    Biz::Scene::SceneInteractor& scene_interactor{m_project_interactor.scene_interactor()};
    scene_interactor.transform_selection(
        get_translation_matrix(
            selection_bounding_box->oriented_bounding_box().rotation,
            translation
        ),
        false
    );
    m_project_interactor.undo_provider().take_snapshot(Biz::UndoSnapshotType::SetTranslation);
}

} // namespace Slic3r::App::Plater
