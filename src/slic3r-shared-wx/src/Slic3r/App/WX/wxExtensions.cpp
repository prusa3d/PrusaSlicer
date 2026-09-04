#include "Slic3r/App/WX/wxExtensions.hpp"

#include <wx/wx.h>
#include <wx/dialog.h>

#include "Slic3r/App/WX/WidgetsConfig.hpp"

namespace Slic3r::App::WX {

/* Function for rescale of buttons in Dialog under MSW if dpi is changed.
 * btn_ids - vector of buttons identifiers
 */
void msw_buttons_rescale(wxDialog* dlg, const int em_unit, const std::vector<int>& btn_ids, double height_koef/* = 1.*/)
{
    const wxSize& btn_size = wxSize(-1, int(2.5 * em_unit * height_koef + 0.5f));

    for (int btn_id : btn_ids) {
        // There is a case [FirmwareDialog], when we have wxControl instead of wxButton
        // so let casting everything to the wxControl
        wxControl* btn = static_cast<wxControl*>(dlg->FindWindowById(btn_id, dlg));
        if (btn)
            btn->SetMinSize(btn_size);
    }
}

/* Function for getting of em_unit value from correct parent.
 * In most of cases it is m_em_unit value from WidgetsConfig,
 * but for DPIDialogs it's its own value. 
 * This value will be used to correct rescale after moving between 
 * Displays with different HDPI */
int em_unit(wxWindow* win)
{
    /*
    if (win)
    {
        wxTopLevelWindow *toplevel = Slic3r::GUI::find_toplevel_parent(win);
        Slic3r::GUI::DPIDialog* dlg = dynamic_cast<Slic3r::GUI::DPIDialog*>(toplevel);
        if (dlg)
            return dlg->em_unit();
        Slic3r::GUI::DPIFrame* frame = dynamic_cast<Slic3r::GUI::DPIFrame*>(toplevel);
        if (frame)
            return frame->em_unit();
    }
    */
    return w_config()->em_unit();
}


}


