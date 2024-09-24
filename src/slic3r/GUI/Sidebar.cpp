///|/ Copyright (c) Prusa Research 2018 - 2023 Vojtěch Bubník @bubnikv, Lukáš Matěna @lukasmatena, Oleksandra Iushchenko @YuSanka, Enrico Turri @enricoturri1966, Tomáš Mészáros @tamasmeszaros, David Kocík @kocikdav, Lukáš Hejl @hejllukas, Pavel Mikuš @Godrak, Filip Sykala @Jony01, Vojtěch Král @vojtechkral
///|/ Copyright (c) 2022 Michael Kirsch
///|/ Copyright (c) 2021 Boleslaw Ciesielski
///|/ Copyright (c) 2019 John Drake @foxox
///|/
///|/ ported from lib/Slic3r/GUI/Plater.pm:
///|/ Copyright (c) Prusa Research 2016 - 2019 Vojtěch Bubník @bubnikv, Vojtěch Král @vojtechkral, Enrico Turri @enricoturri1966, Oleksandra Iushchenko @YuSanka, Lukáš Matěna @lukasmatena, Tomáš Mészáros @tamasmeszaros
///|/ Copyright (c) 2018 Martin Loidl @LoidlM
///|/ Copyright (c) 2017 Matthias Gazzari @qtux
///|/ Copyright (c) Slic3r 2012 - 2016 Alessandro Ranellucci @alranel
///|/ Copyright (c) 2017 Joseph Lenox @lordofhyphens
///|/ Copyright (c) 2015 Daren Schwenke
///|/ Copyright (c) 2014 Mark Hindess
///|/ Copyright (c) 2012 Mike Sheldrake @mesheldrake
///|/ Copyright (c) 2012 Henrik Brix Andersen @henrikbrixandersen
///|/ Copyright (c) 2012 Sam Wong
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#include "Sidebar.hpp"
#include "FrequentlyChangedParameters.hpp"
#include "Plater.hpp"

#include <cstddef>
#include <string>
#include <boost/algorithm/string.hpp>

#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/button.h>
#include <wx/bmpcbox.h>
#include <wx/statbox.h>
#include <wx/statbmp.h>
#include <wx/wupdlock.h> // IWYU pragma: keep
#include "wx/generic/stattextg.h"
#ifdef _WIN32
#include <wx/richtooltip.h>
#include <wx/custombgwin.h>
#include <wx/popupwin.h>
#endif

#include "libslic3r/libslic3r.h"
#include "libslic3r/Model.hpp"
#include "libslic3r/Print.hpp"
#include "libslic3r/SLAPrint.hpp"
#include "libslic3r/PresetBundle.hpp"

#include "GUI.hpp"
#include "GUI_App.hpp"
#include "GUI_ObjectList.hpp"
#include "GUI_ObjectManipulation.hpp"
#include "GUI_ObjectLayers.hpp"
#include "wxExtensions.hpp"
#include "format.hpp"
#include "Selection.hpp"
#include "Tab.hpp"
#include "I18N.hpp"

#include "NotificationManager.hpp"
#include "PresetComboBoxes.hpp"
#include "MsgDialog.hpp"

using Slic3r::Preset;
using Slic3r::GUI::format_wxstr;

namespace Slic3r {
namespace GUI {

class ObjectInfo : public wxStaticBoxSizer
{
    std::string m_warning_icon_name{ "exclamation" };
public:
    ObjectInfo(wxWindow *parent);

    wxStaticBitmap *manifold_warning_icon;
    wxStaticBitmap *info_icon;
    wxStaticText *info_size;
    wxStaticText *info_volume;
    wxStaticText *info_facets;
    wxStaticText *info_manifold;

    wxStaticText *label_volume;
    std::vector<wxStaticText *> sla_hidden_items;

    bool        showing_manifold_warning_icon;
    void        show_sizer(bool show);
    void        update_warning_icon(const std::string& warning_icon_name);
};

ObjectInfo::ObjectInfo(wxWindow *parent) :
    wxStaticBoxSizer(new wxStaticBox(parent, wxID_ANY, _L("Info")), wxVERTICAL)
{
    GetStaticBox()->SetFont(wxGetApp().bold_font());
    wxGetApp().UpdateDarkUI(GetStaticBox());

    auto *grid_sizer = new wxFlexGridSizer(4, 5, 15);
    grid_sizer->SetFlexibleDirection(wxHORIZONTAL);

    auto init_info_label = [parent, grid_sizer](wxStaticText **info_label, wxString text_label, wxSizer* sizer_with_icon=nullptr) {
        auto *text = new wxStaticText(parent, wxID_ANY, text_label + ":");
        text->SetFont(wxGetApp().small_font());
        *info_label = new wxStaticText(parent, wxID_ANY, "");
        (*info_label)->SetFont(wxGetApp().small_font());
        grid_sizer->Add(text, 0);
        if (sizer_with_icon) {
            sizer_with_icon->Insert(0, *info_label, 0);
            grid_sizer->Add(sizer_with_icon, 0, wxEXPAND);
        }
        else
            grid_sizer->Add(*info_label, 0);
        return text;
    };

    init_info_label(&info_size, _L("Size"));

    info_icon = new wxStaticBitmap(parent, wxID_ANY, *get_bmp_bundle("info"));
    info_icon->SetToolTip(_L("For a multipart object, this value isn't accurate.\n"
                             "It doesn't take account of intersections and negative volumes."));
    auto* volume_info_sizer = new wxBoxSizer(wxHORIZONTAL);
    volume_info_sizer->Add(info_icon, 0, wxLEFT, 10);
    label_volume = init_info_label(&info_volume, _L("Volume"), volume_info_sizer);

    init_info_label(&info_facets, _L("Facets"));
    Add(grid_sizer, 0, wxEXPAND);

    info_manifold = new wxStaticText(parent, wxID_ANY, "");
    info_manifold->SetFont(wxGetApp().small_font());
    manifold_warning_icon = new wxStaticBitmap(parent, wxID_ANY, *get_bmp_bundle(m_warning_icon_name));
    auto *sizer_manifold = new wxBoxSizer(wxHORIZONTAL);
    sizer_manifold->Add(manifold_warning_icon, 0, wxLEFT, 2);
    sizer_manifold->Add(info_manifold, 0, wxLEFT, 2);
    Add(sizer_manifold, 0, wxEXPAND | wxTOP, 4);

    sla_hidden_items = { label_volume, info_volume, };

    // Fixes layout issues on plater, short BitmapComboBoxes with some Windows scaling, see GH issue #7414.
    this->Show(false);
}

void ObjectInfo::show_sizer(bool show)
{
    Show(show);
    if (show)
        manifold_warning_icon->Show(showing_manifold_warning_icon && show);
}

void ObjectInfo::update_warning_icon(const std::string& warning_icon_name)
{
    if ((showing_manifold_warning_icon = !warning_icon_name.empty())) {
        m_warning_icon_name = warning_icon_name;
        manifold_warning_icon->SetBitmap(*get_bmp_bundle(m_warning_icon_name));
    }
}


// Note: SlicedInfoIdxs are used in code as indexes, so enum is preferred here than enum class
enum SlicedInfoIdx
{
    siFilament_g,
    siFilament_m,
    siFilament_mm3,
    siMaterial_unit,
    siCost,
    siEstimatedTime,
    siWTNumberOfToolchanges,

