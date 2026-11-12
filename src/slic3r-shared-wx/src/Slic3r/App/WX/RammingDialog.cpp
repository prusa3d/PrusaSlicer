#include "Slic3r/App/WX/RammingDialog.hpp"
#include "Slic3r/App/WX/WidgetsConfig.hpp"
#include "Slic3r/App/WX/I18N.hpp"
#include "Slic3r/App/WX/MsgDialog.hpp"

#include <sstream>

#include <wx/sizer.h>

#include <fmt/format.h>

namespace Slic3r::App::WX {

int scale(const int val)
{
    return val * w_config()->em_unit();
}

int ITEM_WIDTH()
{
    return scale(6);
}

RammingDialog::RammingDialog(const std::string& parameters) :
    wxDialog(
        wxTheApp->GetTopWindow(),
        wxID_ANY,
        _L("Ramming customization"),
        wxDefaultPosition,
        wxDefaultSize,
        wxDEFAULT_DIALOG_STYLE /* | wxRESIZE_BORDER*/
    )
{
    CenterOnParent();
    SetFont(w_config()->normal_font());
    m_panel_ramming = new RammingPanel(this, parameters);

    m_panel_ramming->Show(true);
    this->Show();

    auto main_sizer = new wxBoxSizer(wxVERTICAL);
    main_sizer->Add(m_panel_ramming, 1, wxEXPAND | wxTOP | wxLEFT | wxRIGHT, 5);
    auto buttons = CreateStdDialogButtonSizer(wxOK | wxCANCEL);
    w_config()->SetWindowVariantForButton(buttons->GetAffirmativeButton());
    w_config()->SetWindowVariantForButton(buttons->GetCancelButton());
    main_sizer->Add(buttons, 0, wxALIGN_CENTER_HORIZONTAL | wxTOP | wxBOTTOM, 10);
    SetSizer(main_sizer);
    main_sizer->SetSizeHints(this);

    this->Bind(wxEVT_CLOSE_WINDOW, [this](wxCloseEvent& e) { EndModal(wxCANCEL); });

    this->Bind(
        wxEVT_BUTTON,
        [this](wxCommandEvent&)
        {
            m_output_data = m_panel_ramming->get_parameters();
            EndModal(wxID_OK);
        },
        wxID_OK
    );
    this->Show();
    WarningDialog dlg(
        this,
        _L(
            "Ramming denotes the rapid extrusion just before a tool change in a single-extruder MM printer. Its purpose is to "
            "properly shape the end of the unloaded filament so it does not prevent insertion of the new filament and can itself "
            "be reinserted later. This phase is important and different materials can require different extrusion speeds to get "
            "the good shape. For this reason, the extrusion rates during ramming are adjustable.\n\nThis is an expert-level "
            "setting, incorrect adjustment will likely lead to jams, extruder wheel grinding into filament etc."
        )
    );
    dlg.ShowModal();
}

RammingPanel::RammingPanel(wxWindow* parent, const std::string& parameters) :
    wxPanel(
        parent,
        wxID_ANY,
        wxDefaultPosition,
        wxDefaultSize /*,wxPoint(50,50), wxSize(800,350),wxBORDER_RAISED*/
    )
{
    auto sizer_chart = new wxBoxSizer(wxVERTICAL);
    auto sizer_param = new wxBoxSizer(wxVERTICAL);

    std::stringstream stream{parameters};
    stream >> m_ramming_line_width_multiplicator >> m_ramming_step_multiplicator;
    int ramming_speed_size = 0;
    float dummy            = 0.f;
    while (stream >> dummy)
        ++ramming_speed_size;
    stream.clear();
    stream.get();

    std::vector<std::pair<float, float>> buttons;
    float x = 0.f;
    float y = 0.f;
    while (stream >> x >> y)
        buttons.push_back(std::make_pair(x, y));

    m_chart = new Chart(
        this,
        wxRect(scale(1), scale(1), scale(48), scale(36)),
        buttons,
        ramming_speed_size,
        0.25f,
        scale(1)
    );

    sizer_chart->Add(m_chart, 0, wxALL, 5);

    const long style = wxSP_ARROW_KEYS;

    m_widget_time = new Widgets::SpinInputDouble(
        this,
        from_u8(""),
        wxEmptyString,
        wxDefaultPosition,
        wxSize(ITEM_WIDTH(), -1),
        style,
        0.,
        5.,
        3.,
        0.25
    );
    m_widget_time->SetDigits(2);
    m_widget_volume = new Widgets::SpinInput(
        this,
        from_u8(""),
        wxEmptyString,
        wxDefaultPosition,
        wxSize(ITEM_WIDTH(), -1),
        style,
        0,
        10'000,
        0
    );
    m_widget_ramming_line_width_multiplicator = new Widgets::SpinInput(
        this,
        from_u8(""),
        wxEmptyString,
        wxDefaultPosition,
        wxSize(ITEM_WIDTH(), -1),
        style,
        10,
        300,
        100
    );
    m_widget_ramming_step_multiplicator = new Widgets::SpinInput(
        this,
        from_u8(""),
        wxEmptyString,
        wxDefaultPosition,
        wxSize(ITEM_WIDTH(), -1),
        style,
        10,
        300,
        100
    );

    auto gsizer_param = new wxFlexGridSizer(2, 5, 15);
    gsizer_param->Add(
        new wxStaticText(
            this,
            wxID_ANY,
            from_u8(fmt::format("{} ({})", Biz::_u8L("Total ramming time"), Biz::_u8L("s")))
        ),
        0,
        wxALIGN_CENTER_VERTICAL
    );
    gsizer_param->Add(m_widget_time);
    gsizer_param->Add(
        new wxStaticText(
            this,
            wxID_ANY,
            from_u8(fmt::format("{} ({}³):", Biz::_u8L("Total rammed volume"), Biz::_u8L("mm")))
        ),
        0,
        wxALIGN_CENTER_VERTICAL
    );
    gsizer_param->Add(m_widget_volume);
    gsizer_param->AddSpacer(20);
    gsizer_param->AddSpacer(20);
    gsizer_param->Add(
        new wxStaticText(
            this,
            wxID_ANY,
            from_u8(fmt::format("{} (%):", Biz::_u8L("Ramming line width")))
        ),
        0,
        wxALIGN_CENTER_VERTICAL
    );
    gsizer_param->Add(m_widget_ramming_line_width_multiplicator);
    gsizer_param->Add(
        new wxStaticText(
            this,
            wxID_ANY,
            from_u8(fmt::format("{} (%):", Biz::_u8L("Ramming line spacing")))
        ),
        0,
        wxALIGN_CENTER_VERTICAL
    );
    gsizer_param->Add(m_widget_ramming_step_multiplicator);
    gsizer_param->AddSpacer(40);
    gsizer_param->AddSpacer(40);

    std::string ctrl_str = w_config()->shortkey_ctrl_prefix();
    if (!ctrl_str.empty() && ctrl_str.back() == '+')
        ctrl_str.pop_back();
    gsizer_param->Add(
        new wxStaticText(
            this,
            wxID_ANY,
            from_u8(
                fmt::format(
                    fmt::runtime(
                        // TRN: The placeholder expands to Ctrl or Cmd (on macOS).
                        Biz::_u8L("For constant flow rate, hold {} while dragging.")
                    ),
                    ctrl_str
                )
            )
        ),
        0,
        wxALIGN_CENTER_VERTICAL
    );

    sizer_param->Add(gsizer_param, 0, wxTOP, scale(10));

    m_widget_time->SetValue(m_chart->get_time());
    m_widget_volume->SetValue(m_chart->get_volume());
    m_widget_volume->Disable();
    m_widget_ramming_line_width_multiplicator->SetValue(m_ramming_line_width_multiplicator);
    m_widget_ramming_step_multiplicator->SetValue(m_ramming_step_multiplicator);

    m_widget_ramming_step_multiplicator->Bind(
        wxEVT_TEXT,
        [this](wxCommandEvent&) { line_parameters_changed(); }
    );
    m_widget_ramming_line_width_multiplicator->Bind(
        wxEVT_TEXT,
        [this](wxCommandEvent&) { line_parameters_changed(); }
    );

    auto sizer = new wxBoxSizer(wxHORIZONTAL);
    sizer->Add(sizer_chart, 0, wxALL, 5);
    sizer->Add(sizer_param, 0, wxALL, 10);

    sizer->SetSizeHints(this);
    SetSizer(sizer);

    m_widget_time->Bind(
        wxEVT_SPINCTRL,
        [this](wxCommandEvent&) { m_chart->set_xy_range(m_widget_time->GetValue(), -1); }
    );
    m_widget_time->Bind(
        wxEVT_CHAR,
        [](wxKeyEvent&) {}
    ); // do nothing - prevents the user to change the value
    m_widget_time->GetText()->Bind(
        wxEVT_CHAR,
        [](wxKeyEvent&) {}
    ); // do nothing - prevents the user to change the value
    m_widget_volume->Bind(
        wxEVT_CHAR,
        [](wxKeyEvent&) {}
    ); // do nothing - prevents the user to change the value
    Bind(
        EVT_WIPE_TOWER_CHART_CHANGED,
        [this](wxCommandEvent&)
        {
            m_widget_volume->SetValue(m_chart->get_volume());
            m_widget_time->SetValue(m_chart->get_time());
        }
    );
    Refresh(true); // erase background
}

void RammingPanel::line_parameters_changed()
{
    m_ramming_line_width_multiplicator = m_widget_ramming_line_width_multiplicator->GetValue();
    m_ramming_step_multiplicator       = m_widget_ramming_step_multiplicator->GetValue();
}

std::string RammingPanel::get_parameters()
{
    std::vector<float> speeds                    = m_chart->get_ramming_speed(0.25f);
    std::vector<std::pair<float, float>> buttons = m_chart->get_buttons();
    std::stringstream stream;
    stream << m_ramming_line_width_multiplicator << " " << m_ramming_step_multiplicator;
    for (const float& speed_value : speeds)
        stream << " " << speed_value;
    stream << "|";
    for (const auto& button : buttons)
        stream << " " << button.first << " " << button.second;
    return stream.str();
}

} // namespace Slic3r::App::WX
