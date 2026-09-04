#include "Slic3r/App/Preview/SidebarAutoReslice.hpp"

#include "Slic3r/App/Yoga/ToggleButton.hpp"

#include "Slic3r/Biz/ProjectInteractor.hpp"
#include "Slic3r/Biz/I18N/I18N.hpp"

namespace Slic3r::App::Preview {
using Biz::ProjectInteractor;
using Biz::Slicing::SlicingInteractor;

SidebarAutoReslice::SidebarAutoReslice(ProjectInteractor& project_interactor) :
    Window("SidebarAutoReslice")
{
    set_min_width(220);

    Item* row = emplace_back<Yoga::Item>();
    row->set_orientation(Yoga::Orientation::Horizontal);

    m_auto_reslice_chb = row->emplace_back<Yoga::ToggleButton>(
        Biz::_u8L("Auto-reslice"),
        Biz::_u8L("Automatically runs slicing after any settings change.")
    );
    m_auto_reslice_chb->set_font_type(Render::ImguiFontType::Bold);
    m_auto_reslice_chb->callbacks().checked_changed = [&](bool checked)
    {
        SlicingInteractor& slicing_interactor{project_interactor.slicing_interactor()};
        if (checked) {
            slicing_interactor.enable_auto_slicing(project_interactor.selected_bed_slicing_id());
        } else {
            slicing_interactor.stop_all();
            slicing_interactor.disable_auto_slicing();
        }
        m_app_config.set("auto_reslice", checked);
    };
    m_auto_reslice_chb->set_margin({ 0.f, -5.f });
    m_auto_reslice_chb->set_checked(m_app_config.get<bool>("auto_reslice"));
}

bool SidebarAutoReslice::is_enabled() const
{
    return m_auto_reslice_chb->checked();
}

} // namespace Slic3r::App::Preview