    siCount
};

class SlicedInfo : public wxStaticBoxSizer
{
public:
    SlicedInfo(wxWindow *parent);
    void SetTextAndShow(SlicedInfoIdx idx, const wxString& text, const wxString& new_label="");

private:
    std::vector<std::pair<wxStaticText*, wxStaticText*>> info_vec;
};

SlicedInfo::SlicedInfo(wxWindow *parent) :
    wxStaticBoxSizer(new wxStaticBox(parent, wxID_ANY, _L("Sliced Info")), wxVERTICAL)
{
    GetStaticBox()->SetFont(wxGetApp().bold_font());
    wxGetApp().UpdateDarkUI(GetStaticBox());

    auto *grid_sizer = new wxFlexGridSizer(2, 5, 15);
    grid_sizer->SetFlexibleDirection(wxVERTICAL);

    info_vec.reserve(siCount);

    auto init_info_label = [this, parent, grid_sizer](wxString text_label) {
        auto *text = new wxStaticText(parent, wxID_ANY, text_label);
        text->SetFont(wxGetApp().small_font());
        auto info_label = new wxStaticText(parent, wxID_ANY, "N/A");
        info_label->SetFont(wxGetApp().small_font());
        grid_sizer->Add(text, 0);
        grid_sizer->Add(info_label, 0);
        info_vec.push_back(std::pair<wxStaticText*, wxStaticText*>(text, info_label));
    };

    init_info_label(_L("Used Filament (g)"));
    init_info_label(_L("Used Filament (m)"));
    init_info_label(_L("Used Filament (mm³)"));
    init_info_label(_L("Used Material (unit)"));
    init_info_label(_L("Cost (money)"));
    init_info_label(_L("Estimated printing time"));
    init_info_label(_L("Number of tool changes"));

    Add(grid_sizer, 0, wxEXPAND);
    this->Show(false);
}

void SlicedInfo::SetTextAndShow(SlicedInfoIdx idx, const wxString& text, const wxString& new_label/*=""*/)
{
    const bool show = text != "N/A";
    if (show)
        info_vec[idx].second->SetLabelText(text);
    if (!new_label.IsEmpty())
        info_vec[idx].first->SetLabelText(new_label);
    info_vec[idx].first->Show(show);
    info_vec[idx].second->Show(show);
}


// Sidebar / private

void Sidebar::show_preset_comboboxes()
{
    const bool showSLA = wxGetApp().preset_bundle->printers.get_edited_preset().printer_technology() == ptSLA;

    for (size_t i = 0; i < 4; ++i)
        m_presets_sizer->Show(i, !showSLA);

    for (size_t i = 4; i < 8; ++i)
        m_presets_sizer->Show(i, showSLA);

    m_frequently_changed_parameters->Show(!showSLA);

    m_scrolled_panel->GetParent()->Layout();
    m_scrolled_panel->Refresh();
}

#ifdef _WIN32
using wxRichToolTipPopup = wxCustomBackgroundWindow<wxPopupTransientWindow>;
static wxRichToolTipPopup* get_rtt_popup(wxButton* btn)
{
    auto children = btn->GetChildren();
    for (auto child : children)
        if (child->IsShown())
            return dynamic_cast<wxRichToolTipPopup*>(child);
    return nullptr;
}

// Help function to find and check if some combobox is dropped down and then dismiss it
static bool found_and_dismiss_shown_dropdown(wxWindow* win)
{
    auto children = win->GetChildren();
    if (children.IsEmpty()) {
        if (auto dd = dynamic_cast<DropDown*>(win); dd && dd->IsShown()) {
            dd->CallDismissAndNotify();
            return true;
        }
    }

    for (auto child : children) {
        if (found_and_dismiss_shown_dropdown(child))
            return true;
    }
    return false;
}

static void show_rich_tip(const wxString& tooltip, wxButton* btn)
{   
    if (tooltip.IsEmpty())
        return;

    // Currently state (propably wxWidgets issue) : 
    // When second wxPopupTransientWindow is popped up, then first wxPopupTransientWindow doesn't receive EVT_DISMISS and stay on the top. 
    // New comboboxes use wxPopupTransientWindow as DropDown now
    // That is why DropDown stay on top, when we show rich tooltip for btn.
    // (see https://github.com/prusa3d/PrusaSlicer/issues/11988)

    // So, check the combo boxes and close them if necessary before showing the rich tip.
    found_and_dismiss_shown_dropdown(btn->GetParent());

    wxRichToolTip tip(tooltip, "");
    tip.SetIcon(wxICON_NONE);
    tip.SetTipKind(wxTipKind_BottomRight);
    tip.SetTitleFont(wxGetApp().normal_font());
    tip.SetBackgroundColour(wxGetApp().get_window_default_clr());

    tip.ShowFor(btn);
    // Every call of the ShowFor() creates new RichToolTip and show it.
    // Every one else are hidden. 
    // So, set a text color just for the shown rich tooltip
    if (wxRichToolTipPopup* popup = get_rtt_popup(btn)) {
        auto children = popup->GetChildren();
        for (auto child : children) {
            child->SetForegroundColour(wxGetApp().get_label_clr_default());
            // we neen just first text line for out rich tooltip
            return;
        }
    }
}

static void hide_rich_tip(wxButton* btn)
{
    if (wxRichToolTipPopup* popup = get_rtt_popup(btn))
        popup->Dismiss();
}
#endif

// Sidebar / public

Sidebar::Sidebar(Plater *parent)
    : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxSize(42 * wxGetApp().em_unit(), -1)), m_plater(parent)
{
    m_scrolled_panel = new wxScrolledWindow(this);
    m_scrolled_panel->SetScrollRate(0, 5);

    SetFont(wxGetApp().normal_font());
#ifndef __APPLE__
#ifdef _WIN32
    wxGetApp().UpdateDarkUI(this);
    wxGetApp().UpdateDarkUI(m_scrolled_panel);
#else
    SetBackgroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW));
