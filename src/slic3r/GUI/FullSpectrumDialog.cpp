#include "FullSpectrumDialog.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>
#include <set>

#include <wx/button.h>
#include <wx/clrpicker.h>
#include <wx/colordlg.h>
#include <wx/bmpbuttn.h>
#include <wx/dcbuffer.h>
#include <wx/dcclient.h>
#include <wx/dcgraph.h>
#include <wx/dcmemory.h>
#include <wx/image.h>
#include <wx/scrolwin.h>
#include <wx/wrapsizer.h>
#include <wx/msgdlg.h>
#include <wx/panel.h>
#include <wx/settings.h>
#include <wx/sizer.h>
#include <wx/spinctrl.h>
#include <wx/stattext.h>
#include <wx/vlbox.h>
#include <wx/wupdlock.h>

#include "libslic3r/Model.hpp"
#include "libslic3r/PrintConfig.hpp"

#include "BitmapComboBox.hpp"
#include "Widgets/Button.hpp"
#include "ColorUtils.hpp"
#include "GUI_App.hpp"
#include "I18N.hpp"
#include "Widgets/BarycentricRatioPicker.hpp"
#include "Widgets/GradientRatioBar.hpp"
#include "Widgets/LayerSequenceBar.hpp"
#include "Widgets/StaticBox.hpp"
#include "Widgets/UIColors.hpp"
#include "Widgets/VirtualExtruderListBox.hpp"
#include "format.hpp"
#include "wxExtensions.hpp"
#include "prusa_fdm_mixer.hpp"

using namespace Slic3r::FullSpectrum;

