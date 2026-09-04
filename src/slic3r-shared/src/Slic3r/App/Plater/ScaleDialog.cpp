#include "Slic3r/App/Plater/ScaleDialog.hpp"
#include "Slic3r/App/Plater/ScaleWidget.hpp"
#include "Slic3r/Biz/I18N/I18N.hpp"

using namespace Slic3r::App::Yoga;

namespace Slic3r::App::Plater {

using Biz::_u8L;

ScaleDialog::ScaleDialog(Biz::ProjectInteractor& project_interactor) : GizmoWindow()
{
    content()->set_padding({20_fpx, 20_fpx});
    content()->set_orientation(Yoga::Orientation::Vertical);
    content()->set_gap(20_fpx);

    auto reference_frame_picker{std::make_unique<ReferenceFramePicker>(
        project_interactor,
        Biz::Scene::SelectionReferenceFrame::Volume
    )};

    m_scale_widget = content()->emplace_back<ScaleWidget>(
        project_interactor,
        revert_button(),
        reference_frame_picker.get()
    );

    add_separator(content());
    content()->append(std::move(reference_frame_picker));
}

void ScaleDialog::on_activated(Domain::SelectionId project_id)
{
    m_scale_widget->on_activated(project_id);
}

void ScaleDialog::on_deactivated()
{
    m_scale_widget->on_deactivated();
}

} // namespace Slic3r::App::Plater