#endif
#endif

    // Sizer in the scrolled area
    auto *scrolled_sizer = new wxBoxSizer(wxVERTICAL);
    m_scrolled_panel->SetSizer(scrolled_sizer);

    // The preset chooser
    m_presets_sizer = new wxFlexGridSizer(10, 1, 1, 2);
    m_presets_sizer->AddGrowableCol(0, 1);
    m_presets_sizer->SetFlexibleDirection(wxBOTH);

    bool is_msw = false;
#ifdef __WINDOWS__
    m_scrolled_panel->SetDoubleBuffered(true);

    m_presets_panel = new wxPanel(m_scrolled_panel, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTAB_TRAVERSAL);
    wxGetApp().UpdateDarkUI(m_presets_panel);
    m_presets_panel->SetSizer(m_presets_sizer);

    is_msw = true;
#else
    m_presets_panel = m_scrolled_panel;
#endif //__WINDOWS__

    m_filaments_sizer = new wxBoxSizer(wxVERTICAL);

    const int margin_5 = int(0.5 * wxGetApp().em_unit());// 5;

    auto init_combo = [this, margin_5](PlaterPresetComboBox **combo, wxString label, Preset::Type preset_type, bool filament) {
        auto *text = new wxStaticText(m_presets_panel, wxID_ANY, label + ":");
        text->SetFont(wxGetApp().small_font());
        *combo = new PlaterPresetComboBox(m_presets_panel, preset_type);

        auto combo_and_btn_sizer = new wxBoxSizer(wxHORIZONTAL);
        combo_and_btn_sizer->Add(*combo, 1, wxEXPAND);
        if ((*combo)->edit_btn)
            combo_and_btn_sizer->Add((*combo)->edit_btn, 0, wxALIGN_CENTER_VERTICAL|wxLEFT|wxRIGHT,
                                    int(0.3*wxGetApp().em_unit()));

        auto *sizer_presets = this->m_presets_sizer;
        auto *sizer_filaments = this->m_filaments_sizer;
        // Hide controls, which will be shown/hidden in respect to the printer technology
        text->Show(preset_type == Preset::TYPE_PRINTER);
        sizer_presets->Add(text, 0, wxALIGN_LEFT | wxEXPAND | wxRIGHT, 4);
        if (! filament) {
            combo_and_btn_sizer->ShowItems(preset_type == Preset::TYPE_PRINTER);
            sizer_presets->Add(combo_and_btn_sizer, 0, wxEXPAND | 
#ifdef __WXGTK3__
                wxRIGHT, margin_5);
#else
                wxBOTTOM, 1);
                (void)margin_5; // supress unused capture warning
#endif // __WXGTK3__
            if ((*combo)->connect_info_sizer) {
                auto tmp_h_sizer = new wxBoxSizer(wxHORIZONTAL);
                tmp_h_sizer->Add((*combo)->connect_info_sizer, 1, wxEXPAND);
                sizer_presets->Add(tmp_h_sizer, 0, wxBOTTOM, int(0.3 * wxGetApp().em_unit()));
            }
        } else {
            sizer_filaments->Add(combo_and_btn_sizer, 0, wxEXPAND |
#ifdef __WXGTK3__
                wxRIGHT, margin_5);
#else
                wxBOTTOM, 1);
#endif // __WXGTK3__
            (*combo)->set_extruder_idx(0);
            sizer_filaments->ShowItems(false);
            sizer_presets->Add(sizer_filaments, 1, wxEXPAND);
        }
    };

    m_combos_filament.push_back(nullptr);
    init_combo(&m_combo_print,         _L("Print settings"),     Preset::TYPE_PRINT,         false);
    init_combo(&m_combos_filament[0],  _L("Filament"),           Preset::TYPE_FILAMENT,      true);
    init_combo(&m_combo_sla_print,     _L("SLA print settings"), Preset::TYPE_SLA_PRINT,     false);
    init_combo(&m_combo_sla_material,  _L("SLA material"),       Preset::TYPE_SLA_MATERIAL,  false);
    init_combo(&m_combo_printer,       _L("Printer"),            Preset::TYPE_PRINTER,       false);

    wxBoxSizer* params_sizer = new wxBoxSizer(wxVERTICAL);

    // Frequently changed parameters
    m_frequently_changed_parameters = std::make_unique<FreqChangedParams>(m_scrolled_panel);
    params_sizer->Add(m_frequently_changed_parameters->get_sizer(), 0, wxEXPAND | wxTOP | wxBOTTOM
#ifdef __WXGTK3__
        | wxRIGHT
#endif // __WXGTK3__
        , wxOSX ? 1 : margin_5);

    // Object List
    m_object_list = new ObjectList(m_scrolled_panel);
    params_sizer->Add(m_object_list->get_sizer(), 1, wxEXPAND);

    // Object Manipulations
    m_object_manipulation = std::make_unique<ObjectManipulation>(m_scrolled_panel);
    m_object_manipulation->Hide();
    params_sizer->Add(m_object_manipulation->get_sizer(), 0, wxEXPAND | wxTOP, margin_5);

    // Frequently Object Settings
    m_object_settings = std::make_unique<ObjectSettings>(m_scrolled_panel);
    m_object_settings->Hide();
    params_sizer->Add(m_object_settings->get_sizer(), 0, wxEXPAND | wxTOP, margin_5);

    // Object Layers
    m_object_layers = std::make_unique<ObjectLayers>(m_scrolled_panel);
    m_object_layers->Hide();
    params_sizer->Add(m_object_layers->get_sizer(), 0, wxEXPAND | wxTOP, margin_5);

    // Info boxes
    m_object_info = new ObjectInfo(m_scrolled_panel);
    m_sliced_info = new SlicedInfo(m_scrolled_panel);

    int size_margin = wxGTK3 ? wxLEFT | wxRIGHT : wxLEFT;

    is_msw ?
        scrolled_sizer->Add(m_presets_panel, 0, wxEXPAND | size_margin, margin_5) :
        scrolled_sizer->Add(m_presets_sizer, 0, wxEXPAND | size_margin, margin_5);
    scrolled_sizer->Add(params_sizer, 1, wxEXPAND | size_margin, margin_5);
    scrolled_sizer->Add(m_object_info, 0, wxEXPAND | wxTOP | size_margin, margin_5);
    scrolled_sizer->Add(m_sliced_info, 0, wxEXPAND | wxTOP | size_margin, margin_5);

    // Buttons underneath the scrolled area

    // rescalable bitmap buttons "Send to printer" and "Remove device" 

    auto init_scalable_btn = [this](ScalableButton** btn, const std::string& icon_name, wxString tooltip = wxEmptyString)
    {
#ifdef __APPLE__
        int bmp_px_cnt = 16;
#else
        int bmp_px_cnt = 32;
#endif //__APPLE__
        ScalableBitmap bmp = ScalableBitmap(this, icon_name, bmp_px_cnt);
        *btn = new ScalableButton(this, wxID_ANY, bmp, "", wxBU_EXACTFIT);
        wxGetApp().SetWindowVariantForButton((*btn));

#ifdef _WIN32
        (*btn)->Bind(wxEVT_ENTER_WINDOW, [tooltip, btn, this](wxMouseEvent& event) {
            show_rich_tip(tooltip, *btn);
            event.Skip();
        });
        (*btn)->Bind(wxEVT_LEAVE_WINDOW, [btn, this](wxMouseEvent& event) {
            hide_rich_tip(*btn);
            event.Skip();
        });
#else
        (*btn)->SetToolTip(tooltip);
#endif // _WIN32
        (*btn)->Hide();
    };

    init_scalable_btn(&m_btn_send_gcode   , "export_gcode", _L("Send to printer") + " " +GUI::shortkey_ctrl_prefix() + "Shift+G");
	init_scalable_btn(&m_btn_export_gcode_removable, "export_to_sd", _L("Export to SD card / Flash drive") + " " + GUI::shortkey_ctrl_prefix() + "U");

    // regular buttons "Slice now" and "Export G-code" 