namespace Slic3r::GUI {

namespace {

wxColour border_color_normal()
{
    return GUI_App::dark_mode() ? wxColour(0xA0, 0xA0, 0xA0) : wxColour(0x40, 0x40, 0x40);
}

wxColour border_color_subtle()
{
    return GUI_App::dark_mode() ? wxColour(0x70, 0x70, 0x70) : wxColour(0x80, 0x80, 0x80);
}

wxColour border_color_cross()
{
    return GUI_App::dark_mode() ? wxColour(0x90, 0x90, 0x90) : wxColour(0x60, 0x60, 0x60);
}

wxColour window_bg_color()
{
    return wxGetApp().get_window_default_clr();
}

wxString make_list_title(const FullSpectrum::VirtualExtruder& ve)
{
    return wxString::Format(_L("Virtual extruder #%u"), ve.id);
}

wxString make_list_subtitle(const FullSpectrum::VirtualExtruder& ve)
{
    assert(ve.type() == VirtualExtruder::Type::Gradient);
    const FullSpectrum::VirtualExtruderGradient& gradient = *ve.gradient;
    if (gradient.z_min.has_value() && gradient.z_max.has_value())
        return wxString::Format(
            _L("Gradient %.1f – %.1f mm, %zu stops"),
            *gradient.z_min,
            *gradient.z_max,
            gradient.stops.size()
        );
    return wxString::Format(_L("Gradient (auto range), %zu colors"), gradient.stops.size());
}

} // namespace

FullSpectrumDialog::FullSpectrumDialog(
    wxWindow* parent,
    const Model& model,
    const DynamicPrintConfig& full_config
) :
    DPIDialog(
        parent,
        wxID_ANY,
        _L("Virtual Extruders - Color Mixing"),
        wxDefaultPosition,
        wxDefaultSize,
        wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER
    ),
    m_model(model)
{
    if (const ConfigOptionFloats* nozzle_diameter_opt =
            full_config.option<ConfigOptionFloats>("nozzle_diameter"))
        m_num_physical = static_cast<unsigned int>(nozzle_diameter_opt->values.size());

    std::vector<std::string> extruder_colors;
    if (const ConfigOptionStrings* extruder_colour_opt =
            full_config.option<ConfigOptionStrings>("extruder_colour"))
        extruder_colors = extruder_colour_opt->values;
    std::vector<std::string> filament_colors;
    if (const ConfigOptionStrings* filament_colour_opt =
            full_config.option<ConfigOptionStrings>("filament_colour"))
        filament_colors = filament_colour_opt->values;

    m_physical_colors.clear();
    m_physical_colors.reserve(m_num_physical);
    for (unsigned int i = 0; i < m_num_physical; ++i) {
        std::string color = i < extruder_colors.size() ? extruder_colors[i] : std::string();
        if (color.empty() && i < filament_colors.size())
            color = filament_colors[i];
        if (color.empty())
            color = "#808080";
        m_physical_colors.push_back(std::move(color));
    }

    m_physical_types.clear();
    m_physical_types.reserve(m_num_physical);
    if (const ConfigOptionStrings* filament_type_opt =
            full_config.option<ConfigOptionStrings>("filament_type"))
        for (unsigned int i = 0; i < m_num_physical; ++i)
            m_physical_types.push_back(
                i < filament_type_opt->values.size() ? filament_type_opt->values[i] : std::string()
            );
    else
        m_physical_types.resize(m_num_physical);

    m_working_list = model.virtual_extruders;
    m_original_ids.reserve(m_working_list.size());
    for (const FullSpectrum::VirtualExtruder& ve : m_working_list)
        m_original_ids.push_back(ve.id);

    m_preset_extruders_enabled.assign(m_num_physical, true);

    build_layout();
    refresh_virtual_extruder_list(-1);

    apply_colors();

    const int em = wxGetApp().em_unit();
    this->GetSizer()->SetSizeHints(this);
    const wxSize desired(100 * em, 62 * em);
    this->SetMinSize(desired);
    this->SetSize(desired);
    this->CentreOnParent();
}

void FullSpectrumDialog::on_dpi_changed(const wxRect&)
{
    this->Layout();
}

void FullSpectrumDialog::on_sys_color_changed()
{
    apply_colors();
}

void FullSpectrumDialog::apply_colors()
{
    const bool dark = GUI_App::dark_mode();

#ifdef _WIN32
    if (dark)
        wxGetApp().UpdateDlgDarkUI(this);
    else
        wxGetApp().UpdateDlgDarkUI(this, true);

    if (m_presets_scrolled_window)
        m_presets_scrolled_window->SetBackgroundColour(window_bg_color());

    if (!dark) {
        const wxColour panel_bg = wxSystemSettings::GetColour(wxSYS_COLOUR_BTNFACE);
        for (wxWindow* w :
             {m_left_panel,
              m_right_panel,
              (wxWindow*) m_presets_pane,
              (wxWindow*) m_editor_pane,
              (wxWindow*) m_editor_box})
            if (w)
                w->SetBackgroundColour(panel_bg);

        std::function<void(wxWindow*)> fix_text_bg = [&](wxWindow* w)
        {
            if (w == m_presets_scrolled_window)
                return;
            if (dynamic_cast<wxStaticText*>(w))
                w->SetBackgroundColour(panel_bg);
            for (auto* child : w->GetChildren())
                fix_text_bg(child);
        };
        if (m_left_panel)
            fix_text_bg(m_left_panel);
        if (m_right_panel)
            fix_text_bg(m_right_panel);
    }
#endif

    {
        StateColor btn_bg, btn_text;
        if (dark) {
            btn_bg = StateColor(
                std::make_pair(wxColour(0x40, 0x40, 0x40), (int) StateColor::Disabled),
                std::make_pair(wxColour(0x5A, 0x5A, 0x5A), (int) StateColor::Hovered),
                std::make_pair(wxColour(0x4E, 0x4E, 0x4E), (int) StateColor::Normal)
            );
            btn_text = StateColor(
                std::make_pair(wxColour(0x70, 0x70, 0x70), (int) StateColor::Disabled),
                std::make_pair(wxColour(250, 250, 250), (int) StateColor::Normal)
            );
        } else {
            btn_bg = StateColor(
                std::make_pair(wxColour(0xF0, 0xF0, 0xF0), (int) StateColor::Disabled),
                std::make_pair(*wxLIGHT_GREY, (int) StateColor::Hovered),
                std::make_pair(*wxWHITE, (int) StateColor::Normal)
            );
            btn_text = StateColor(
                std::make_pair(*wxLIGHT_GREY, (int) StateColor::Disabled),
                std::make_pair(*wxBLACK, (int) StateColor::Normal)
            );
        }
        if (m_btn_add_blend) {
            m_btn_add_blend->SetBackgroundColor(btn_bg);
            m_btn_add_blend->SetTextColor(btn_text);
        }
        if (m_btn_add_gradient) {
            m_btn_add_gradient->SetBackgroundColor(btn_bg);
            m_btn_add_gradient->SetTextColor(btn_text);
        }
    }

    Refresh();
}

static wxBitmap render_filter_chip_bitmap(
    unsigned int extruder_id_1based,
    const std::string& hex_color,
    bool enabled,
    const wxSize& size
);

void FullSpectrumDialog::build_layout()
{
    const int em  = wxGetApp().em_unit();
    const int pad = em;
    const int gap = em;

    wxBoxSizer* main_sizer = new wxBoxSizer(wxVERTICAL);

    if (m_num_physical < 2) {
        wxStaticText* hint = new wxStaticText(
            this,
            wxID_ANY,
            _L("Virtual extruders need at least two physical extruders. "
               "Increase the extruder count in Printer Settings first.")
        );
        hint->Wrap(60 * em);
        main_sizer->Add(hint, 0, wxALL | wxEXPAND, pad);
    }

    wxBoxSizer* split_sizer = new wxBoxSizer(wxHORIZONTAL);

    StaticBox* left_box = new StaticBox(this);
    m_left_panel        = left_box;
    left_box->SetCornerRadius(em / 2);
    left_box->SetBorderColor(
        StateColor(std::make_pair(clr_border_normal, (int) StateColor::Normal))
    );
    wxBoxSizer* left_box_sizer = new wxBoxSizer(wxVERTICAL);

    wxStaticText* left_title = new wxStaticText(left_box, wxID_ANY, _L("Virtual Extruders"));
    left_title->SetFont(wxGetApp().bold_font());
    left_box_sizer->Add(left_title, 0, wxALL, pad);

    m_virtual_extruder_list = new VirtualExtruderListBox(left_box, wxSize(32 * em, 30 * em));
    m_virtual_extruder_list->Bind(wxEVT_LISTBOX, &FullSpectrumDialog::on_list_selected, this);
    auto* click_intercept = new wxEvtHandler();
    click_intercept->Bind(
        wxEVT_LEFT_DOWN,
        [this](wxMouseEvent&)
        {
            m_virtual_extruder_list->SetFocus();
            const wxPoint client_pos =
                m_virtual_extruder_list->ScreenToClient(wxGetMousePosition());
            const int item_h        = m_virtual_extruder_list->ItemHeight();
            const int first_visible = int(m_virtual_extruder_list->GetVisibleBegin());
            const int hit_raw =
                (client_pos.y >= 0
                 && client_pos.x >= 0
                 && client_pos.x < m_virtual_extruder_list->GetClientSize().GetWidth()) ?
                (first_visible + client_pos.y / item_h) :
                wxNOT_FOUND;
            const int count = int(m_working_list.size());
            const int hit   = (hit_raw >= 0 && hit_raw < count) ? hit_raw : wxNOT_FOUND;

            if (hit != wxNOT_FOUND
                && m_virtual_extruder_list->TrashIconRect(size_t(hit)).Contains(client_pos))
            {
                if (m_virtual_extruder_list->on_remove_row)
                    m_virtual_extruder_list->on_remove_row(hit);
                return;
            }

            const int cur  = m_virtual_extruder_list->GetMySelection();
            const int next = (hit == cur) ? wxNOT_FOUND : hit;
            if (next == cur)
                return;
            m_virtual_extruder_list->SetMySelection(next);
            wxCommandEvent e(wxEVT_LISTBOX, m_virtual_extruder_list->GetId());
            e.SetEventObject(m_virtual_extruder_list);
            e.SetInt(next);
            m_virtual_extruder_list->ProcessWindowEvent(e);
        }
    );
    m_virtual_extruder_list->PushEventHandler(click_intercept);
    left_box_sizer->Add(m_virtual_extruder_list, 1, wxLEFT | wxRIGHT | wxEXPAND, pad);

    wxBoxSizer* buttons_sizer = new wxBoxSizer(wxHORIZONTAL);
    m_btn_add_blend = new Button(left_box, _L("Add blend"), "recipe_add", 0, wxSize(16, 16));
    m_btn_add_blend->SetCornerRadius(em / 2);
    m_btn_add_blend->SetPaddingSize(wxSize(em, em / 2));
    m_btn_add_blend->Bind(wxEVT_BUTTON, &FullSpectrumDialog::on_add_blend_clicked, this);
    m_btn_add_blend->SetToolTip(
        _L("Create a blend recipe that alternates two filaments layer by layer.")
    );

    m_btn_add_gradient = new Button(left_box, _L("Add gradient"), "recipe_add", 0, wxSize(16, 16));
    m_btn_add_gradient->SetCornerRadius(em / 2);
    m_btn_add_gradient->SetPaddingSize(wxSize(em, em / 2));
    m_btn_add_gradient->Bind(wxEVT_BUTTON, &FullSpectrumDialog::on_add_gradient, this);
    m_btn_add_gradient->SetToolTip(
        _L("Create a gradient recipe that transitions between filaments across a height range.")
    );

    buttons_sizer->Add(m_btn_add_blend, 0, wxRIGHT, gap / 2);

    // TODO: Disable gradient for now.
    // buttons_sizer->Add(m_btn_add_gradient, 0);
    m_btn_add_gradient->Hide();

    m_virtual_extruder_list->on_remove_row = [this](int index)
    {
        if (index < 0 || index >= int(m_working_list.size()))
            return;
        const unsigned int removed_id = m_working_list[index].id;
        const int users               = count_objects_using(removed_id);
        if (users > 0) {
            const wxString msg = format_wxstr(
                _L("Virtual extruder %1% is assigned to %2% object(s) or modifier(s). "
                   "Removing it will reset their extruder to default. Continue?"),
                removed_id,
                users
            );
            if (wxMessageBox(msg, _L("Remove virtual extruder"), wxYES_NO | wxICON_QUESTION, this)
                != wxYES)
                return;
        }
        m_working_list.erase(m_working_list.begin() + index);
        if (std::find(m_original_ids.begin(), m_original_ids.end(), removed_id)
            != m_original_ids.end())
            m_removed_ids.push_back(removed_id);
        refresh_virtual_extruder_list(-1);
        rebuild_preset_grid();
    };
    left_box_sizer->Add(buttons_sizer, 0, wxALL | wxEXPAND, pad);

    left_box->SetSizer(left_box_sizer);
    split_sizer->Add(left_box, 0, wxALL | wxEXPAND, pad);

    StaticBox* right_box = new StaticBox(this);
    right_box->SetCornerRadius(em / 2);
    right_box->SetBorderColor(
        StateColor(std::make_pair(clr_border_normal, (int) StateColor::Normal))
    );
    wxBoxSizer* right_box_sizer = new wxBoxSizer(wxVERTICAL);

    m_right_panel          = right_box;
    m_right_sizer          = right_box_sizer;
    m_presets_pane         = new wxPanel(right_box);
    m_editor_pane          = new wxPanel(right_box);
    const wxColour pane_bg = right_box->GetBackgroundColour();
    m_presets_pane->SetBackgroundColour(pane_bg);
    m_editor_pane->SetBackgroundColour(pane_bg);
    m_presets_pane->SetDoubleBuffered(true);
    m_editor_pane->SetDoubleBuffered(true);
    const int border_inset = em / 4;
    right_box_sizer->Add(m_presets_pane, 1, wxEXPAND | wxALL, border_inset);
    right_box_sizer->Add(m_editor_pane, 1, wxEXPAND | wxALL, border_inset);

    wxBoxSizer* edit_content_sizer = new wxBoxSizer(wxVERTICAL);
    m_editor_box                   = new wxPanel(m_editor_pane);
    wxBoxSizer* editor_sizer       = new wxBoxSizer(wxVERTICAL);

    m_editor_title = new wxStaticText(m_editor_box, wxID_ANY, _L("Virtual Extruder"));
    m_editor_title->SetFont(wxGetApp().bold_font());
    editor_sizer->Add(m_editor_title, 0, wxLEFT | wxRIGHT | wxTOP, pad);

    m_editor_description = new wxStaticText(m_editor_box, wxID_ANY, wxEmptyString);
    m_editor_description->SetFont(wxGetApp().small_font());
    m_editor_description->Wrap(40 * em);
    editor_sizer->Add(m_editor_description, 0, wxALL, pad);

    m_rows_sizer = new wxBoxSizer(wxVERTICAL);
    editor_sizer->Add(m_rows_sizer, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, pad);

    m_btn_add_component = new wxButton(m_editor_box, wxID_ANY, _L("Add component"));
    m_btn_add_component
        ->Bind(wxEVT_BUTTON, &FullSpectrumDialog::on_add_or_remove_component_clicked, this);
    editor_sizer->Add(m_btn_add_component, 0, wxLEFT | wxRIGHT | wxBOTTOM, pad);

    wxBoxSizer* color_row = new wxBoxSizer(wxHORIZONTAL);
    color_row->Add(
        new wxStaticText(m_editor_box, wxID_ANY, _L("Display color")),
        0,
        wxALIGN_CENTER_VERTICAL | wxRIGHT,
        gap
    );
    m_color_swatch =
        new wxPanel(m_editor_box, wxID_ANY, wxDefaultPosition, wxSize(10 * em, 3 * em));
    m_color_swatch->SetBackgroundStyle(wxBG_STYLE_PAINT);
    m_color_swatch->SetDoubleBuffered(true);
    m_color_swatch->SetCursor(wxCURSOR_HAND);
    m_color_swatch->Bind(
        wxEVT_PAINT,
        [this](wxPaintEvent&)
        {
            wxAutoBufferedPaintDC dc(m_color_swatch);
            const wxSize sz = m_color_swatch->GetClientSize();
            dc.SetBackground(wxBrush(m_color_swatch->GetParent()->GetBackgroundColour()));
            dc.Clear();
            dc.SetBrush(wxBrush(m_color_swatch->GetBackgroundColour()));
            dc.SetPen(wxPen(border_color_normal(), 1));
            dc.DrawRoundedRectangle(0, 0, sz.GetWidth(), sz.GetHeight(), 3);
        }
    );
    m_color_swatch->Bind(
        wxEVT_LEFT_DOWN,
        [this](wxMouseEvent&)
        {
            wxCommandEvent evt(wxEVT_BUTTON, m_color_swatch->GetId());
            on_color_click(evt);
        }
    );
    color_row->Add(m_color_swatch, 0, wxALIGN_CENTER_VERTICAL);
    editor_sizer->Add(color_row, 0, wxLEFT | wxRIGHT | wxBOTTOM, pad);

    m_permanent_ratio_title = new wxStaticText(m_editor_box, wxID_ANY, _L("Mix ratio"));
    m_permanent_ratio_title->SetFont(wxGetApp().small_font());
    editor_sizer->Add(m_permanent_ratio_title, 0, wxLEFT | wxRIGHT | wxTOP, pad);

    m_permanent_ratio_bar = new GradientRatioBar(m_editor_box);
    m_permanent_ratio_bar->Bind(wxEVT_SLIDER, &FullSpectrumDialog::on_ratio_bar_changed, this);
    m_permanent_ratio_bar->SetToolTip(
        _L("Drag the handle to adjust the mix ratio. "
           "More % = more layers of that filament per cycle.")
    );
    editor_sizer->Add(m_permanent_ratio_bar, 0, wxLEFT | wxRIGHT | wxTOP | wxEXPAND, pad);

    m_permanent_ternary_picker = new BarycentricRatioPicker(m_editor_box);
    m_permanent_ternary_picker->Bind(wxEVT_SLIDER, &FullSpectrumDialog::on_triangle_changed, this);
    m_permanent_ternary_picker->SetToolTip(
        _L("Drag the handle to set the share of each filament. "
           "Closer to a corner = more of that filament.")
    );
    editor_sizer->Add(m_permanent_ternary_picker, 0, wxLEFT | wxRIGHT | wxTOP | wxEXPAND, pad);

    wxStaticText* seq_title =
        new wxStaticText(m_editor_box, wxID_ANY, _L("Layer sequence preview"));
    seq_title->SetFont(wxGetApp().small_font());
    editor_sizer->Add(seq_title, 0, wxLEFT | wxRIGHT | wxTOP, pad);

    m_sequence_bar = new LayerSequenceBar(m_editor_box);
    m_sequence_bar->SetToolTip(
        _L("Each cell is one print layer. "
           "The pattern shows how filaments alternate.")
    );
    editor_sizer->Add(m_sequence_bar, 0, wxALL | wxEXPAND, pad);

    m_editor_box->SetSizer(editor_sizer);
    edit_content_sizer->Add(m_editor_box, 1, wxEXPAND);
    m_editor_pane->SetSizer(edit_content_sizer);

    wxBoxSizer* presets_tab_sizer = new wxBoxSizer(wxVERTICAL);

    const wxString intro_str =
        _L("A virtual extruder blends physical filaments layer by layer to create "
           "new colors. Click a blend below to add it to your project.");
    wxStaticText* intro_text = new wxStaticText(m_presets_pane, wxID_ANY, intro_str);
    intro_text->SetFont(wxGetApp().normal_font());
    intro_text->Wrap(40 * em);
    intro_text->Bind(
        wxEVT_SIZE,
        [this, intro_text, intro_str](wxSizeEvent& e)
        {
            if (!m_intro_wrap_guard) {
                m_intro_wrap_guard = true;
                intro_text->SetLabel(intro_str);
                intro_text->Wrap(e.GetSize().GetWidth());
                m_intro_wrap_guard = false;
            }
            e.Skip();
        }
    );
    presets_tab_sizer->Add(intro_text, 0, wxALL | wxEXPAND, pad);

    if (m_num_physical > 0) {
        wxStaticText* filter_label = new wxStaticText(
            m_presets_pane,
            wxID_ANY,
            _L("Toggle extruders to filter the blends shown below")
        );
        filter_label->SetFont(wxGetApp().small_font());
        presets_tab_sizer->Add(filter_label, 0, wxLEFT | wxRIGHT | wxTOP, pad);

        m_extruder_filter_wrap = new wxWrapSizer(wxHORIZONTAL);
        const wxSize chip_size = FromDIP(wxSize(28, 28));
        m_preset_filter_buttons.reserve(m_num_physical);
        for (unsigned int i = 0; i < m_num_physical; ++i) {
            auto chip_bmp = std::make_shared<wxBitmap>(
                render_filter_chip_bitmap(i + 1, m_physical_colors[i], /*enabled=*/true, chip_size)
            );
            auto hovered = std::make_shared<bool>(false);

            wxPanel* btn = new wxPanel(
                m_presets_pane,
                wxID_ANY,
                wxDefaultPosition,
                chip_size + FromDIP(wxSize(4, 4)),
                wxBORDER_NONE
            );
            btn->SetMinSize(chip_size + FromDIP(wxSize(4, 4)));
            btn->SetDoubleBuffered(true);
            btn->SetBackgroundStyle(wxBG_STYLE_PAINT);
            btn->SetCursor(wxCursor(wxCURSOR_HAND));
            btn->SetToolTip(
                wxString::Format(_L("Toggle extruder %u for preset generation"), i + 1)
            );

            const int chip_pad = (btn->GetMinSize().GetWidth() - chip_size.GetWidth()) / 2;
            btn->Bind(
                wxEVT_ENTER_WINDOW,
                [btn, hovered](wxMouseEvent& e)
                {
                    *hovered = true;
                    btn->Refresh();
                    e.Skip();
                }
            );
            btn->Bind(
                wxEVT_LEAVE_WINDOW,
                [btn, hovered](wxMouseEvent& e)
                {
                    *hovered = false;
                    btn->Refresh();
                    e.Skip();
                }
            );
            btn->Bind(
                wxEVT_PAINT,
                [btn, chip_bmp, hovered, chip_pad](wxPaintEvent&)
                {
                    wxAutoBufferedPaintDC dc(btn);
                    dc.SetBackground(wxBrush(btn->GetParent()->GetBackgroundColour()));
                    dc.Clear();
                    if (chip_bmp->IsOk())
                        dc.DrawBitmap(*chip_bmp, chip_pad, chip_pad, true);
                    if (*hovered) {
                        const int w = btn->GetClientSize().GetWidth();
                        const int h = btn->GetClientSize().GetHeight();
                        dc.SetBrush(*wxTRANSPARENT_BRUSH);
                        dc.SetPen(wxPen(wxColour(0xED, 0x6B, 0x21), 1));
                        dc.DrawRoundedRectangle(0, 0, w, h, 4);
                    }
                }
            );

            const unsigned int idx = i;
            btn->Bind(
                wxEVT_LEFT_DOWN,
                [this, idx, chip_size, chip_bmp](wxMouseEvent&)
                {
                    m_preset_extruders_enabled[idx] = !m_preset_extruders_enabled[idx];
                    *chip_bmp                       = render_filter_chip_bitmap(
                        idx + 1,
                        m_physical_colors[idx],
                        m_preset_extruders_enabled[idx],
                        chip_size
                    );
                    m_preset_filter_buttons[idx]->Refresh();
                    rebuild_preset_grid();
                }
            );

            m_preset_filter_buttons.push_back(btn);
            m_extruder_filter_wrap->Add(btn, 0, wxALL, FromDIP(2));
        }
        presets_tab_sizer
            ->Add(m_extruder_filter_wrap, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, pad);
    }

    m_presets_scrolled_window = new wxScrolledWindow(
        m_presets_pane,
        wxID_ANY,
        wxDefaultPosition,
        wxDefaultSize,
        wxVSCROLL | wxBORDER_SIMPLE
    );
    m_presets_scrolled_window->SetScrollRate(0, 10);
    m_presets_scrolled_window->SetBackgroundColour(window_bg_color());
    m_presets_scrolled_window->SetBackgroundStyle(wxBG_STYLE_PAINT);
    m_presets_scrolled_window->Bind(
        wxEVT_PAINT,
        [this](wxPaintEvent&)
        {
            wxAutoBufferedPaintDC dc(m_presets_scrolled_window);
            m_presets_scrolled_window->PrepareDC(dc);
            dc.SetBackground(wxBrush(m_presets_scrolled_window->GetBackgroundColour()));
            dc.Clear();
        }
    );
    m_presets_scrolled_window->Bind(
        wxEVT_SIZE,
        [this](wxSizeEvent& e)
        {
            m_presets_scrolled_window->FitInside();
            e.Skip();
        }
    );

    wxBoxSizer* presets_content_sizer = new wxBoxSizer(wxVERTICAL);

    m_two_color_presets_title =
        new wxStaticText(m_presets_scrolled_window, wxID_ANY, _L("Two-color blends"));
    m_two_color_presets_title->SetFont(wxGetApp().bold_font());
    presets_content_sizer->Add(m_two_color_presets_title, 0, wxALL, pad);
    m_two_color_presets_wrap = new wxWrapSizer(wxHORIZONTAL);
    presets_content_sizer->Add(m_two_color_presets_wrap, 0, wxEXPAND | wxLEFT | wxRIGHT, pad);

    presets_content_sizer->AddSpacer(pad * 2);

    m_three_color_presets_title =
        new wxStaticText(m_presets_scrolled_window, wxID_ANY, _L("Three-color blends"));
    m_three_color_presets_title->SetFont(wxGetApp().bold_font());
    presets_content_sizer->Add(m_three_color_presets_title, 0, wxLEFT | wxRIGHT | wxBOTTOM, pad);
    m_three_color_presets_wrap = new wxWrapSizer(wxHORIZONTAL);
    presets_content_sizer->Add(m_three_color_presets_wrap, 0, wxEXPAND | wxLEFT | wxRIGHT, pad);

    m_presets_empty_hint = new wxStaticText(
        m_presets_scrolled_window,
        wxID_ANY,
        _L("No combinations available. Presets pair up filaments of the same "
           "type (e.g. PLA + PLA); adjust filament types in the printer preset "
           "to see more suggestions.")
    );
    m_presets_empty_hint->SetFont(wxGetApp().small_font());
    m_presets_empty_hint->Wrap(40 * em);
    m_presets_empty_hint->Hide();
    presets_content_sizer->Add(m_presets_empty_hint, 0, wxALL, pad);

    m_presets_scrolled_window->SetSizer(presets_content_sizer);
    presets_tab_sizer->Add(m_presets_scrolled_window, 1, wxEXPAND | wxALL, pad);

    m_presets_pane->SetSizer(presets_tab_sizer);

    right_box->SetSizer(right_box_sizer);
    split_sizer->Add(right_box, 1, wxTOP | wxBOTTOM | wxRIGHT | wxEXPAND, pad);

    main_sizer->Add(split_sizer, 1, wxEXPAND);

    wxStdDialogButtonSizer* btn_sizer = this->CreateStdDialogButtonSizer(wxOK | wxCANCEL);
    main_sizer->Add(btn_sizer, 0, wxEXPAND | wxALL, pad);

    this->Bind(wxEVT_CLOSE_WINDOW, [this](wxCloseEvent&) { this->EndModal(wxID_CANCEL); });

    auto background_deselect = [this](wxMouseEvent& event)
    {
        if (m_virtual_extruder_list && m_virtual_extruder_list->GetMySelection() != wxNOT_FOUND) {
            m_virtual_extruder_list->SetMySelection(wxNOT_FOUND);
            wxCommandEvent e(wxEVT_LISTBOX, m_virtual_extruder_list->GetId());
            e.SetEventObject(m_virtual_extruder_list);
            e.SetInt(wxNOT_FOUND);
            m_virtual_extruder_list->ProcessWindowEvent(e);
        }
        event.Skip();
    };
    this->Bind(wxEVT_LEFT_DOWN, background_deselect);
    left_box->Bind(wxEVT_LEFT_DOWN, background_deselect);
    m_right_panel->Bind(wxEVT_LEFT_DOWN, background_deselect);
    m_editor_pane->Bind(wxEVT_LEFT_DOWN, background_deselect);

    this->SetSizer(main_sizer);

    const bool can_add = (m_num_physical >= 2);
    m_btn_add_blend->Enable(can_add);
    m_btn_add_gradient->Enable(can_add);
    update_right_panel_visibility();

    rebuild_preset_grid();
}

void FullSpectrumDialog::refresh_virtual_extruder_list(int select_index)
{
    std::vector<VirtualExtruderListBox::Row> rows;
    rows.reserve(m_working_list.size());
    for (const FullSpectrum::VirtualExtruder& ve : m_working_list) {
        VirtualExtruderListBox::Row row;
        row.title = make_list_title(ve);
        if (ve.type() == VirtualExtruder::Type::Gradient) {
            row.is_gradient = true;
            row.subtitle    = _L("Gradient");
            row.gradient_stops.reserve(ve.gradient->stops.size());
            for (const FullSpectrum::VirtualExtruderGradientStop& stop : ve.gradient->stops) {
                const wxColour color = physical_color(stop.extruder_id);
                row.gradient_stops.emplace_back(color, stop.position);
                row.blend_components.push_back({color, stop.extruder_id, -1});
            }
            row.swatch = row.gradient_stops.front().first;
        } else {
            row.swatch = parse_hex_color(current_effective_color_for(ve));
            row.blend_components.reserve(ve.components.size());
            for (const FullSpectrum::VirtualExtruderComponent& component : ve.components) {
                const wxColour color = physical_color(component.extruder_id);
                const int percent    = int(std::round(component.ratio * 100.0));
                row.blend_components.push_back({color, component.extruder_id, percent});
            }
        }
        rows.push_back(std::move(row));
    }
    m_virtual_extruder_list->SetRows(std::move(rows));

    if (select_index >= 0 && select_index < int(m_working_list.size())) {
        m_virtual_extruder_list->SetMySelection(select_index);
        populate_editor_from_selection();
    } else {
        m_virtual_extruder_list->SetMySelection(wxNOT_FOUND);
        update_right_panel_visibility();
    }
}

void FullSpectrumDialog::populate_editor_from_selection()
{
    update_right_panel_visibility();
    const int selection = selected_index();
    if (selection < 0)
        return;

    const FullSpectrum::VirtualExtruder& ve = m_working_list[selection];
    m_editor_title->SetLabel(wxString::Format(_L("Virtual extruder #%u"), ve.id));

    if (ve.type() == VirtualExtruder::Type::Gradient) {
        m_btn_add_component->SetLabel(_L("Add color"));
        m_btn_add_component->SetToolTip(_L("Add another transition point along the gradient."));
        m_color_swatch->Enable(true);
        m_color_swatch->SetToolTip(
            _L("Preview of the blended color. Click to pick a custom display color. "
               "Resets automatically when you change the composition.")
        );
        m_editor_description->SetLabel(
            _L("Set the filament at each point along the gradient. "
               "Position 0 is the bottom, 1 is the top. "
               "The gradient range is determined automatically from the assigned area.")
        );
        m_editor_description->Wrap(40 * wxGetApp().em_unit());
        m_permanent_ratio_title->Show(false);
        m_permanent_ratio_bar->Show(false);
        m_permanent_ternary_picker->Show(false);

        rebuild_stop_rows();
    } else {
        m_color_swatch->Enable(true);
        m_color_swatch->SetToolTip(
            _L("Preview of the blended color. Click to pick a custom display color. "
               "Resets automatically when you change the composition.")
        );
        if (ve.components.size() == 2) {
            m_editor_description->SetLabel(_L(
                "Drag the slider to set how many layers each filament gets in the repeating pattern."
            ));
        } else if (ve.components.size() == 3) {
            m_editor_description->SetLabel(
                _L("Drag the handle inside the triangle to set the share of each filament.")
            );
        } else {
            m_editor_description->SetLabel(_L(
                "Each component's weight controls how many layers it gets in the repeating pattern."
            ));
        }
        m_editor_description->Wrap(40 * wxGetApp().em_unit());

        rebuild_component_rows();

        if (ve.components.size() == 2) {
            const unsigned int ext_a = ve.components[0].extruder_id;
            const unsigned int ext_b = ve.components[1].extruder_id;
            const int ratio_a_percent =
                std::clamp(int(std::round(ve.components[0].ratio * 100.0)), 1, 99);
            GradientRatioBar* bar =
                m_inline_blend_ratio_bar ? m_inline_blend_ratio_bar : m_permanent_ratio_bar;
            bar->SetColors(physical_color(ext_a), physical_color(ext_b));
            bar->SetRatioAPercent(ratio_a_percent);
        }
    }

    update_preview_and_validation();
}

void FullSpectrumDialog::rebuild_component_rows()
{
    wxWindowUpdateLocker freeze_lock(m_editor_box);

    m_rows_sizer->Clear(/*delete_windows=*/true);
    m_component_rows.clear();
    m_stop_rows.clear();
    m_inline_blend_ratio_bar = nullptr;
    m_inline_ternary_picker  = nullptr;

    const int selection = selected_index();
    if (selection < 0) {
        m_editor_box->Layout();
        return;
    }

    const FullSpectrum::VirtualExtruder& ve = m_working_list[selection];

    const int em  = wxGetApp().em_unit();
    const int gap = em;

    const size_t component_count  = ve.components.size();
    const bool is_three_component = (component_count == 3);
    const bool is_two_component   = (component_count == 2);

    if (is_two_component) {
        auto make_dropdown = [&](size_t i) -> BitmapComboBox*
        {
            unsigned int ext_id = ve.components[i].extruder_id;
            if (ext_id == 0 || ext_id > m_num_physical)
                ext_id = 1;
            auto* combo = new BitmapComboBox(
                m_editor_box,
                wxID_ANY,
                wxEmptyString,
                wxDefaultPosition,
                wxSize(6 * em, -1),
                0,
                nullptr,
                wxCB_READONLY
            );
            populate_extruder_choices(combo);
            combo->SetSelection(int(ext_id) - 1);
            combo->Bind(wxEVT_COMBOBOX, &FullSpectrumDialog::on_component_changed, this);
            return combo;
        };

        BitmapComboBox* combo_a = make_dropdown(0);
        BitmapComboBox* combo_b = make_dropdown(1);

        auto* inline_bar = new GradientRatioBar(m_editor_box);
        inline_bar->SetMinSize(wxSize(10 * em, 28));
        inline_bar->Bind(wxEVT_SLIDER, &FullSpectrumDialog::on_ratio_bar_changed, this);
        inline_bar->SetToolTip(
            _L("Drag the handle to adjust the mix ratio. "
               "More % = more layers of that filament per cycle.")
        );
        m_inline_blend_ratio_bar = inline_bar;

        wxBoxSizer* inline_sizer = new wxBoxSizer(wxHORIZONTAL);
        inline_sizer->Add(combo_a, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, gap);
        inline_sizer->Add(inline_bar, 1, wxALIGN_CENTER_VERTICAL);
        inline_sizer->Add(combo_b, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, gap);
        m_rows_sizer->Add(inline_sizer, 0, wxEXPAND | wxBOTTOM, gap / 2);

        auto* pct_a = new wxStaticText(
            m_editor_box,
            wxID_ANY,
            wxEmptyString,
            wxDefaultPosition,
            wxSize(6 * em, -1),
            wxALIGN_CENTRE_HORIZONTAL
        );
        auto* pct_b = new wxStaticText(
            m_editor_box,
            wxID_ANY,
            wxEmptyString,
            wxDefaultPosition,
            wxSize(6 * em, -1),
            wxALIGN_CENTRE_HORIZONTAL
        );
        pct_a->SetFont(wxGetApp().small_font());
        pct_b->SetFont(wxGetApp().small_font());
        wxBoxSizer* pct_sizer = new wxBoxSizer(wxHORIZONTAL);
        pct_sizer->Add(pct_a, 0, wxRIGHT, gap);
        pct_sizer->AddStretchSpacer(1);
        pct_sizer->Add(pct_b, 0, wxLEFT, gap);
        m_rows_sizer->Add(pct_sizer, 0, wxEXPAND);

        ComponentRow row_a;
        row_a.extruder     = combo_a;
        row_a.weight_label = pct_a;
        m_component_rows.push_back(row_a);
        ComponentRow row_b;
        row_b.extruder     = combo_b;
        row_b.weight_label = pct_b;
        m_component_rows.push_back(row_b);
    } else if (is_three_component) {
        auto make_dropdown = [&](size_t i) -> BitmapComboBox*
        {
            unsigned int ext_id = ve.components[i].extruder_id;
            if (ext_id == 0 || ext_id > m_num_physical)
                ext_id = 1;
            auto* combo = new BitmapComboBox(
                m_editor_box,
                wxID_ANY,
                wxEmptyString,
                wxDefaultPosition,
                wxSize(6 * em, -1),
                0,
                nullptr,
                wxCB_READONLY
            );
            populate_extruder_choices(combo);
            combo->SetSelection(int(ext_id) - 1);
            combo->Bind(wxEVT_COMBOBOX, &FullSpectrumDialog::on_component_changed, this);
            return combo;
        };

        BitmapComboBox* combo_0 = make_dropdown(0);
        BitmapComboBox* combo_1 = make_dropdown(1);
        BitmapComboBox* combo_2 = make_dropdown(2);

        auto* pct_0 = new wxStaticText(
            m_editor_box,
            wxID_ANY,
            wxEmptyString,
            wxDefaultPosition,
            wxSize(6 * em, -1),
            wxALIGN_CENTRE_HORIZONTAL
        );
        pct_0->SetFont(wxGetApp().small_font());
        m_rows_sizer->Add(pct_0, 0, wxALIGN_CENTER_HORIZONTAL | wxBOTTOM, gap / 4);
        m_rows_sizer->Add(combo_0, 0, wxALIGN_CENTER_HORIZONTAL | wxBOTTOM, gap / 2);

        const int tri_h          = 18 * em;
        const int tri_w          = tri_h + 6 * em;
        constexpr int tri_margin = 12;
        auto* tri                = new BarycentricRatioPicker(m_editor_box);
        tri->SetMinSize(wxSize(tri_w, tri_h));
        tri->Bind(wxEVT_SLIDER, &FullSpectrumDialog::on_triangle_changed, this);
        tri->SetToolTip(
            _L("Drag the handle to set the share of each filament. "
               "Closer to a corner = more of that filament.")
        );
        m_inline_ternary_picker = tri;
        m_rows_sizer->Add(tri, 0, wxALIGN_CENTER_HORIZONTAL | wxBOTTOM, gap / 2);

        auto* pct_1 = new wxStaticText(
            m_editor_box,
            wxID_ANY,
            wxEmptyString,
            wxDefaultPosition,
            wxSize(6 * em, -1),
            wxALIGN_CENTRE_HORIZONTAL
        );
        auto* pct_2 = new wxStaticText(
            m_editor_box,
            wxID_ANY,
            wxEmptyString,
            wxDefaultPosition,
            wxSize(6 * em, -1),
            wxALIGN_CENTRE_HORIZONTAL
        );
        pct_1->SetFont(wxGetApp().small_font());
        pct_2->SetFont(wxGetApp().small_font());

        wxBoxSizer* bottom_combo_sizer = new wxBoxSizer(wxHORIZONTAL);
        bottom_combo_sizer->AddSpacer(tri_margin);
        bottom_combo_sizer->Add(combo_1, 0);
        bottom_combo_sizer->AddStretchSpacer(1);
        bottom_combo_sizer->Add(combo_2, 0);
        bottom_combo_sizer->AddSpacer(tri_margin);
        bottom_combo_sizer->SetMinSize(wxSize(tri_w, -1));
        m_rows_sizer->Add(bottom_combo_sizer, 0, wxALIGN_CENTER_HORIZONTAL | wxBOTTOM, gap / 4);

        wxBoxSizer* bottom_pct_sizer = new wxBoxSizer(wxHORIZONTAL);
        bottom_pct_sizer->AddSpacer(tri_margin);
        bottom_pct_sizer->Add(pct_1, 0);
        bottom_pct_sizer->AddStretchSpacer(1);
        bottom_pct_sizer->Add(pct_2, 0);
        bottom_pct_sizer->AddSpacer(tri_margin);
        bottom_pct_sizer->SetMinSize(wxSize(tri_w, -1));
        m_rows_sizer->Add(bottom_pct_sizer, 0, wxALIGN_CENTER_HORIZONTAL);

        ComponentRow row_0;
        row_0.extruder     = combo_0;
        row_0.weight_label = pct_0;
        ComponentRow row_1;
        row_1.extruder     = combo_1;
        row_1.weight_label = pct_1;
        ComponentRow row_2;
        row_2.extruder     = combo_2;
        row_2.weight_label = pct_2;
        m_component_rows.push_back(row_0);
        m_component_rows.push_back(row_1);
        m_component_rows.push_back(row_2);
    } else {
        auto make_row = [&](size_t i, unsigned int ext_id, const wxString& label_text)
        {
            ComponentRow row;
            row.row_sizer = new wxBoxSizer(wxHORIZONTAL);

            row.label = new wxStaticText(
                m_editor_box,
                wxID_ANY,
                label_text,
                wxDefaultPosition,
                wxSize(10 * em, -1)
            );

            row.extruder = new BitmapComboBox(
                m_editor_box,
                wxID_ANY,
                wxEmptyString,
                wxDefaultPosition,
                wxSize(6 * em, -1),
                0,
                nullptr,
                wxCB_READONLY
            );
            populate_extruder_choices(row.extruder);
            if (ext_id == 0 || ext_id > m_num_physical)
                ext_id = 1;
            row.extruder->SetSelection(int(ext_id) - 1);
            row.extruder->Bind(wxEVT_COMBOBOX, &FullSpectrumDialog::on_component_changed, this);

            row.weight_label = new wxStaticText(
                m_editor_box,
                wxID_ANY,
                wxEmptyString,
                wxDefaultPosition,
                wxSize(7 * em, -1)
            );
            row.weight_label->SetFont(wxGetApp().small_font());

            row.remove_btn = new wxButton(
                m_editor_box,
                wxID_ANY,
                wxString::FromUTF8("\xE2\x88\x92"),
                wxDefaultPosition,
                wxSize(3 * em, -1)
            );
            const size_t captured_index = i;
            row.remove_btn->Bind(
                wxEVT_BUTTON,
                [this, captured_index](wxCommandEvent&) { on_remove_component(captured_index); }
            );
            row.remove_btn->Enable(component_count > 2);

            row.row_sizer->Add(row.label, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, gap);
            row.row_sizer->Add(row.extruder, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, gap);
            row.row_sizer->Add(row.weight_label, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, gap);
            row.row_sizer->AddStretchSpacer(1);
            row.row_sizer->Add(row.remove_btn, 0, wxALIGN_CENTER_VERTICAL);

            m_rows_sizer->Add(row.row_sizer, 0, wxEXPAND | wxBOTTOM, gap / 2);
            m_component_rows.push_back(row);
        };

        for (size_t i = 0; i < component_count; ++i)
            make_row(i, ve.components[i].extruder_id, wxString::Format(_L("Component %zu"), i + 1));
    }

    if (is_two_component) {
        m_btn_add_component->SetLabel(_L("Add third extruder"));
        m_btn_add_component->SetToolTip(
            _L("Add a third filament to create a three-way blend with the triangle mixer.")
        );
        m_btn_add_component->Enable(m_num_physical >= 3);
    } else if (is_three_component) {
        m_btn_add_component->SetLabel(_L("Remove third extruder"));
        m_btn_add_component->SetToolTip(
            _L("Remove the third filament and go back to the two-way slider mixer.")
        );
        m_btn_add_component->Enable(true);
    } else {
        m_btn_add_component->SetLabel(_L("Add component"));
        m_btn_add_component->SetToolTip(_L("Add another filament component to this blend."));
        m_btn_add_component->Enable(
            component_count
            < std::min<unsigned int>(unsigned(FullSpectrum::MAX_BLEND_COMPONENTS), m_num_physical)
        );
    }
    m_permanent_ratio_title->Show(false);
    m_permanent_ratio_bar->Show(false);
    m_permanent_ternary_picker->Show(false);

    if (is_three_component && m_inline_ternary_picker) {
        auto* tri         = m_inline_ternary_picker;
        auto vertex_color = [&](size_t i) -> wxColour
        { return physical_color(ve.components[i].extruder_id); };
        tri->SetColors(vertex_color(0), vertex_color(1), vertex_color(2));
        tri->SetWeights(ve.components[0].ratio, ve.components[1].ratio, ve.components[2].ratio);
        tri->SetVertexLabels(
            wxString::Format("%u", ve.components[0].extruder_id),
            wxString::Format("%u", ve.components[1].extruder_id),
            wxString::Format("%u", ve.components[2].extruder_id)
        );
    }

    if (m_editor_description) {
        if (is_two_component)
            m_editor_description->SetLabel(_L(
                "Drag the slider to set how many layers each filament gets in the repeating pattern."
            ));
        else if (is_three_component)
            m_editor_description->SetLabel(
                _L("Drag the handle inside the triangle to set the share of each filament.")
            );
        else
            m_editor_description->SetLabel(_L(
                "Each component's weight controls how many layers it gets in the repeating pattern."
            ));
        m_editor_description->Wrap(40 * wxGetApp().em_unit());
    }

    m_editor_box->Layout();

    this->Layout();
}

static wxBitmap
render_extruder_chip_bitmap(unsigned int extruder_id_1based, const std::string& hex_color)
{
    const int w = 28;
    const int h = 18;
    wxBitmap bmp(w, h);
    {
        wxMemoryDC dc(bmp);
        dc.SetBackground(wxBrush(parse_hex_color(hex_color)));
        dc.Clear();

        wxColour bg = parse_hex_color(hex_color);
        dc.SetTextForeground(wcag_contrast_text(bg));

        wxFont font = wxGetApp().bold_font();
        font.SetPointSize(std::max(7, font.GetPointSize() - 1));
        dc.SetFont(font);

        const wxString label = wxString::Format("%u", extruder_id_1based);
        const wxSize ext     = dc.GetTextExtent(label);
        dc.DrawText(label, (w - ext.GetWidth()) / 2, (h - ext.GetHeight()) / 2);
    }
    return bmp;
}

void FullSpectrumDialog::populate_extruder_choices(BitmapComboBox* choice) const
{
    choice->Clear();
    for (unsigned int i = 0; i < m_num_physical; ++i) {
        const wxBitmap chip = render_extruder_chip_bitmap(i + 1, m_physical_colors[i]);
        choice->Append(wxEmptyString, chip);
    }
}

void FullSpectrumDialog::rebuild_stop_rows()
{
    wxWindowUpdateLocker freeze_lock(m_editor_box);

    m_rows_sizer->Clear(/*delete_windows=*/true);
    m_component_rows.clear();
    m_stop_rows.clear();

    const int selection = selected_index();
    if (selection < 0) {
        m_editor_box->Layout();
        return;
    }

    const FullSpectrum::VirtualExtruder& ve = m_working_list[selection];
    if (!ve.gradient.has_value()) {
        m_editor_box->Layout();
        return;
    }

    const int em  = wxGetApp().em_unit();
    const int gap = em;

    const std::vector<FullSpectrum::VirtualExtruderGradientStop>& stops = ve.gradient->stops;
    const size_t n                                                      = stops.size();

    auto make_row = [&](size_t i, unsigned int ext_id, double position)
    {
        StopRow row;
        row.row_sizer = new wxBoxSizer(wxHORIZONTAL);

        row.label = new wxStaticText(
            m_editor_box,
            wxID_ANY,
            wxString::Format(_L("Color %zu"), i + 1),
            wxDefaultPosition,
            wxSize(10 * em, -1)
        );

        row.extruder = new BitmapComboBox(
            m_editor_box,
            wxID_ANY,
            wxEmptyString,
            wxDefaultPosition,
            wxSize(6 * em, -1),
            0,
            nullptr,
            wxCB_READONLY
        );
        populate_extruder_choices(row.extruder);
        if (ext_id == 0 || ext_id > m_num_physical)
            ext_id = 1;
        row.extruder->SetSelection(int(ext_id) - 1);
        row.extruder->Bind(wxEVT_COMBOBOX, &FullSpectrumDialog::on_stop_extruder_changed, this);

        row.position = new wxSpinCtrlDouble(
            m_editor_box,
            wxID_ANY,
            wxEmptyString,
            wxDefaultPosition,
            wxSize(7 * em, -1),
            wxSP_ARROW_KEYS,
            0.0,
            1.0,
            position,
            0.05
        );
        row.position->SetDigits(2);
        row.position->SetToolTip(_L("Position from 0.0 (bottom) to 1.0 (top)."));
        row.position
            ->Bind(wxEVT_SPINCTRLDOUBLE, &FullSpectrumDialog::on_stop_position_changed, this);

        row.remove_btn = new wxButton(
            m_editor_box,
            wxID_ANY,
            wxString::FromUTF8("\xE2\x88\x92"),
            wxDefaultPosition,
            wxSize(3 * em, -1)
        );
        const size_t captured_index = i;
        row.remove_btn->Bind(
            wxEVT_BUTTON,
            [this, captured_index](wxCommandEvent&) { on_remove_stop(captured_index); }
        );
        row.remove_btn->Enable(n > 2);

        row.row_sizer->Add(row.label, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, gap);
        row.row_sizer->Add(row.extruder, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, gap);
        row.row_sizer->Add(row.position, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, gap);
        row.row_sizer->AddStretchSpacer(1);
        row.row_sizer->Add(row.remove_btn, 0, wxALIGN_CENTER_VERTICAL);

        m_rows_sizer->Add(row.row_sizer, 0, wxEXPAND | wxBOTTOM, gap / 2);
        m_stop_rows.push_back(row);
    };

    for (size_t i = 0; i < n; ++i)
        make_row(i, stops[i].extruder_id, stops[i].position);

    m_btn_add_component->Enable(n < std::max<size_t>(2, size_t(m_num_physical)));

    m_editor_box->Layout();

    this->Layout();
}

void FullSpectrumDialog::update_preview_and_validation()
{
    const int selection = selected_index();
    if (selection < 0)
        return;

    FullSpectrum::VirtualExtruder& ve = m_working_list[selection];

    bool invalid = false;
    std::vector<wxColour> cycle_colors;

    if (ve.type() == VirtualExtruder::Type::Gradient) {
        if (!ve.gradient.has_value())
            ve.gradient = FullSpectrum::VirtualExtruderGradient{};

        const size_t n = m_stop_rows.size();
        ve.gradient->stops.clear();
        ve.gradient->stops.reserve(n);
        for (size_t i = 0; i < n; ++i) {
            const int sel             = m_stop_rows[i].extruder->GetSelection();
            const unsigned int ext_id = (sel >= 0) ? unsigned(sel + 1) : 1u;
            const double position     = m_stop_rows[i].position->GetValue();
            ve.gradient->stops.push_back({ext_id, position});
        }
        std::sort(
            ve.gradient->stops.begin(),
            ve.gradient->stops.end(),
            [](const FullSpectrum::VirtualExtruderGradientStop& a,
               const FullSpectrum::VirtualExtruderGradientStop& b)
            { return a.position < b.position; }
        );

        if (ve.gradient->stops.size() < 2) {
            invalid = true;
        } else {
            for (const auto& s : ve.gradient->stops)
                if (s.position < 0.0 || s.position > 1.0) {
                    invalid = true;
                    break;
                }
            if (!invalid)
                for (size_t i = 1; i < ve.gradient->stops.size(); ++i)
                    if (std::abs(
                            ve.gradient->stops[i].position - ve.gradient->stops[i - 1].position
                        )
                        < 1e-9)
                    {
                        invalid = true;
                        break;
                    }
        }

        if (!invalid) {
            const double z_lo              = ve.gradient->z_min.value_or(0.0);
            const double z_hi              = ve.gradient->z_max.value_or(1.0);
            constexpr size_t preview_count = 200;
            std::vector<double> synthetic_z(preview_count);
            const double span = z_hi - z_lo;
            for (size_t i = 0; i < preview_count; ++i)
                synthetic_z[i] = z_lo + span * (double(i) + 0.5) / double(preview_count);
            const std::vector<unsigned int> sequence =
                ve.resolve_gradient_with_ranges(synthetic_z, {{z_lo, z_hi}});
            cycle_colors.reserve(sequence.size());
            for (unsigned int slot : sequence)
                cycle_colors.push_back(physical_color(slot));
        }
    } else {
        const size_t n                = m_component_rows.size();
        const bool is_two_component   = (n == 2);
        const bool is_three_component = (n == 3);

        ve.components.clear();
        ve.components.reserve(n);
        for (size_t i = 0; i < n; ++i) {
            const int sel             = m_component_rows[i].extruder->GetSelection();
            const unsigned int ext_id = (sel >= 0) ? unsigned(sel + 1) : 1u;
            double ratio              = 0.0;
            if (is_three_component) {
                BarycentricRatioPicker* tri =
                    m_inline_ternary_picker ? m_inline_ternary_picker : m_permanent_ternary_picker;
                const double w0 = tri->weight_0();
                const double w1 = tri->weight_1();
                const double w2 = std::max(0.0, 1.0 - w0 - w1);
                ratio           = (i == 0) ? w0 : (i == 1) ? w1 : w2;
            } else if (is_two_component) {
                GradientRatioBar* bar =
                    m_inline_blend_ratio_bar ? m_inline_blend_ratio_bar : m_permanent_ratio_bar;
                const int ratio_a_percent = bar->GetRatioAPercent();
                ratio                     = (i == 0) ? double(ratio_a_percent) / 100.0 :
                                                       1.0 - double(ratio_a_percent) / 100.0;
            }
            ve.components.push_back({ext_id, ratio});
        }

        if (is_two_component) {
            const unsigned int ext_a = ve.components[0].extruder_id;
            const unsigned int ext_b = ve.components[1].extruder_id;
            GradientRatioBar* bar =
                m_inline_blend_ratio_bar ? m_inline_blend_ratio_bar : m_permanent_ratio_bar;
            bar->SetColors(physical_color(ext_a), physical_color(ext_b));
        }
        if (is_three_component) {
            BarycentricRatioPicker* tri =
                m_inline_ternary_picker ? m_inline_ternary_picker : m_permanent_ternary_picker;
            tri->SetColors(
                physical_color(ve.components[0].extruder_id),
                physical_color(ve.components[1].extruder_id),
                physical_color(ve.components[2].extruder_id)
            );
            tri->SetVertexLabels(
                wxString::Format("%u", ve.components[0].extruder_id),
                wxString::Format("%u", ve.components[1].extruder_id),
                wxString::Format("%u", ve.components[2].extruder_id)
            );
        }

        bool invalid_weights = false;
        {
            double sum_ratio = 0.0;
            for (const auto& c : ve.components)
                sum_ratio += c.ratio;
            invalid_weights = (sum_ratio <= 1e-9);
        }
        if (invalid_weights) {
            invalid = true;
        }

        if (!invalid) {
            const std::vector<unsigned int> sequence = ve.build_sequence();
            cycle_colors.reserve(sequence.size());
            for (unsigned int slot : sequence)
                cycle_colors.push_back(physical_color(slot));
        }

        for (size_t i = 0; i < n && i < m_component_rows.size(); ++i) {
            const int pct = int(std::round(ve.components[i].ratio * 100.0));
            if (m_component_rows[i].weight_label)
                m_component_rows[i].weight_label->SetLabel(wxString::Format("%d%%", pct));
        }
    }

    m_sequence_bar->SetCycle(std::move(cycle_colors));

    std::vector<VirtualExtruderListBox::Row> rows = m_virtual_extruder_list->rows();
    if (selection < int(rows.size())) {
        rows[selection].title       = make_list_title(ve);
        rows[selection].subtitle    = make_list_subtitle(ve);
        rows[selection].is_gradient = ve.type() == VirtualExtruder::Type::Gradient;
        rows[selection].gradient_stops.clear();
        if (ve.type() == VirtualExtruder::Type::Gradient
            && ve.gradient.has_value()
            && !ve.gradient->stops.empty())
        {
            rows[selection].subtitle = _L("Gradient");
            rows[selection].gradient_stops.reserve(ve.gradient->stops.size());
            rows[selection].blend_components.clear();
            for (const FullSpectrum::VirtualExtruderGradientStop& stop : ve.gradient->stops) {
                const wxColour color = physical_color(stop.extruder_id);
                rows[selection].gradient_stops.emplace_back(color, stop.position);
                rows[selection].blend_components.push_back({color, stop.extruder_id, -1});
            }
            rows[selection].swatch = rows[selection].gradient_stops.front().first;
        } else {
            rows[selection].swatch = parse_hex_color(current_effective_color_for(ve));
            rows[selection].blend_components.clear();
            rows[selection].blend_components.reserve(ve.components.size());
            for (const FullSpectrum::VirtualExtruderComponent& comp : ve.components) {
                const wxColour color = physical_color(comp.extruder_id);
                const int percent    = int(std::round(comp.ratio * 100.0));
                rows[selection].blend_components.push_back({color, comp.extruder_id, percent});
            }
        }
        m_virtual_extruder_list->SetRows(std::move(rows));
        m_virtual_extruder_list->SetMySelection(selection);
    }

    m_editor_title->SetLabel(wxString::Format(_L("Virtual extruder #%u"), ve.id));

    const std::string effective_color_hex = current_effective_color_for(ve);
    const wxColour new_color =
        ve.color.has_value() ? parse_hex_color(*ve.color) : parse_hex_color(effective_color_hex);
    if (m_color_swatch->GetBackgroundColour() != new_color) {
        m_color_swatch->SetBackgroundColour(new_color);
        m_color_swatch->Refresh();
    }
}

void FullSpectrumDialog::update_right_panel_visibility()
{
    const bool has_selection = selected_index() >= 0;
    if (!has_selection)
        rebuild_preset_grid();
    if (m_editor_pane)
        m_editor_pane->Show(has_selection);
    if (m_presets_pane)
        m_presets_pane->Show(!has_selection);
    if (m_right_sizer)
        m_right_sizer->Layout();
    if (m_right_panel)
        m_right_panel->Layout();
    if (m_right_panel)
        m_right_panel->Refresh();

    if (!has_selection) {
        if (m_editor_title)
            m_editor_title->SetLabel(_L("Virtual Extruder"));
        if (m_editor_description)
            m_editor_description->SetLabel(wxEmptyString);
        if (m_color_swatch) {
            m_color_swatch->SetBackgroundColour(parse_hex_color("#808080"));
            m_color_swatch->Refresh();
        }
        if (m_sequence_bar)
            m_sequence_bar->SetCycle({});
    }
}

void FullSpectrumDialog::on_list_selected(wxCommandEvent&)
{
    populate_editor_from_selection();
}

void FullSpectrumDialog::on_add_blend_clicked(wxCommandEvent&)
{
    if (m_num_physical < 2)
        return;

    FullSpectrum::VirtualExtruder ve;
    ve.id = next_free_virtual_id();
    ve.components.push_back({1u, 0.5});
    ve.components.push_back({2u, 0.5});
    m_working_list.push_back(std::move(ve));
    refresh_virtual_extruder_list(int(m_working_list.size()) - 1);
}

void FullSpectrumDialog::on_remove_recipe_clicked(wxCommandEvent&)
{
    const int selection = selected_index();
    if (selection < 0)
        return;

    const unsigned int removed_id = m_working_list[selection].id;
    const int users               = count_objects_using(removed_id);
    if (users > 0) {
        const wxString msg = format_wxstr(
            _L("Virtual extruder %1% is assigned to %2% object(s) or modifier(s). "
               "Removing it will reset their extruder to default. Continue?"),
            removed_id,
            users
        );
        if (wxMessageBox(msg, _L("Remove virtual extruder"), wxYES_NO | wxICON_QUESTION, this)
            != wxYES)
            return;
    }

    m_working_list.erase(m_working_list.begin() + selection);
    if (std::find(m_original_ids.begin(), m_original_ids.end(), removed_id) != m_original_ids.end())
        m_removed_ids.push_back(removed_id);

    const int new_selection = std::min<int>(selection, int(m_working_list.size()) - 1);
    refresh_virtual_extruder_list(new_selection);
}

void FullSpectrumDialog::on_component_changed(wxCommandEvent&)
{
    clear_color_override();
    update_preview_and_validation();
}

void FullSpectrumDialog::on_ratio_bar_changed(wxCommandEvent&)
{
    clear_color_override();
    update_preview_and_validation();
}

void FullSpectrumDialog::on_color_click(wxCommandEvent&)
{
    const int selection = selected_index();
    if (selection < 0)
        return;
    FullSpectrum::VirtualExtruder& ve = m_working_list[selection];

    wxColourData color_data;
    color_data.SetChooseFull(true);
    color_data.SetColour(parse_hex_color(current_effective_color_for(ve)));
    wxColourDialog color_dialog(this, &color_data);
    if (color_dialog.ShowModal() != wxID_OK)
        return;

    const wxColour picked = color_dialog.GetColourData().GetColour();
    ve.color              = wxcolour_to_hex(picked);
    update_preview_and_validation();
}

void FullSpectrumDialog::on_add_or_remove_component_clicked(wxCommandEvent&)
{
    const int selection = selected_index();
    if (selection < 0)
        return;
    FullSpectrum::VirtualExtruder& ve = m_working_list[selection];

    if (ve.type() == VirtualExtruder::Type::Gradient) {
        on_add_stop();
        return;
    }

    if (ve.components.size() == 3) {
        on_remove_component(2);
        return;
    }

    const size_t cap = std::min<size_t>(FullSpectrum::MAX_BLEND_COMPONENTS, m_num_physical);
    if (ve.components.size() >= cap)
        return;

    const unsigned int next_id = pick_unused_physical_id(ve);
    const double even_ratio    = 1.0 / double(ve.components.size() + 1);
    for (auto& c : ve.components)
        c.ratio = even_ratio;
    ve.components.push_back({next_id, even_ratio});
    ve.color.reset();

    rebuild_component_rows();
    update_preview_and_validation();
}

void FullSpectrumDialog::on_remove_component(size_t row_index)
{
    const int selection = selected_index();
    if (selection < 0)
        return;
    FullSpectrum::VirtualExtruder& ve = m_working_list[selection];

    if (ve.components.size() <= 2 || row_index >= ve.components.size())
        return;
    ve.components.erase(ve.components.begin() + row_index);
    double sum = 0.0;
    for (const auto& c : ve.components)
        sum += c.ratio;
    if (sum > 0.0)
        for (auto& c : ve.components)
            c.ratio /= sum;
    ve.color.reset();

    rebuild_component_rows();
    update_preview_and_validation();
}

void FullSpectrumDialog::on_triangle_changed(wxCommandEvent&)
{
    clear_color_override();
    update_preview_and_validation();
}

void FullSpectrumDialog::on_add_gradient(wxCommandEvent&)
{
    if (m_num_physical < 2)
        return;

    FullSpectrum::VirtualExtruder ve;
    ve.id                = next_free_virtual_id();
    double default_z_max = 50.0;
    for (const ModelObject* object : m_model.objects)
        if (object != nullptr) {
            const auto bbox_z = object->raw_mesh_bounding_box().size().z();
            if (std::isfinite(bbox_z) && bbox_z > 0.0)
                default_z_max = std::max(default_z_max, bbox_z);
        }
    FullSpectrum::VirtualExtruderGradient gradient;
    gradient.stops.push_back({1u, 0.0});
    gradient.stops.push_back({2u, 1.0});
    ve.gradient = std::move(gradient);

    m_working_list.push_back(std::move(ve));
    refresh_virtual_extruder_list(int(m_working_list.size()) - 1);
}

void FullSpectrumDialog::on_add_stop()
{
    const int selection = selected_index();
    if (selection < 0)
        return;
    FullSpectrum::VirtualExtruder& ve = m_working_list[selection];
    if (ve.type() == VirtualExtruder::Type::Blend || !ve.gradient.has_value())
        return;

    auto& stops         = ve.gradient->stops;
    double new_position = 0.5;
    if (stops.size() >= 2) {
        std::vector<double> positions;
        positions.reserve(stops.size());
        for (const auto& s : stops)
            positions.push_back(s.position);
        std::sort(positions.begin(), positions.end());
        double widest_gap = 0.0;
        for (size_t i = 1; i < positions.size(); ++i) {
            const double gap = positions[i] - positions[i - 1];
            if (gap > widest_gap) {
                widest_gap   = gap;
                new_position = (positions[i] + positions[i - 1]) * 0.5;
            }
        }
    }

    const unsigned int next_id = pick_unused_physical_id_for_gradient(ve);
    stops.push_back({next_id, new_position});
    std::sort(
        stops.begin(),
        stops.end(),
        [](const FullSpectrum::VirtualExtruderGradientStop& a,
           const FullSpectrum::VirtualExtruderGradientStop& b) { return a.position < b.position; }
    );

    rebuild_stop_rows();
    update_preview_and_validation();
}

void FullSpectrumDialog::on_remove_stop(size_t row_index)
{
    const int selection = selected_index();
    if (selection < 0)
        return;
    FullSpectrum::VirtualExtruder& ve = m_working_list[selection];
    if (!ve.gradient.has_value())
        return;

    auto& stops = ve.gradient->stops;
    if (stops.size() <= 2 || row_index >= stops.size())
        return;
    stops.erase(stops.begin() + row_index);

    rebuild_stop_rows();
    update_preview_and_validation();
}

void FullSpectrumDialog::on_stop_extruder_changed(wxCommandEvent&)
{
    update_preview_and_validation();
}

void FullSpectrumDialog::on_stop_position_changed(wxCommandEvent&)
{
    update_preview_and_validation();
    CallAfter([this]() { rebuild_stop_rows(); });
}

unsigned int FullSpectrumDialog::next_free_virtual_id() const
{
    std::set<unsigned int> used;
    for (const FullSpectrum::VirtualExtruder& ve : m_working_list)
        used.insert(ve.id);
    unsigned int candidate = m_num_physical + 1;
    while (used.count(candidate) != 0)
        ++candidate;
    return candidate;
}

int FullSpectrumDialog::selected_index() const
{
    const int selection =
        m_virtual_extruder_list ? m_virtual_extruder_list->GetMySelection() : wxNOT_FOUND;
    if (selection == wxNOT_FOUND || selection < 0 || selection >= int(m_working_list.size()))
        return -1;
    return selection;
}

int FullSpectrumDialog::count_objects_using(unsigned int virtual_extruder_id) const
{
    int users = 0;
    for (const ModelObject* object : m_model.objects) {
        if (object == nullptr)
            continue;
        if (object->config.has("extruder") && object->config.extruder() == int(virtual_extruder_id))
            ++users;
        for (const ModelVolume* volume : object->volumes) {
            if (volume == nullptr)
                continue;
            if (volume->config.has("extruder")
                && volume->config.extruder() == int(virtual_extruder_id))
                ++users;
            if (volume->is_mm_painted()) {
                const auto& used_states = volume->mm_segmentation_facets.get_data().used_states;
                if (virtual_extruder_id < used_states.size() && used_states[virtual_extruder_id])
                    ++users;
            }
        }
    }
    return users;
}

wxColour FullSpectrumDialog::physical_color(unsigned int ext_id_1based) const
{
    if (ext_id_1based >= 1 && ext_id_1based <= m_physical_colors.size())
        return parse_hex_color(m_physical_colors[ext_id_1based - 1]);
    return wxColour(0x80, 0x80, 0x80);
}

void FullSpectrumDialog::clear_color_override()
{
    const int selection = selected_index();
    if (selection >= 0 && size_t(selection) < m_working_list.size())
        m_working_list[selection].color.reset();
}

std::string FullSpectrumDialog::current_effective_color_for(
    const FullSpectrum::VirtualExtruder& ve
) const
{
    const std::string blended = ve.effective_color(m_physical_colors);
    return blended.empty() ? std::string("#808080") : blended;
}

unsigned int FullSpectrumDialog::pick_unused_physical_id(
    const FullSpectrum::VirtualExtruder& ve
) const
{
    std::set<unsigned int> used;
    for (const auto& c : ve.components)
        used.insert(c.extruder_id);
    for (unsigned int candidate = 1; candidate <= m_num_physical; ++candidate)
        if (used.count(candidate) == 0)
            return candidate;
    return 1u;
}

unsigned int FullSpectrumDialog::pick_unused_physical_id_for_gradient(
    const FullSpectrum::VirtualExtruder& ve
) const
{
    if (!ve.gradient.has_value())
        return 1u;
    std::set<unsigned int> used;
    for (const auto& s : ve.gradient->stops)
        used.insert(s.extruder_id);
    for (unsigned int candidate = 1; candidate <= m_num_physical; ++candidate)
        if (used.count(candidate) == 0)
            return candidate;
    return 1u;
}

std::vector<FullSpectrumDialog::RecommendationPreset>
FullSpectrumDialog::build_two_color_presets() const
{
    auto enabled = [&](unsigned int ext_1based)
    {
        return ext_1based >= 1
            && ext_1based <= m_preset_extruders_enabled.size()
            && m_preset_extruders_enabled[ext_1based - 1];
    };
    std::vector<RecommendationPreset> out;
    for (unsigned int i = 1; i <= m_num_physical; ++i) {
        if (!enabled(i))
            continue;
        for (unsigned int j = i + 1; j <= m_num_physical; ++j) {
            if (!enabled(j))
                continue;
            const std::string& ti = m_physical_types[i - 1];
            const std::string& tj = m_physical_types[j - 1];
            if (ti.empty() || tj.empty() || ti != tj)
                continue;
            out.push_back({{i, j}, {50, 50}});
            out.push_back({{i, j}, {75, 25}});
            out.push_back({{i, j}, {25, 75}});
        }
    }
    return out;
}

std::vector<FullSpectrumDialog::RecommendationPreset>
FullSpectrumDialog::build_three_color_presets() const
{
    auto enabled = [&](unsigned int ext_1based)
    {
        return ext_1based >= 1
            && ext_1based <= m_preset_extruders_enabled.size()
            && m_preset_extruders_enabled[ext_1based - 1];
    };
    std::vector<RecommendationPreset> out;
    for (unsigned int i = 1; i <= m_num_physical; ++i) {
        if (!enabled(i))
            continue;
        for (unsigned int j = i + 1; j <= m_num_physical; ++j) {
            if (!enabled(j))
                continue;
            for (unsigned int k = j + 1; k <= m_num_physical; ++k) {
                if (!enabled(k))
                    continue;
                const std::string& ti = m_physical_types[i - 1];
                const std::string& tj = m_physical_types[j - 1];
                const std::string& tk = m_physical_types[k - 1];
                if (ti.empty() || tj.empty() || tk.empty() || ti != tj || tj != tk)
                    continue;
                for (int dominant = 0; dominant < 3; ++dominant) {
                    RecommendationPreset p;
                    p.extruder_ids         = {i, j, k};
                    p.ratios_pct           = {25, 25, 25};
                    p.ratios_pct[dominant] = 50;
                    out.push_back(std::move(p));
                    if (out.size() >= 200)
                        return out;
                }
            }
        }
    }
    return out;
}

static wxColour
blend_weighted(const std::vector<wxColour>& colors, const std::vector<int>& ratios_pct)
{
    const size_t n = std::min(colors.size(), ratios_pct.size());
    std::vector<prusa_fdm_mixer::Part> parts;
    parts.reserve(n);
    for (size_t i = 0; i < n; ++i) {
        if (ratios_pct[i] <= 0) {
            continue;
        }

        const std::string hex = wxcolour_to_hex(colors[i]);
        parts.push_back({hex, double(ratios_pct[i]) / 100.0});
    }
    if (parts.empty())
        return wxColour(0x80, 0x80, 0x80);
    try {
        const prusa_fdm_mixer::RGB rgb = prusa_fdm_mixer::mix_rgb(parts);
        return wxColour(rgb.r, rgb.g, rgb.b);
    } catch (...) {
        return wxColour(0x80, 0x80, 0x80);
    }
}

static wxBitmap render_preset_bitmap(
    const wxSize& size,
    const std::vector<wxColour>& colors,
    const std::vector<unsigned int>& extruder_ids,
    const std::vector<int>& ratios_pct
)
{
    const int w    = size.GetWidth();
    const int h    = size.GetHeight();
    const size_t n = std::min({colors.size(), extruder_ids.size(), ratios_pct.size()});

    wxBitmap bmp(w, h);
    wxMemoryDC mdc(bmp);
    const wxColour bg = blend_weighted(colors, ratios_pct);
    mdc.SetBackground(wxBrush(window_bg_color()));
    mdc.Clear();

    mdc.SetPen(*wxTRANSPARENT_PEN);
    mdc.SetBrush(wxBrush(bg));
    mdc.DrawRoundedRectangle(0, 0, w, h, 3);

    mdc.SetTextForeground(wcag_contrast_text(bg));

    wxString label;
    for (size_t i = 0; i < n; ++i) {
        if (i > 0)
            label += wxT("+");
        label += wxString::Format("%u", extruder_ids[i]);
    }

    wxFont font = wxGetApp().bold_font();
    font.SetPointSize(font.GetPointSize() + 1);
    mdc.SetFont(font);
    wxCoord tw = 0, th = 0;
    mdc.GetTextExtent(label, &tw, &th);
    mdc.DrawText(label, (w - tw) / 2, (h - th) / 2);

    mdc.SetPen(wxPen(border_color_normal(), 1));
    mdc.SetBrush(*wxTRANSPARENT_BRUSH);
    mdc.DrawRoundedRectangle(0, 0, w, h, 3);

    mdc.SelectObject(wxNullBitmap);
    return bmp;
}

static wxBitmap render_filter_chip_bitmap(
    unsigned int extruder_id_1based,
    const std::string& hex_color,
    bool enabled,
    const wxSize& size
)
{
    wxBitmap bmp(size);
    wxMemoryDC dc(bmp);
    const int w = size.GetWidth();
    const int h = size.GetHeight();

    const wxColour active_bg = parse_hex_color(hex_color);
    const wxColour bg        = enabled ?
               active_bg :
               wxColour(
            static_cast<unsigned char>((active_bg.Red() + 0xC0 * 2) / 3),
            static_cast<unsigned char>((active_bg.Green() + 0xC0 * 2) / 3),
            static_cast<unsigned char>((active_bg.Blue() + 0xC0 * 2) / 3)
        );

    dc.SetBackground(wxBrush(window_bg_color()));
    dc.Clear();
    dc.SetPen(*wxTRANSPARENT_PEN);
    dc.SetBrush(wxBrush(bg));
    dc.DrawRoundedRectangle(0, 0, w, h, 3);

    dc.SetTextForeground(wcag_contrast_text(bg));

    wxFont font = wxGetApp().bold_font();
    font.SetPointSize(std::max(8, font.GetPointSize()));
    dc.SetFont(font);
    const wxString label = wxString::Format("%u", extruder_id_1based);
    const wxSize ext     = dc.GetTextExtent(label);
    dc.DrawText(label, (w - ext.GetWidth()) / 2, (h - ext.GetHeight()) / 2);

    dc.SetBrush(*wxTRANSPARENT_BRUSH);
    dc.SetPen(wxPen(
        enabled ? border_color_normal() : border_color_subtle(),
        1,
        enabled ? wxPENSTYLE_SOLID : wxPENSTYLE_SHORT_DASH
    ));
    dc.DrawRoundedRectangle(0, 0, w, h, 3);

    if (!enabled) {
        dc.SetPen(wxPen(border_color_cross(), 2));
        dc.DrawLine(2, 2, w - 2, h - 2);
    }

    dc.SelectObject(wxNullBitmap);
    return bmp;
}

void FullSpectrumDialog::rebuild_preset_grid()
{
    if (m_two_color_presets_wrap == nullptr || m_three_color_presets_wrap == nullptr) {
        return;
    }

    wxWindowUpdateLocker freeze_lock(m_presets_scrolled_window);

    m_two_color_presets_wrap->Clear(/*delete_windows=*/true);
    m_three_color_presets_wrap->Clear(/*delete_windows=*/true);

    std::vector<RecommendationPreset> two_color   = build_two_color_presets();
    std::vector<RecommendationPreset> three_color = build_three_color_presets();

    constexpr double COMBO_DEDUPE_DELTA_E = 5.0;
    auto mix_hex_for_preset               = [this](const RecommendationPreset& p) -> std::string
    {
        std::vector<prusa_fdm_mixer::Part> parts;
        for (size_t i = 0; i < p.extruder_ids.size(); ++i) {
            const unsigned int ext = p.extruder_ids[i];
            if (ext >= 1 && ext <= m_physical_colors.size()) {
                parts.push_back({m_physical_colors[ext - 1], double(p.ratios_pct[i]) / 100.0});
            }
        }
        if (parts.empty())
            return "#808080";
        try {
            return prusa_fdm_mixer::mix(parts);
        } catch (...) {
            return "#808080";
        }
    };

    std::vector<RecommendationPreset> all_presets;
    all_presets.reserve(two_color.size() + three_color.size());
    for (auto& p : two_color) {
        all_presets.push_back(std::move(p));
    }

    for (auto& p : three_color) {
        all_presets.push_back(std::move(p));
    }

    struct AcceptedEntry
    {
        prusa_fdm_mixer::LAB lab;
        bool is_three;
    };

    std::vector<AcceptedEntry> accepted_labs;
    std::vector<RecommendationPreset> accepted_two, accepted_three;
    accepted_labs.reserve(all_presets.size());

    for (auto& preset : all_presets) {
        const std::string hex = mix_hex_for_preset(preset);
        prusa_fdm_mixer::LAB lab;
        try {
            lab = prusa_fdm_mixer::rgb_to_lab(prusa_fdm_mixer::hex_to_rgb(hex));
        } catch (...) {
            continue;
        }

        bool too_close = false;
        for (const auto& acc : accepted_labs) {
            if (prusa_fdm_mixer::delta_e_2000(lab, acc.lab) < COMBO_DEDUPE_DELTA_E) {
                too_close = true;
                break;
            }
        }
        if (too_close) {
            continue;
        }

        const bool is_three = (preset.extruder_ids.size() == 3);
        accepted_labs.push_back({lab, is_three});
        if (is_three) {
            accepted_three.push_back(std::move(preset));
        } else {
            accepted_two.push_back(std::move(preset));
        }
    }
    two_color   = std::move(accepted_two);
    three_color = std::move(accepted_three);

    auto preset_sort_key = [&](const RecommendationPreset& p) -> std::tuple<int, int, double>
    {
        const std::string hex = mix_hex_for_preset(p);
        prusa_fdm_mixer::LAB lab;
        try {
            lab = prusa_fdm_mixer::rgb_to_lab(prusa_fdm_mixer::hex_to_rgb(hex));
        } catch (...) {
            return {1, 0, 0.0};
        }
        const double C        = std::sqrt(lab.a * lab.a + lab.b * lab.b);
        const bool achromatic = C < 5.0;
        if (achromatic) {
            return {1, 0, lab.L};
        }

        double h = std::atan2(lab.b, lab.a) * 180.0 / M_PI;
        if (h < 0) {
            h += 360.0;
        }

        const int hue_bucket = int(h / 20.0);
        return {0, hue_bucket, lab.L};
    };
    auto preset_less = [&](const RecommendationPreset& a, const RecommendationPreset& b)
    { return preset_sort_key(a) < preset_sort_key(b); };
    std::sort(two_color.begin(), two_color.end(), preset_less);
    std::sort(three_color.begin(), three_color.end(), preset_less);

    auto preset_already_used = [this](const RecommendationPreset& preset) -> bool
    {
        for (const FullSpectrum::VirtualExtruder& ve : m_working_list) {
            if (ve.type() != FullSpectrum::VirtualExtruder::Type::Blend) {
                continue;
            }

            if (ve.components.size() != preset.extruder_ids.size()) {
                continue;
            }

            struct IdRatio
            {
                unsigned int id;
                double ratio;
            };
            auto by_id_then_ratio = [](const IdRatio& a, const IdRatio& b)
            { return a.id != b.id ? a.id < b.id : a.ratio < b.ratio; };

            std::vector<IdRatio> ve_sorted, preset_sorted;
            ve_sorted.reserve(ve.components.size());
            preset_sorted.reserve(preset.extruder_ids.size());

            for (const auto& c : ve.components)
                ve_sorted.push_back({c.extruder_id, c.ratio});
            for (size_t i = 0; i < preset.extruder_ids.size(); ++i)
                preset_sorted.push_back(
                    {preset.extruder_ids[i], double(preset.ratios_pct[i]) / 100.0}
                );

            std::sort(ve_sorted.begin(), ve_sorted.end(), by_id_then_ratio);
            std::sort(preset_sorted.begin(), preset_sorted.end(), by_id_then_ratio);

            bool match = true;
            for (size_t i = 0; i < ve_sorted.size() && match; ++i) {
                if (ve_sorted[i].id != preset_sorted[i].id)
                    match = false;
                else if (std::abs(ve_sorted[i].ratio - preset_sorted[i].ratio) > 0.02)
                    match = false;
            }
            if (match) {
                return true;
            }
        }
        return false;
    };

    auto add_preset_button =
        [this, &preset_already_used](wxWrapSizer* wrap, const RecommendationPreset& preset)
    {
        std::vector<wxColour> cols;
        cols.reserve(preset.extruder_ids.size());
        wxString tooltip;
        for (size_t i = 0; i < preset.extruder_ids.size(); ++i) {
            const unsigned int ext = preset.extruder_ids[i];
            cols.push_back(physical_color(ext));
            if (i > 0) {
                tooltip += " + ";
            }

            tooltip += wxString::Format("Ext %u (%d %%)", ext, preset.ratios_pct[i]);
        }
        const bool used       = preset_already_used(preset);
        const wxSize sw       = FromDIP(wxSize(48, 48));
        const wxSize btn_size = sw + FromDIP(wxSize(10, 10));
        const int pad_px      = (btn_size.GetWidth() - sw.GetWidth()) / 2;
        const wxBitmap bmp = render_preset_bitmap(sw, cols, preset.extruder_ids, preset.ratios_pct);
        wxPanel* btn       = new wxPanel(
            m_presets_scrolled_window,
            wxID_ANY,
            wxDefaultPosition,
            btn_size,
            wxBORDER_NONE
        );
        btn->SetMinSize(btn_size);
        btn->SetDoubleBuffered(true);
        btn->SetBackgroundStyle(wxBG_STYLE_PAINT);
        btn->SetCursor(used ? wxNullCursor : wxCursor(wxCURSOR_HAND));
        btn->SetToolTip(used ? _L("Already added") : _L("Click to add: ") + tooltip);
        auto hovered = std::make_shared<bool>(false);
        btn->Bind(
            wxEVT_ENTER_WINDOW,
            [btn, hovered](wxMouseEvent& e)
            {
                *hovered = true;
                btn->Refresh();
                e.Skip();
            }
        );
        btn->Bind(
            wxEVT_LEAVE_WINDOW,
            [btn, hovered](wxMouseEvent& e)
            {
                *hovered = false;
                btn->Refresh();
                e.Skip();
            }
        );
        btn->Bind(
            wxEVT_PAINT,
            [btn, bmp, hovered, used, pad_px](wxPaintEvent&)
            {
                wxAutoBufferedPaintDC dc(btn);
                dc.SetBackground(wxBrush(btn->GetParent()->GetBackgroundColour()));
                dc.Clear();
                dc.DrawBitmap(bmp, pad_px, pad_px, true);
                const int w = btn->GetClientSize().GetWidth();
                const int h = btn->GetClientSize().GetHeight();
                dc.SetBrush(*wxTRANSPARENT_BRUSH);
                if (used || *hovered) {
                    if (wxGraphicsContext* gc = wxGraphicsContext::Create(dc)) {
                        gc->SetAntialiasMode(wxANTIALIAS_DEFAULT);
                        gc->SetBrush(*wxTRANSPARENT_BRUSH);
                        if (used) {
                            gc->SetPen(wxPen(wxColour(0xED, 0x6B, 0x21), 2, wxPENSTYLE_SHORT_DASH));
                            gc->DrawRoundedRectangle(1.5, 1.5, w - 3.0, h - 3.0, 4.0);
                        } else {
                            gc->SetPen(wxPen(wxColour(0xED, 0x6B, 0x21), 2));
                            gc->DrawRoundedRectangle(1.5, 1.5, w - 3.0, h - 3.0, 4.0);
                        }
                        delete gc;
                    }
                }
            }
        );
        const RecommendationPreset preset_copy = preset;
        btn->Bind(
            wxEVT_LEFT_DOWN,
            [this, preset_copy, used](wxMouseEvent&)
            {
                if (!used) {
                    on_preset_clicked(preset_copy);
                }
            }
        );
        wrap->Add(btn, 0, wxALL, FromDIP(4));
    };

    for (const RecommendationPreset& p : two_color) {
        add_preset_button(m_two_color_presets_wrap, p);
    }

    for (const RecommendationPreset& p : three_color) {
        add_preset_button(m_three_color_presets_wrap, p);
    }

    const bool has_two   = !two_color.empty();
    const bool has_three = !three_color.empty();
    if (m_two_color_presets_title) {
        m_two_color_presets_title->Show(has_two);
    }

    if (m_three_color_presets_title) {
        m_three_color_presets_title->Show(has_three);
    }

    if (m_presets_empty_hint) {
        m_presets_empty_hint->Show(!has_two && !has_three);
    }

    m_presets_scrolled_window->FitInside();
    m_presets_scrolled_window->Layout();
    m_presets_scrolled_window->SendSizeEvent();
}

void FullSpectrumDialog::on_preset_clicked(const RecommendationPreset& preset)
{
    FullSpectrum::VirtualExtruder ve;
    ve.id = next_free_virtual_id();
    for (size_t i = 0; i < preset.extruder_ids.size(); ++i) {
        ve.components.push_back({preset.extruder_ids[i], double(preset.ratios_pct[i]) / 100.0});
    }

    m_working_list.push_back(std::move(ve));

    refresh_virtual_extruder_list(-1);
    rebuild_preset_grid();
}

} // namespace Slic3r::GUI
