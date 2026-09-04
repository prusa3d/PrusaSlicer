#pragma once

#include <wx/spinctrl.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/checkbox.h>
#include <wx/msgdlg.h>

#include "Slic3r/App/WX/RammingChart.hpp"
#include "Slic3r/App/WX/Widgets/SpinInput.hpp"

namespace Slic3r::App::WX {

class RammingPanel : public wxPanel
{
public:
    RammingPanel(wxWindow* parent, const std::string& data);
    std::string get_parameters();

private:
    Chart* m_chart                                                = nullptr;
    Widgets::SpinInput* m_widget_volume                           = nullptr;
    Widgets::SpinInput* m_widget_ramming_line_width_multiplicator = nullptr;
    Widgets::SpinInput* m_widget_ramming_step_multiplicator       = nullptr;
    Widgets::SpinInputDouble* m_widget_time                       = nullptr;
    int m_ramming_step_multiplicator;
    int m_ramming_line_width_multiplicator;

    void line_parameters_changed();
};

class RammingDialog : public wxDialog
{
public:
    RammingDialog(const std::string& parameters);

    std::string get_parameters()
    {
        return m_output_data;
    }

private:
    RammingPanel* m_panel_ramming = nullptr;
    std::string m_output_data;
};
} // namespace Slic3r::App::WX