#ifdef _WIN32
    const int scaled_height = m_btn_export_gcode_removable->GetBitmapHeight();
#else
    const int scaled_height = m_btn_export_gcode_removable->GetBitmapHeight() + 4;
#endif
    auto init_btn = [this](wxButton **btn, wxString label, const int button_height) {
        *btn = new wxButton(this, wxID_ANY, label, wxDefaultPosition,
                            wxSize(-1, button_height), wxBU_EXACTFIT);
        wxGetApp().SetWindowVariantForButton((*btn));
        (*btn)->SetFont(wxGetApp().bold_font());
        wxGetApp().UpdateDarkUI((*btn), true);
    };

    init_btn(&m_btn_export_gcode, _L("Export G-code") + dots , scaled_height);
    init_btn(&m_btn_reslice     , _L("Slice now")            , scaled_height);
    init_btn(&m_btn_connect_gcode, _L("Send to Connect"), scaled_height);

    enable_buttons(false);

    auto *btns_sizer = new wxBoxSizer(wxVERTICAL);

    auto* complect_btns_sizer = new wxBoxSizer(wxHORIZONTAL);
    complect_btns_sizer->Add(m_btn_export_gcode, 1, wxEXPAND);
    complect_btns_sizer->Add(m_btn_connect_gcode, 1, wxEXPAND | wxLEFT, margin_5);
    complect_btns_sizer->Add(m_btn_send_gcode, 0, wxLEFT, margin_5);
	complect_btns_sizer->Add(m_btn_export_gcode_removable, 0, wxLEFT, margin_5);

    btns_sizer->Add(m_btn_reslice, 0, wxEXPAND | wxTOP, margin_5);
    btns_sizer->Add(complect_btns_sizer, 0, wxEXPAND | wxTOP, margin_5);

    auto *sizer = new wxBoxSizer(wxVERTICAL);
    sizer->Add(m_scrolled_panel, 1, wxEXPAND);
    sizer->Add(btns_sizer, 0, wxEXPAND | wxLEFT | wxBOTTOM
#ifndef _WIN32
        | wxRIGHT
#endif // __linux__
        , margin_5);
    SetSizer(sizer);

    // Events
    m_btn_export_gcode->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { m_plater->export_gcode(false); });
    m_btn_reslice->Bind(wxEVT_BUTTON, [this](wxCommandEvent&)
    {
        if (m_plater->canvas3D()->get_gizmos_manager().is_in_editing_mode(true))
            return;

        const bool export_gcode_after_slicing = wxGetKeyState(WXK_SHIFT);
        if (export_gcode_after_slicing)
            m_plater->export_gcode(true);
        else
            m_plater->reslice();
        m_plater->select_view_3D("Preview");
    });

#ifdef _WIN32
    m_btn_reslice->Bind(wxEVT_ENTER_WINDOW, [this](wxMouseEvent& event) {
        show_rich_tip(m_reslice_btn_tooltip, m_btn_reslice);
        event.Skip();
    });
    m_btn_reslice->Bind(wxEVT_LEAVE_WINDOW, [this](wxMouseEvent& event) {
        hide_rich_tip(m_btn_reslice);
        event.Skip();
    });
#endif // _WIN32

    m_btn_send_gcode->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { m_plater->send_gcode(); });
    m_btn_export_gcode_removable->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { m_plater->export_gcode(true); });
    m_btn_connect_gcode->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { m_plater->connect_gcode(); });

    this->Bind(wxEVT_COMBOBOX, &Sidebar::on_select_preset, this);

}

Sidebar::~Sidebar() {}

void Sidebar::init_filament_combo(PlaterPresetComboBox** combo, int extr_idx)
{
    *combo = new PlaterPresetComboBox(m_presets_panel, Slic3r::Preset::TYPE_FILAMENT);
    (*combo)->set_extruder_idx(extr_idx);

    auto combo_and_btn_sizer = new wxBoxSizer(wxHORIZONTAL);
    combo_and_btn_sizer->Add(*combo, 1, wxEXPAND);
    combo_and_btn_sizer->Add((*combo)->edit_btn, 0, wxALIGN_CENTER_VERTICAL | wxLEFT | wxRIGHT,
                            int(0.3*wxGetApp().em_unit()));

    this->m_filaments_sizer->Add(combo_and_btn_sizer, 1, wxEXPAND |
#ifdef __WXGTK3__
        wxRIGHT, int(0.5 * wxGetApp().em_unit()));
#else
        wxBOTTOM, 1);
