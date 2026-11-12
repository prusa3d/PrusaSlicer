#pragma once

#include <vector>

class wxDialog;
class wxWindow;

namespace Slic3r::App::WX {

void    msw_buttons_rescale(wxDialog* dlg, const int em_unit, const std::vector<int>& btn_ids, double height_koef = 1.);
int     em_unit(wxWindow* win);

}

