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

int em_unit(wxWindow* win)
{
    return win ? w_config()->em_unit(win) : w_config()->em_unit();
}


}