#endif // __WXGTK3__
}

void Sidebar::remove_unused_filament_combos(const size_t current_extruder_count)
{
    if (current_extruder_count >= m_combos_filament.size())
        return;
    auto sizer_filaments = this->m_filaments_sizer;
    while (m_combos_filament.size() > current_extruder_count) {
        const int last = m_combos_filament.size() - 1;
        sizer_filaments->Remove(last);
        (*m_combos_filament[last]).Destroy();
        m_combos_filament.pop_back();
    }
}

void Sidebar::update_all_preset_comboboxes()
{
    PresetBundle &preset_bundle = *wxGetApp().preset_bundle;
    const auto print_tech = preset_bundle.printers.get_edited_preset().printer_technology();

    // Update the print choosers to only contain the compatible presets, update the dirty flags.
    if (print_tech == ptFFF)
        m_combo_print->update();
    else {
        m_combo_sla_print->update();
        m_combo_sla_material->update();
    }
    // Update the printer choosers, update the dirty flags.
    m_combo_printer->update();
    // Update the filament choosers to only contain the compatible presets, update the color preview,
    // update the dirty flags.
    if (print_tech == ptFFF) {
        for (PlaterPresetComboBox* cb : m_combos_filament)
            cb->update();
    }
}

void Sidebar::update_printer_presets_combobox()
{
    m_combo_printer->update();
    Layout();
}

void Sidebar::update_presets(Preset::Type preset_type)
{
    PresetBundle &preset_bundle = *wxGetApp().preset_bundle;
    const auto print_tech = preset_bundle.printers.get_edited_preset().printer_technology();

    switch (preset_type) {
    case Preset::TYPE_FILAMENT:
    {
        const size_t extruder_cnt = print_tech != ptFFF ? 1 :
                                dynamic_cast<ConfigOptionFloats*>(preset_bundle.printers.get_edited_preset().config.option("nozzle_diameter"))->values.size();
        const size_t filament_cnt = m_combos_filament.size() > extruder_cnt ? extruder_cnt : m_combos_filament.size();

        for (size_t i = 0; i < filament_cnt; i++)
            m_combos_filament[i]->update();

        break;
    }

    case Preset::TYPE_PRINT:
        m_combo_print->update();
        break;

    case Preset::TYPE_SLA_PRINT:
        m_combo_sla_print->update();
        break;

    case Preset::TYPE_SLA_MATERIAL:
        m_combo_sla_material->update();
        break;

    case Preset::TYPE_PRINTER:
    {
        update_all_preset_comboboxes();
#if 1 // #ysFIXME_delete_after_test_of (PS 2.6.2) >> it looks like CallAfter() is no need [issue with disapearing of comboboxes are not reproducible]
        show_preset_comboboxes();
#else
        // CallAfter is really needed here to correct layout of the preset comboboxes,
        // when printer technology is changed during a project loading AND/OR switching the application mode.
        // Otherwise, some of comboboxes are invisible 
        CallAfter([this]() { show_preset_comboboxes(); });
#endif
        break;
    }

    default: break;
    }

    // Synchronize config.ini with the current selections.
    wxGetApp().preset_bundle->export_selections(*wxGetApp().app_config);
}

void Sidebar::on_select_preset(wxCommandEvent& evt)
{
    PlaterPresetComboBox* combo = static_cast<PlaterPresetComboBox*>(evt.GetEventObject());
    Preset::Type preset_type = combo->get_type();

    // see https://github.com/prusa3d/PrusaSlicer/issues/3889
    // Under OSX: in case of use of a same names written in different case (like "ENDER" and "Ender"),
    // m_presets_choice->GetSelection() will return first item, because search in PopupListCtrl is case-insensitive.
    // So, use GetSelection() from event parameter 
    int selection = evt.GetSelection();

    auto idx = combo->get_extruder_idx();

    //! Because of The MSW and GTK version of wxBitmapComboBox derived from wxComboBox,
    //! but the OSX version derived from wxOwnerDrawnCombo.
    //! So, to get selected string we do
    //!     combo->GetString(combo->GetSelection())
    //! instead of
    //!     combo->GetStringSelection().ToUTF8().data());

    std::string preset_name = wxGetApp().preset_bundle->get_preset_name_by_alias(preset_type,
                              Preset::remove_suffix_modified(into_u8(combo->GetString(selection))), idx);

    std::string last_selected_ph_printer_name = combo->get_selected_ph_printer_name();

    bool select_preset = !combo->selection_is_changed_according_to_physical_printers();
    // TODO: ?
    if (preset_type == Preset::TYPE_FILAMENT) {
        wxGetApp().preset_bundle->set_filament_preset(idx, preset_name);

        TabFilament* tab = dynamic_cast<TabFilament*>(wxGetApp().get_tab(Preset::TYPE_FILAMENT));
        if (tab && combo->get_extruder_idx() == tab->get_active_extruder() && !tab->select_preset(preset_name)) {
            // revert previously selection
            const std::string& old_name = wxGetApp().preset_bundle->filaments.get_edited_preset().name;
                                          wxGetApp().preset_bundle->set_filament_preset(idx, old_name);
        }
        else
            // Synchronize config.ini with the current selections.
            wxGetApp().preset_bundle->export_selections(*wxGetApp().app_config);
        combo->update();
    }
    else if (select_preset) {
        wxWindowUpdateLocker noUpdates(m_presets_panel);
        wxGetApp().get_tab(preset_type)->select_preset(preset_name, false, last_selected_ph_printer_name);
    }

    if (preset_type != Preset::TYPE_PRINTER || select_preset) {
        // update plater with new config
        m_plater->on_config_change(wxGetApp().preset_bundle->full_config());
    }
    if (preset_type == Preset::TYPE_PRINTER) {
        /* Settings list can be changed after printer preset changing, so
         * update all settings items for all item had it.
         * Furthermore, Layers editing is implemented only for FFF printers
         * and for SLA presets they should be deleted
         */
        m_object_list->update_object_list_by_printer_technology();
    }

#ifdef __WXMSW__
    // From the Win 2004 preset combobox lose a focus after change the preset selection
    // and that is why the up/down arrow doesn't work properly
    // (see https://github.com/prusa3d/PrusaSlicer/issues/5531 ).
    // So, set the focus to the combobox explicitly
    combo->SetFocus();
#endif
}

void Sidebar::update_reslice_btn_tooltip()
{
    wxString tooltip = wxString("Slice") + " [" + GUI::shortkey_ctrl_prefix() + "R]";
    if (m_mode != comSimple)
        tooltip += wxString("\n") + _L("Hold Shift to Slice & Export G-code");
#ifdef _WIN32
    m_reslice_btn_tooltip = tooltip;
#else
    m_btn_reslice->SetToolTip(tooltip);
#endif
}

void Sidebar::msw_rescale()
{
    SetMinSize(wxSize(42 * wxGetApp().em_unit(), -1));
    m_combo_print       ->msw_rescale();
    m_combo_sla_print   ->msw_rescale();
    m_combo_sla_material->msw_rescale();
    m_combo_printer     ->msw_rescale();

    for (PlaterPresetComboBox* combo : m_combos_filament)
        combo->msw_rescale();

    m_frequently_changed_parameters->msw_rescale();
    m_object_list                  ->msw_rescale();
    m_object_manipulation          ->msw_rescale();
    m_object_layers                ->msw_rescale();

#ifdef _WIN32
    const int scaled_height = m_btn_export_gcode_removable->GetBitmapHeight();
#else
    const int scaled_height = m_btn_export_gcode_removable->GetBitmapHeight() + 4;
#endif
    m_btn_export_gcode->SetMinSize(wxSize(-1, scaled_height));
    m_btn_reslice     ->SetMinSize(wxSize(-1, scaled_height));

    m_scrolled_panel->Layout();
}

void Sidebar::sys_color_changed()
{
#ifdef _WIN32
    wxWindowUpdateLocker noUpdates(this);

    for (wxWindow* win : std::vector<wxWindow*>{ this, m_sliced_info->GetStaticBox(), m_object_info->GetStaticBox(), m_btn_reslice, m_btn_export_gcode })
        wxGetApp().UpdateDarkUI(win);
    for (wxWindow* win : std::vector<wxWindow*>{ m_scrolled_panel, m_presets_panel })
        wxGetApp().UpdateAllStaticTextDarkUI(win);
    for (wxWindow* btn : std::vector<wxWindow*>{ m_btn_reslice, m_btn_export_gcode, m_btn_connect_gcode })
        wxGetApp().UpdateDarkUI(btn, true);

    m_frequently_changed_parameters->sys_color_changed();
    m_object_settings              ->sys_color_changed();
#endif

    m_combo_print       ->sys_color_changed();
    m_combo_sla_print   ->sys_color_changed();
    m_combo_sla_material->sys_color_changed();
    m_combo_printer     ->sys_color_changed();

    for (PlaterPresetComboBox* combo : m_combos_filament)
        combo->sys_color_changed();

    m_object_list        ->sys_color_changed();
    m_object_manipulation->sys_color_changed();
    m_object_layers      ->sys_color_changed();

    // btn...->msw_rescale() updates icon on button, so use it
    m_btn_send_gcode            ->sys_color_changed();
    m_btn_export_gcode_removable->sys_color_changed();

    m_scrolled_panel->Layout();
    m_scrolled_panel->Refresh();
}

ObjectManipulation* Sidebar::obj_manipul()
{
    return m_object_manipulation.get();
}

ObjectList* Sidebar::obj_list()
{
    return m_object_list;
}

ObjectSettings* Sidebar::obj_settings()
{
    return m_object_settings.get();
}

ObjectLayers* Sidebar::obj_layers()
{
    return m_object_layers.get();
}

ConfigOptionsGroup* Sidebar::og_freq_chng_params(const bool is_fff)
{
    return m_frequently_changed_parameters->get_og(is_fff);
}

wxButton* Sidebar::get_wiping_dialog_button()
{
    return m_frequently_changed_parameters->get_wiping_dialog_button();
}

void Sidebar::update_objects_list_extruder_column(size_t extruders_count)
{
    m_object_list->update_objects_list_extruder_column(extruders_count);
}

void Sidebar::show_info_sizer()
{
    Selection& selection = wxGetApp().plater()->canvas3D()->get_selection();
    ModelObjectPtrs objects = m_plater->model().objects;
    const int obj_idx = selection.get_object_idx();
    const int inst_idx = selection.get_instance_idx();

    if (m_mode < comExpert || objects.empty() || obj_idx < 0 || int(objects.size()) <= obj_idx ||
        inst_idx < 0 || int(objects[obj_idx]->instances.size()) <= inst_idx ||
        objects[obj_idx]->volumes.empty() ||                                            // hack to avoid crash when deleting the last object on the bed
        (selection.is_single_full_object() && objects[obj_idx]->instances.size()> 1) ||
        !(selection.is_single_full_instance() || selection.is_single_volume())) {
        m_object_info->Show(false);
        return;
    }

    const ModelObject* model_object = objects[obj_idx];

    bool imperial_units = wxGetApp().app_config->get_bool("use_inches");
    double koef = imperial_units ? ObjectManipulation::mm_to_in : 1.0f;

    ModelVolume* vol = nullptr;
    Transform3d t;
    if (selection.is_single_volume()) {
        std::vector<int> obj_idxs, vol_idxs;
        wxGetApp().obj_list()->get_selection_indexes(obj_idxs, vol_idxs);
        if (vol_idxs.size() != 1)
            // Case when this fuction is called between update selection in ObjectList and on Canvas
            // Like after try to delete last solid part in object, the object is selected in ObjectLIst when just a part is still selected on Canvas
            // see https://github.com/prusa3d/PrusaSlicer/issues/7408
            return;
        vol = model_object->volumes[vol_idxs[0]];
        t = model_object->instances[inst_idx]->get_matrix() * vol->get_matrix();
    }

    Vec3d size = vol ? vol->mesh().transformed_bounding_box(t).size() : model_object->instance_bounding_box(inst_idx).size();
    m_object_info->info_size->SetLabel(wxString::Format("%.2f x %.2f x %.2f", size(0)*koef, size(1)*koef, size(2)*koef));

    const TriangleMeshStats& stats = vol ? vol->mesh().stats() : model_object->get_object_stl_stats();

    double volume_val = stats.volume;
    if (vol)
        volume_val *= std::fabs(t.matrix().block(0, 0, 3, 3).determinant());

    m_object_info->info_volume->SetLabel(wxString::Format("%.2f", volume_val * pow(koef,3)));
    m_object_info->info_facets->SetLabel(format_wxstr(_L_PLURAL("%1% (%2$d shell)", "%1% (%2$d shells)", stats.number_of_parts),
                                                       static_cast<int>(model_object->facets_count()), stats.number_of_parts));

    wxString info_manifold_label;
    auto mesh_errors = obj_list()->get_mesh_errors_info(&info_manifold_label);
    wxString tooltip = mesh_errors.tooltip;
    m_object_info->update_warning_icon(mesh_errors.warning_icon_name);
    m_object_info->info_manifold->SetLabel(info_manifold_label);
    m_object_info->info_manifold->SetToolTip(tooltip);
    m_object_info->manifold_warning_icon->SetToolTip(tooltip);

    m_object_info->show_sizer(true);
    if (vol || model_object->volumes.size() == 1)
        m_object_info->info_icon->Hide();

    if (m_plater->printer_technology() == ptSLA) {
        for (auto item: m_object_info->sla_hidden_items)
            item->Show(false);
    }
}

void Sidebar::update_sliced_info_sizer()
{
    if (m_sliced_info->IsShown(size_t(0)))
    {
        if (m_plater->printer_technology() == ptSLA)
        {
            const SLAPrintStatistics& ps = m_plater->sla_print().print_statistics();
            wxString new_label = _L("Used Material (ml)") + ":";
            const bool is_supports = ps.support_used_material > 0.0;
            if (is_supports)
                new_label += format_wxstr("\n    - %s\n    - %s", _L_PLURAL("object", "objects", m_plater->model().objects.size()), _L("supports and pad"));

            wxString info_text = is_supports ?
                wxString::Format("%.2f \n%.2f \n%.2f", (ps.objects_used_material + ps.support_used_material) / 1000,
                                                       ps.objects_used_material / 1000,
                                                       ps.support_used_material / 1000) :
                wxString::Format("%.2f", (ps.objects_used_material + ps.support_used_material) / 1000);
            m_sliced_info->SetTextAndShow(siMaterial_unit, info_text, new_label);

            wxString str_total_cost = "N/A";

            DynamicPrintConfig* cfg = wxGetApp().get_tab(Preset::TYPE_SLA_MATERIAL)->get_config();
            if (cfg->option("bottle_cost")->getFloat() > 0.0 &&
                cfg->option("bottle_volume")->getFloat() > 0.0)
            {
                double material_cost = cfg->option("bottle_cost")->getFloat() / 
                                       cfg->option("bottle_volume")->getFloat();
                str_total_cost = wxString::Format("%.3f", material_cost*(ps.objects_used_material + ps.support_used_material) / 1000);                
            }
            m_sliced_info->SetTextAndShow(siCost, str_total_cost, "Cost");

            wxString t_est = "N/A";
            if (! std::isnan(ps.estimated_print_time)) {
                t_est = from_u8(short_time_ui(get_time_dhms(float(ps.estimated_print_time))));
                if (ps.estimated_print_time_tolerance > 0.)
                    t_est += from_u8(" \u00B1 ") + from_u8(short_time_ui(get_time_dhms(float(ps.estimated_print_time_tolerance))));
            }

            m_sliced_info->SetTextAndShow(siEstimatedTime, t_est, _L("Estimated printing time") + ":");

            m_plater->get_notification_manager()->set_slicing_complete_print_time(_u8L("Estimated printing time") + ": " + into_u8(t_est), m_plater->is_sidebar_collapsed());

            // Hide non-SLA sliced info parameters
            m_sliced_info->SetTextAndShow(siFilament_m, "N/A");
            m_sliced_info->SetTextAndShow(siFilament_mm3, "N/A");
            m_sliced_info->SetTextAndShow(siFilament_g, "N/A");
            m_sliced_info->SetTextAndShow(siWTNumberOfToolchanges, "N/A");
        }
        else
        {
            const PrintStatistics& ps = m_plater->fff_print().print_statistics();
            const bool is_wipe_tower = ps.total_wipe_tower_filament > 0;

            bool imperial_units = wxGetApp().app_config->get_bool("use_inches");
            double koef = imperial_units ? ObjectManipulation::in_to_mm : 1000.0;

            wxString new_label = imperial_units ? _L("Used Filament (in)") : _L("Used Filament (m)");
            if (is_wipe_tower)
                new_label += format_wxstr(":\n    - %1%\n    - %2%", _L("objects"), _L("wipe tower"));

            wxString info_text = is_wipe_tower ?
                                wxString::Format("%.2f \n%.2f \n%.2f", ps.total_used_filament / koef,
                                                (ps.total_used_filament - ps.total_wipe_tower_filament) / koef,
                                                ps.total_wipe_tower_filament / koef) :
                                wxString::Format("%.2f", ps.total_used_filament / koef);
            m_sliced_info->SetTextAndShow(siFilament_m,    info_text,      new_label);

            koef = imperial_units ? pow(ObjectManipulation::mm_to_in, 3) : 1.0f;
            new_label = imperial_units ? _L("Used Filament (in³)") : _L("Used Filament (mm³)");
            info_text = wxString::Format("%.2f", imperial_units ? ps.total_extruded_volume * koef : ps.total_extruded_volume);
            m_sliced_info->SetTextAndShow(siFilament_mm3,  info_text,      new_label);

            if (ps.total_weight == 0.0)
                m_sliced_info->SetTextAndShow(siFilament_g, "N/A");
            else {
                new_label = _L("Used Filament (g)");
                info_text = wxString::Format("%.2f", ps.total_weight);

                if (ps.filament_stats.size() > 1)
                    new_label += ":";

                const auto& extruders_filaments = wxGetApp().preset_bundle->extruders_filaments;
                for (const auto& [filament_id, filament_vol] : ps.filament_stats) {
                    assert(filament_id < extruders_filaments.size());
                    if (const Preset* preset = extruders_filaments[filament_id].get_selected_preset()) {
                        double filament_weight;
                        if (ps.filament_stats.size() == 1)
                            filament_weight = ps.total_weight;
                        else {
                            double filament_density = preset->config.opt_float("filament_density", 0);
                            filament_weight = filament_vol * filament_density/* *2.4052f*/ * 0.001; // assumes 1.75mm filament diameter;

                            new_label += "\n    - " + format_wxstr(_L("Filament at extruder %1%"), filament_id + 1);
                            info_text += wxString::Format("\n%.2f", filament_weight);
                        }

                        double spool_weight = preset->config.opt_float("filament_spool_weight", 0);
                        if (spool_weight != 0.0) {
                            new_label += "\n      " + _L("(including spool)");
                            info_text += wxString::Format(" (%.2f)\n", filament_weight + spool_weight);
                        }
                    }
                }

                m_sliced_info->SetTextAndShow(siFilament_g, info_text, new_label);
            }

            new_label = _L("Cost");
            if (is_wipe_tower)
                new_label += format_wxstr(":\n    - %1%\n    - %2%", _L("objects"), _L("wipe tower"));

            info_text = ps.total_cost == 0.0 ? "N/A" :
                        is_wipe_tower ?
                        wxString::Format("%.2f \n%.2f \n%.2f", ps.total_cost,
                                            (ps.total_cost - ps.total_wipe_tower_cost),
                                            ps.total_wipe_tower_cost) :
                        wxString::Format("%.2f", ps.total_cost);
            m_sliced_info->SetTextAndShow(siCost, info_text,      new_label);

            if (ps.estimated_normal_print_time == "N/A" && ps.estimated_silent_print_time == "N/A")
                m_sliced_info->SetTextAndShow(siEstimatedTime, "N/A");
            else {
                info_text = "";
                new_label = _L("Estimated printing time") + ":";
                if (ps.estimated_normal_print_time != "N/A") {
                    new_label += format_wxstr("\n   - %1%", _L("normal mode"));
                    info_text += format_wxstr("\n%1%", short_time_ui(ps.estimated_normal_print_time));

                    m_plater->get_notification_manager()->set_slicing_complete_print_time(_u8L("Estimated printing time") + ": " + ps.estimated_normal_print_time, m_plater->is_sidebar_collapsed());

                }
                if (ps.estimated_silent_print_time != "N/A") {
                    new_label += format_wxstr("\n   - %1%", _L("stealth mode"));
                    info_text += format_wxstr("\n%1%", short_time_ui(ps.estimated_silent_print_time));
                }
                m_sliced_info->SetTextAndShow(siEstimatedTime, info_text, new_label);
            }

            m_sliced_info->SetTextAndShow(siWTNumberOfToolchanges, ps.total_toolchanges > 0 ? wxString::Format("%.d", ps.total_toolchanges) : "N/A");

            // Hide non-FFF sliced info parameters
            m_sliced_info->SetTextAndShow(siMaterial_unit, "N/A");
        }
    }

    Layout();
}

void Sidebar::show_sliced_info_sizer(const bool show)
{
    wxWindowUpdateLocker freeze_guard(this);

    m_sliced_info->Show(show);
    if (show)
        update_sliced_info_sizer();

    Layout();
    m_scrolled_panel->Refresh();
}

void Sidebar::enable_buttons(bool enable)
{
    m_btn_reslice->Enable(enable);
    m_btn_export_gcode->Enable(enable);
    m_btn_send_gcode->Enable(enable);
    m_btn_export_gcode_removable->Enable(enable);
    m_btn_connect_gcode->Enable(enable);
}

bool Sidebar::show_reslice(bool show)          const { return m_btn_reslice->Show(show); }
bool Sidebar::show_export(bool show)           const { return m_btn_export_gcode->Show(show); }
bool Sidebar::show_send(bool show)             const { return m_btn_send_gcode->Show(show); }
bool Sidebar::show_export_removable(bool show) const { return m_btn_export_gcode_removable->Show(show); }
bool Sidebar::show_connect(bool show)          const { return m_btn_connect_gcode->Show(show); }


void Sidebar::update_mode()
{
    m_mode = wxGetApp().get_mode();

    update_reslice_btn_tooltip();

    wxWindowUpdateLocker noUpdates(this);

    if (m_mode == comSimple)
        m_object_manipulation->set_coordinates_type(ECoordinatesType::World);

    m_object_list->get_sizer()->Show(m_mode > comSimple);

    m_object_list->unselect_objects();
    m_object_list->update_selections();

    Layout();
}

void Sidebar::set_btn_label(const ActionButtonType btn_type, const wxString& label) const
{
    switch (btn_type)
    {
    case ActionButtonType::Reslice:   m_btn_reslice->SetLabelText(label);        break;
    case ActionButtonType::Export:    m_btn_export_gcode->SetLabelText(label);   break;
    case ActionButtonType::SendGCode: /*m_btn_send_gcode->SetLabelText(label);*/ break;
    case ActionButtonType::Connect: /*m_btn_connect_gcode->SetLabelText(label);*/ break;
    }
}

void Sidebar::collapse(bool collapse)
{
    is_collapsed = collapse;

    this->Show(!collapse);
    m_plater->Layout();

    // save collapsing state to the AppConfig
    if (wxGetApp().is_editor())
        wxGetApp().app_config->set("collapsed_sidebar", collapse ? "1" : "0");
}

void Sidebar::update_ui_from_settings()
{
    m_object_manipulation->update_ui_from_settings();
    show_info_sizer();
    update_sliced_info_sizer();
    m_object_list->apply_volumes_order();
}

void Sidebar::set_extruders_count(size_t extruders_count)
{
    if (extruders_count == m_combos_filament.size())
        return;

    dynamic_cast<TabFilament*>(wxGetApp().get_tab(Preset::TYPE_FILAMENT))->update_extruder_combobox();

    wxWindowUpdateLocker noUpdates_scrolled_panel(this);

    size_t i = m_combos_filament.size();
    while (i < extruders_count)
    {
        PlaterPresetComboBox* filament_choice;
        init_filament_combo(&filament_choice, i);
        m_combos_filament.push_back(filament_choice);

        // initialize selection
        filament_choice->update();
        ++i;
    }

    // remove unused choices if any
    remove_unused_filament_combos(extruders_count);
    
    Layout();
    m_scrolled_panel->Refresh();
}


}}    // namespace Slic3r::GUI
