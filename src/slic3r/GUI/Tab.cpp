///|/ Copyright (c) Prusa Research 2017 - 2023 Oleksandra Iushchenko @YuSanka, Lukáš Matěna @lukasmatena, Lukáš Hejl @hejllukas, Vojtěch Bubník @bubnikv, Pavel Mikuš @Godrak, Tomáš Mészáros @tamasmeszaros, David Kocík @kocikdav, Enrico Turri @enricoturri1966, Vojtěch Král @vojtechkral
///|/ Copyright (c) 2021 Martin Budden
///|/ Copyright (c) 2021 Ilya @xorza
///|/ Copyright (c) 2019 John Drake @foxox
///|/ Copyright (c) 2019 Matthias Urlichs @smurfix
///|/ Copyright (c) 2019 Thomas Moore
///|/ Copyright (c) 2019 Sijmen Schoon
///|/ Copyright (c) 2018 Martin Loidl @LoidlM
///|/
///|/ ported from lib/Slic3r/GUI/Tab.pm:
///|/ Copyright (c) Prusa Research 2016 - 2018 Vojtěch Bubník @bubnikv, Lukáš Matěna @lukasmatena
///|/ Copyright (c) 2015 - 2017 Joseph Lenox @lordofhyphens
///|/ Copyright (c) Slic3r 2012 - 2016 Alessandro Ranellucci @alranel
///|/ Copyright (c) 2016 Chow Loong Jin @hyperair
///|/ Copyright (c) 2012 QuantumConcepts
///|/ Copyright (c) 2012 Henrik Brix Andersen @henrikbrixandersen
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
// #include "libslic3r/GCodeSender.hpp"
#include "slic3r/GUI/BedShapeDialog.hpp"
#include "slic3r/Utils/Serial.hpp"
#include "Tab.hpp"
#include "PresetHints.hpp"
#include "libslic3r/PresetBundle.hpp"
#include "libslic3r/Utils.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/SLAPrint.hpp"
#include "libslic3r/GCode/GCodeProcessor.hpp"
#include "libslic3r/GCode/GCodeWriter.hpp"
#include "libslic3r/GCode/Thumbnails.hpp"

#include "slic3r/Utils/Http.hpp"
#include "slic3r/Utils/PrintHost.hpp"
#include "BonjourDialog.hpp"
#include "WipeTowerDialog.hpp"
#include "ButtonsDescription.hpp"
#include "Search.hpp"
#include "OG_CustomCtrl.hpp"

#include <wx/app.h>
#include <wx/button.h>
#include <wx/scrolwin.h>
#include <wx/sizer.h>

#include <wx/bmpcbox.h>
#include <wx/bmpbuttn.h>
#include <wx/treectrl.h>
#include <wx/imaglist.h>
#include <wx/settings.h>
#include <wx/filedlg.h>

#include <boost/algorithm/string/predicate.hpp>
#include <boost/algorithm/string/replace.hpp>
#include <boost/filesystem.hpp>
#include <boost/exception/diagnostic_information.hpp>

#include "wxExtensions.hpp"
#include "PresetComboBoxes.hpp"
#include <wx/wupdlock.h>

#include "GUI_App.hpp"
#include "GUI_ObjectList.hpp"
#include "Plater.hpp"
#include "MainFrame.hpp"
#include "format.hpp"
#include "UnsavedChangesDialog.hpp"
#include "SavePresetDialog.hpp"
#include "EditGCodeDialog.hpp"
#include "MsgDialog.hpp"
#include "TopBar.hpp"

#include "Widgets/CheckBox.hpp"

#ifdef WIN32
	#include <CommCtrl.h>
#endif // WIN32

namespace Slic3r {
namespace GUI {

Tab::Tab(wxBookCtrlBase* parent, const wxString& title, Preset::Type type) :
    m_parent(parent), m_type(type), m_title(title)
{
    Create(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBK_LEFT | wxTAB_TRAVERSAL/*, name*/);
    this->SetFont(Slic3r::GUI::wxGetApp().normal_font());

#ifdef __WXMSW__
    wxGetApp().UpdateDarkUI(this);
#elif __WXOSX__
    SetBackgroundColour(parent->GetBackgroundColour());
#elif __WXGTK3__
    SetBackgroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW));
#endif

    m_compatible_printers.type			= Preset::TYPE_PRINTER;
    m_compatible_printers.key_list		= "compatible_printers";
    m_compatible_printers.key_condition	= "compatible_printers_condition";
    m_compatible_printers.dialog_title  = _L("Compatible printers");
    m_compatible_printers.dialog_label  = _L("Select the printers this profile is compatible with.");

    m_compatible_prints.type			= Preset::TYPE_PRINT;
    m_compatible_prints.key_list 		= "compatible_prints";
    m_compatible_prints.key_condition	= "compatible_prints_condition";
    m_compatible_prints.dialog_title 	= _L("Compatible print profiles");
    m_compatible_prints.dialog_label 	= _L("Select the print profiles this profile is compatible with.");

    wxGetApp().tabs_list.push_back(this);

    m_em_unit = em_unit(m_parent); //wxGetApp().em_unit();

    m_config_manipulation = get_config_manipulation();

    Bind(wxEVT_SIZE, ([](wxSizeEvent &evt) {
        //for (auto page : m_pages)
        //    if (! page.get()->IsShown())
        //        page->layout_valid = false;
        evt.Skip();
    }));

    m_highlighter.set_timer_owner(this, 0);
}

void Tab::set_type()
{
    if (m_name == "print")              { m_type = Slic3r::Preset::TYPE_PRINT; }
    else if (m_name == "sla_print")     { m_type = Slic3r::Preset::TYPE_SLA_PRINT; }
    else if (m_name == "filament")      { m_type = Slic3r::Preset::TYPE_FILAMENT; }
    else if (m_name == "sla_material")  { m_type = Slic3r::Preset::TYPE_SLA_MATERIAL; }
    else if (m_name == "printer")       { m_type = Slic3r::Preset::TYPE_PRINTER; }
    else                                { m_type = Slic3r::Preset::TYPE_INVALID; assert(false); }
}

// sub new
void Tab::create_preset_tab()
{
#ifdef __WINDOWS__
    SetDoubleBuffered(true);
#endif //__WINDOWS__

    m_preset_bundle = wxGetApp().preset_bundle;

    // Vertical sizer to hold the choice menu and the rest of the page.
#ifdef __WXOSX__
    auto  *main_sizer = new wxBoxSizer(wxVERTICAL);
    main_sizer->SetSizeHints(this);
    this->SetSizer(main_sizer);

    // Create additional panel to Fit() it from OnActivate()
    // It's needed for tooltip showing on OSX
    m_tmp_panel = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBK_LEFT | wxTAB_TRAVERSAL);
    auto panel = m_tmp_panel;
    auto  sizer = new wxBoxSizer(wxVERTICAL);
    m_tmp_panel->SetSizer(sizer);
    m_tmp_panel->Layout();

    main_sizer->Add(m_tmp_panel, 1, wxEXPAND | wxALL, 0);
#else
    Tab *panel = this;
    auto  *sizer = new wxBoxSizer(wxVERTICAL);
    sizer->SetSizeHints(panel);
    panel->SetSizer(sizer);
#endif //__WXOSX__

    // preset chooser
    m_presets_choice = new TabPresetComboBox(panel, m_type);
    m_presets_choice->set_selection_changed_function([this](int selection) {
        if (!m_presets_choice->selection_is_changed_according_to_physical_printers())
        {
            if (m_type == Preset::TYPE_PRINTER && !m_presets_choice->is_selected_physical_printer())
                m_preset_bundle->physical_printers.unselect_printer();

            // select preset
            std::string preset_name = m_presets_choice->GetString(selection).ToUTF8().data();
            select_preset(Preset::remove_suffix_modified(preset_name));
        }
    });

    //buttons
    m_scaled_buttons.reserve(6);
    m_scaled_buttons.reserve(2);

    add_scaled_button(panel, &m_btn_compare_preset, "compare");
    add_scaled_button(panel, &m_btn_save_preset, "save");
    add_scaled_button(panel, &m_btn_rename_preset, "edit");
    add_scaled_button(panel, &m_btn_delete_preset, "cross");
    if (m_type == Preset::Type::TYPE_PRINTER)
        add_scaled_button(panel, &m_btn_edit_ph_printer, "cog");

    m_show_incompatible_presets = false;

    add_scaled_button(panel, &m_btn_hide_incompatible_presets, "flag_green");

    //TRN Settings Tab: tooltip for toolbar button
    m_btn_compare_preset->SetToolTip(_L("Compare preset with another"));
    //TRN Settings Tab: tooltip for toolbar button
    m_btn_save_preset  ->SetToolTip(_L("Save preset"));
    //TRN Settings Tab: tooltip for toolbar button
    m_btn_rename_preset->SetToolTip(_L("Rename preset"));
    m_btn_rename_preset->Hide();
    //TRN Settings Tab: tooltip for toolbar button
    m_btn_delete_preset->SetToolTip(_(L("Delete preset")));
    m_btn_delete_preset->Hide();

    add_scaled_button(panel, &m_question_btn, "question");
    m_question_btn->SetToolTip(_(L("Hover the cursor over buttons to find more information \n"
                                   "or click this button.")));

    // Bitmaps to be shown on the "Revert to system" aka "Lock to system" button next to each input field.
    add_scaled_bitmap(this, m_bmp_value_lock  , "lock_closed");
    add_scaled_bitmap(this, m_bmp_value_unlock, "lock_open");
    m_bmp_non_system = &m_bmp_white_bullet;
    // Bitmaps to be shown on the "Undo user changes" button next to each input field.
    add_scaled_bitmap(this, m_bmp_value_revert, "undo");
    add_scaled_bitmap(this, m_bmp_white_bullet, "dot");
    // Bitmap to be shown on the "edit" button before to each editable input field.
    add_scaled_bitmap(this, m_bmp_edit_value, "edit");

    fill_icon_descriptions();
    set_tooltips_text();

    add_scaled_button(panel, &m_undo_btn,        m_bmp_white_bullet.name());
    add_scaled_button(panel, &m_undo_to_sys_btn, m_bmp_white_bullet.name());

    m_undo_btn->Bind(wxEVT_BUTTON, ([this](wxCommandEvent) { on_roll_back_value(); }));
    m_undo_to_sys_btn->Bind(wxEVT_BUTTON, ([this](wxCommandEvent) { on_roll_back_value(true); }));
    m_question_btn->Bind(wxEVT_BUTTON, [this](wxCommandEvent) {
        GUI_Descriptions::Dialog dlg(this, m_icon_descriptions);
        if (dlg.ShowModal() == wxID_OK)
            wxGetApp().update_label_colours();
    });

    // Colors for ui "decoration"
    m_sys_label_clr			= wxGetApp().get_label_clr_sys();
    m_modified_label_clr	= wxGetApp().get_label_clr_modified();
    m_default_text_clr		= wxGetApp().get_label_clr_default();

    const float scale_factor = em_unit(this)*0.1;// GetContentScaleFactor();
    m_top_hsizer = new wxBoxSizer(wxHORIZONTAL);
    sizer->Add(m_top_hsizer, 0, wxEXPAND | wxBOTTOM, 3);
    m_top_hsizer->Add(m_presets_choice, 0, wxLEFT | wxRIGHT | wxTOP | wxALIGN_CENTER_VERTICAL, 3);
    m_top_hsizer->AddSpacer(int(4*scale_factor));

    m_h_buttons_sizer = new wxBoxSizer(wxHORIZONTAL);
    m_h_buttons_sizer->Add(m_btn_save_preset, 0, wxALIGN_CENTER_VERTICAL);
    m_h_buttons_sizer->AddSpacer(int(4*scale_factor));
    m_h_buttons_sizer->Add(m_btn_rename_preset, 0, wxALIGN_CENTER_VERTICAL);
    m_h_buttons_sizer->AddSpacer(int(4 * scale_factor));
    m_h_buttons_sizer->Add(m_btn_delete_preset, 0, wxALIGN_CENTER_VERTICAL);
    if (m_btn_edit_ph_printer) {
        m_h_buttons_sizer->AddSpacer(int(4 * scale_factor));
        m_h_buttons_sizer->Add(m_btn_edit_ph_printer, 0, wxALIGN_CENTER_VERTICAL);
    }
    m_h_buttons_sizer->AddSpacer(int(/*16*/8 * scale_factor));
    m_h_buttons_sizer->Add(m_btn_hide_incompatible_presets, 0, wxALIGN_CENTER_VERTICAL);
    m_h_buttons_sizer->AddSpacer(int(8 * scale_factor));
    m_h_buttons_sizer->Add(m_question_btn, 0, wxALIGN_CENTER_VERTICAL);
    m_h_buttons_sizer->AddSpacer(int(32 * scale_factor));
    m_h_buttons_sizer->Add(m_undo_to_sys_btn, 0, wxALIGN_CENTER_VERTICAL);
    m_h_buttons_sizer->Add(m_undo_btn, 0, wxALIGN_CENTER_VERTICAL);
    m_h_buttons_sizer->AddSpacer(int(8*scale_factor));
    m_h_buttons_sizer->Add(m_btn_compare_preset, 0, wxALIGN_CENTER_VERTICAL);

    m_top_hsizer->Add(m_h_buttons_sizer, 1, wxEXPAND);
    m_top_hsizer->AddSpacer(int(16*scale_factor));
    // StretchSpacer has a strange behavior under OSX, so
    // hide whole top sizer to correct layout later
    m_top_hsizer->ShowItems(false);

    //Horizontal sizer to hold the tree and the selected page.
    m_hsizer = new wxBoxSizer(wxHORIZONTAL);
    sizer->Add(m_hsizer, 1, wxEXPAND, 0);

    //left vertical sizer
    m_left_sizer = new wxBoxSizer(wxVERTICAL);
    m_hsizer->Add(m_left_sizer, 0, wxEXPAND | wxLEFT | wxTOP | wxBOTTOM, 3);

    // tree
    m_treectrl = new wxTreeCtrl(panel, wxID_ANY, wxDefaultPosition, wxSize(20 * m_em_unit, -1),
        wxTR_NO_BUTTONS | wxTR_HIDE_ROOT | wxTR_SINGLE | wxTR_NO_LINES | wxBORDER_SUNKEN | wxWANTS_CHARS);
    m_treectrl->SetFont(wxGetApp().normal_font());
#ifdef __linux__
    m_treectrl->SetBackgroundColour(m_parent->GetBackgroundColour());
#endif
    m_left_sizer->Add(m_treectrl, 1, wxEXPAND);
    // Index of the last icon inserted into m_treectrl
    m_icon_count = -1;
    m_treectrl->AddRoot("root");
    m_treectrl->SetIndent(0);
    wxGetApp().UpdateDarkUI(m_treectrl);

    // Delay processing of the following handler until the message queue is flushed.
    // This helps to process all the cursor key events on Windows in the tree control,
    // so that the cursor jumps to the last item.
    m_treectrl->Bind(wxEVT_TREE_SEL_CHANGED, [this](wxTreeEvent&) {
#ifdef __linux__
        // Events queue is opposite On Linux. wxEVT_SET_FOCUS invokes after wxEVT_TREE_SEL_CHANGED,
        // and a result wxEVT_KILL_FOCUS doesn't invoke for the TextCtrls.
        // see https://github.com/prusa3d/PrusaSlicer/issues/5720
        // So, call SetFocus explicitly for this control before changing of the selection
        m_treectrl->SetFocus();
#endif
            if (!m_disable_tree_sel_changed_event && !m_pages.empty()) {
                if (m_page_switch_running)
                    m_page_switch_planned = true;
                else {
                    m_page_switch_running = true;
                    do {
                        m_page_switch_planned = false;
                        m_treectrl->Update();
                    } while (this->tree_sel_change_delayed());
                    m_page_switch_running = false;
                }
            }
        });

    m_treectrl->Bind(wxEVT_KEY_DOWN, &Tab::OnKeyDown, this);

    // Initialize the page.
#ifdef __WXOSX__
    auto page_parent = m_tmp_panel;
#else
    auto page_parent = this;
#endif

    m_page_view = new wxScrolledWindow(page_parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTAB_TRAVERSAL);
    m_page_sizer = new wxBoxSizer(wxVERTICAL);
    m_page_view->SetSizer(m_page_sizer);
    m_page_view->SetScrollbars(1, 20, 1, 2);
    m_hsizer->Add(m_page_view, 1, wxEXPAND | wxLEFT, 5);

    m_btn_compare_preset->Bind(wxEVT_BUTTON, ([this](wxCommandEvent e) { compare_preset(); }));
    m_btn_save_preset->Bind(wxEVT_BUTTON, ([this](wxCommandEvent e) { save_preset(); }));
    m_btn_rename_preset->Bind(wxEVT_BUTTON, ([this](wxCommandEvent e) { rename_preset(); }));
    m_btn_delete_preset->Bind(wxEVT_BUTTON, ([this](wxCommandEvent e) { delete_preset(); }));
    m_btn_hide_incompatible_presets->Bind(wxEVT_BUTTON, ([this](wxCommandEvent e) {
        toggle_show_hide_incompatible();
    }));

    if (m_btn_edit_ph_printer)
        m_btn_edit_ph_printer->Bind(wxEVT_BUTTON, [this](wxCommandEvent e) {
            if (m_preset_bundle->physical_printers.has_selection())
                m_presets_choice->edit_physical_printer();
            else
                m_presets_choice->add_physical_printer();
        });

    // Initialize the DynamicPrintConfig by default keys/values.
    build();

    if (!m_scaled_icons_list.empty()) {
        // update icons for tree_ctrl
        wxVector<wxBitmapBundle> img_bundles;
        for (const ScalableBitmap& bmp : m_scaled_icons_list)
            img_bundles.push_back(bmp.bmp());
        m_treectrl->SetImages(img_bundles);
    }

    // ys_FIXME: Following should not be needed, the function will be called later
    // (update_mode->update_visibility->rebuild_page_tree). This does not work, during the
    // second call of rebuild_page_tree m_treectrl->GetFirstVisibleItem(); returns zero
    // for some unknown reason (and the page is not refreshed until user does a selection).
    rebuild_page_tree();

    m_completed = true;
}

void Tab::add_scaled_button(wxWindow* parent,
                            ScalableButton** btn,
                            const std::string& icon_name,
                            const wxString& label/* = wxEmptyString*/,
                            long style /*= wxBU_EXACTFIT | wxNO_BORDER*/)
{
    *btn = new ScalableButton(parent, wxID_ANY, icon_name, label, wxDefaultSize, wxDefaultPosition, style);
    m_scaled_buttons.push_back(*btn);
}

void Tab::add_scaled_bitmap(wxWindow* parent,
                            ScalableBitmap& bmp,
                            const std::string& icon_name)
{
    bmp = ScalableBitmap(parent, icon_name);
    m_scaled_bitmaps.push_back(&bmp);
}

void Tab::load_initial_data()
{
    m_config = &m_presets->get_edited_preset().config;
    bool has_parent = m_presets->get_selected_preset_parent() != nullptr;
    m_bmp_non_system = has_parent ? &m_bmp_value_unlock : &m_bmp_white_bullet;
    m_ttg_non_system = has_parent ? &m_ttg_value_unlock : &m_ttg_white_bullet_ns;
    m_tt_non_system  = has_parent ? &m_tt_value_unlock  : &m_ttg_white_bullet_ns;
}

Slic3r::GUI::PageShp Tab::add_options_page(const wxString& title, const std::string& icon, bool is_extruder_pages /*= false*/)
{
    // Index of icon in an icon list $self->{icons}.
    auto icon_idx = 0;
    if (!icon.empty()) {
        icon_idx = (m_icon_index.find(icon) == m_icon_index.end()) ? -1 : m_icon_index.at(icon);
        if (icon_idx == -1) {
            // Add a new icon to the icon list.
            m_scaled_icons_list.push_back(ScalableBitmap(this, icon));
            icon_idx = ++m_icon_count;
            m_icon_index[icon] = icon_idx;
        }

        if (m_category_icon.find(title) == m_category_icon.end()) {
            // Add new category to the category_to_icon list.
            m_category_icon[title] = icon;
        }
    }
    // Initialize the page.
    PageShp page(new Page(m_page_view, title, icon_idx));

    if (!is_extruder_pages)
        m_pages.push_back(page);

    page->set_config(m_config);
    return page;
}

// Names of categories is save in English always. We translate them only for UI.
// But category "Extruder n" can't be translated regularly (using _()), so
// just for this category we should splite the title and translate "Extruder" word separately
wxString Tab::translate_category(const wxString& title, Preset::Type preset_type)
{
    if (preset_type == Preset::TYPE_PRINTER && title.Contains("Extruder ")) {
        return _("Extruder") + title.SubString(8, title.Last());
    }
    return _(title);
}

void Tab::OnActivate()
{
    wxWindowUpdateLocker noUpdates(this);
#ifdef __WXOSX__
//    wxWindowUpdateLocker noUpdates(this);
    auto size = GetSizer()->GetSize();
    m_tmp_panel->GetSizer()->SetMinSize(size.x + m_size_move, size.y);
    Fit();
    m_size_move *= -1;
#endif // __WXOSX__

#ifdef __WXMSW__
    // Workaround for tooltips over Tree Controls displayed over excessively long
    // tree control items, stealing the window focus.
    //
    // In case the Tab was reparented from the MainFrame to the floating dialog,
    // the tooltip created by the Tree Control before reparenting is not reparented, 
    // but it still points to the MainFrame. If the tooltip pops up, the MainFrame 
    // is incorrectly focussed, stealing focus from the floating dialog.
    //
    // The workaround is to delete the tooltip control.
    // Vojtech tried to reparent the tooltip control, but it did not work,
    // and if the Tab was later reparented back to MainFrame, the tooltip was displayed
    // at an incorrect position, therefore it is safer to just discard the tooltip control
    // altogether.
    HWND hwnd_tt = TreeView_GetToolTips(m_treectrl->GetHandle());
    if (hwnd_tt) {
	    HWND hwnd_toplevel 	= find_toplevel_parent(m_treectrl)->GetHandle();
	    HWND hwnd_parent 	= ::GetParent(hwnd_tt);
	    if (hwnd_parent != hwnd_toplevel) {
	    	::DestroyWindow(hwnd_tt);
			TreeView_SetToolTips(m_treectrl->GetHandle(), nullptr);
	    }
    }
#endif

    // create controls on active page
    activate_selected_page([](){});
    m_hsizer->Layout();

    if (m_presets_choice->IsShown())
        Refresh(); // Just refresh page, if m_presets_choice is already shown
    else {
        // From the tab creation whole top sizer is hidden to correct update of preset combobox's size
        // (see https://github.com/prusa3d/PrusaSlicer/issues/10746)

        // On first OnActivate call show top sizer
        m_top_hsizer->ShowItems(true);
        // Size and layouts of all items are correct now,
        // but ALL items of top sizer are visible.
        // So, update visibility of each item according to the ui settings
        update_btns_enabling();
        m_btn_hide_incompatible_presets->Show(m_show_btn_incompatible_presets && m_type != Slic3r::Preset::TYPE_PRINTER);
        if (TabFilament* tab = dynamic_cast<TabFilament*>(this))
            tab->update_extruder_combobox_visibility();

        Layout();
    }
}

void Tab::update_label_colours()
{
    m_default_text_clr = wxGetApp().get_label_clr_default();
    if (m_sys_label_clr == wxGetApp().get_label_clr_sys() && m_modified_label_clr == wxGetApp().get_label_clr_modified())
        return;
    m_sys_label_clr = wxGetApp().get_label_clr_sys();
    m_modified_label_clr = wxGetApp().get_label_clr_modified();

    //update options "decoration"
    for (const auto& opt : m_options_list)
    {
        const wxColour *color = &m_sys_label_clr;

        // value isn't equal to system value
        if ((opt.second & osSystemValue) == 0) {
            // value is equal to last saved
            if ((opt.second & osInitValue) != 0)
                color = &m_default_text_clr;
            // value is modified
            else
                color = &m_modified_label_clr;
        }
        if (OptionsGroup::is_option_without_field(opt.first)) {
            if (Line* line = get_line(opt.first))
                line->set_label_colour(color);
            continue;
        }

        Field* field = get_field(opt.first);
        if (field == nullptr) continue;
        field->set_label_colour(color);
    }

    auto cur_item = m_treectrl->GetFirstVisibleItem();
    if (!cur_item || !m_treectrl->IsVisible(cur_item))
        return;
    while (cur_item) {
        auto title = m_treectrl->GetItemText(cur_item);
        for (auto page : m_pages)
        {
            if (translate_category(page->title(), m_type) != title)
                continue;

            const wxColor *clr = !page->m_is_nonsys_values ? &m_sys_label_clr :
                page->m_is_modified_values ? &m_modified_label_clr :
                &m_default_text_clr;

            m_treectrl->SetItemTextColour(cur_item, *clr);
            break;
        }
        cur_item = m_treectrl->GetNextVisible(cur_item);
    }

    decorate();
}

void Tab::decorate()
{
    for (const auto& opt : m_options_list)
    {
        Field*      field = nullptr;
        bool        option_without_field = false;

        if(OptionsGroup::is_option_without_field(opt.first))
            option_without_field = true;

        if (!option_without_field) {
            field = get_field(opt.first);
            if (!field)
                continue;
        }

        bool is_nonsys_value = false;
        bool is_modified_value = true;
        const ScalableBitmap* sys_icon  = &m_bmp_value_lock;
        const ScalableBitmap* icon      = &m_bmp_value_revert;

        const wxColour* color = m_is_default_preset ? &m_default_text_clr : &m_sys_label_clr;

        const wxString* sys_tt  = &m_tt_value_lock;
        const wxString* tt      = &m_tt_value_revert;

        // value isn't equal to system value
        if ((opt.second & osSystemValue) == 0) {
            is_nonsys_value = true;
            sys_icon = m_bmp_non_system;
            sys_tt = m_tt_non_system;
            // value is equal to last saved
            if ((opt.second & osInitValue) != 0)
                color = &m_default_text_clr;
            // value is modified
            else
                color = &m_modified_label_clr;
        }
        if ((opt.second & osInitValue) != 0)
        {
            is_modified_value = false;
            icon = &m_bmp_white_bullet;
            tt = &m_tt_white_bullet;
        }

        if (option_without_field) {
            if (Line* line = get_line(opt.first)) {
                line->set_undo_bitmap(icon);
                line->set_undo_to_sys_bitmap(sys_icon);
                line->set_undo_tooltip(tt);
                line->set_undo_to_sys_tooltip(sys_tt);
                line->set_label_colour(color);
            }
            continue;
        }
        
        field->m_is_nonsys_value = is_nonsys_value;
        field->m_is_modified_value = is_modified_value;
        field->set_undo_bitmap(icon);
        field->set_undo_to_sys_bitmap(sys_icon);
        field->set_undo_tooltip(tt);
        field->set_undo_to_sys_tooltip(sys_tt);
        field->set_label_colour(color);

        if (field->has_edit_ui())
            field->set_edit_bitmap(&m_bmp_edit_value);

    }

    if (m_active_page)
        m_active_page->refresh();
}

// Update UI according to changes
void Tab::update_changed_ui()
{
    if (m_postpone_update_ui)
        return;

    const bool deep_compare = m_type != Preset::TYPE_FILAMENT;
    auto dirty_options = m_presets->current_dirty_options(deep_compare);
    auto nonsys_options = m_presets->current_different_from_parent_options(deep_compare);
    if (m_type == Preset::TYPE_PRINTER) {
        {
            auto check_bed_custom_options = [](std::vector<std::string>& keys) {
                size_t old_keys_size = keys.size();
                keys.erase(std::remove_if(keys.begin(), keys.end(), [](const std::string& key) { 
                    return key == "bed_custom_texture" || key == "bed_custom_model"; }), keys.end());
                if (old_keys_size != keys.size() && std::find(keys.begin(), keys.end(), "bed_shape") == keys.end())
                    keys.emplace_back("bed_shape");
            };
            check_bed_custom_options(dirty_options);
            check_bed_custom_options(nonsys_options);
        }

        if (static_cast<TabPrinter*>(this)->m_printer_technology == ptFFF) {
            TabPrinter* tab = static_cast<TabPrinter*>(this);
            if (tab->m_initial_extruders_count != tab->m_extruders_count)
                dirty_options.emplace_back("extruders_count");
            if (tab->m_sys_extruders_count != tab->m_extruders_count)
                nonsys_options.emplace_back("extruders_count");
        }
    }

    for (auto& it : m_options_list)
        it.second = m_opt_status_value;

    for (auto opt_key : dirty_options)	m_options_list[opt_key] &= ~osInitValue;
    for (auto opt_key : nonsys_options)	m_options_list[opt_key] &= ~osSystemValue;

    decorate();

    wxTheApp->CallAfter([this]() {
        if (parent()) //To avoid a crash, parent should be exist for a moment of a tree updating
            update_changed_tree_ui();
    });
}

void Tab::init_options_list()
{
    m_options_list.clear();

    for (const std::string& opt_key : m_config->keys())
        emplace_option(opt_key, m_type != Preset::TYPE_FILAMENT && !PresetCollection::is_independent_from_extruder_number_option(opt_key));
}

template<class T>
void add_correct_opts_to_options_list(const std::string &opt_key, std::map<std::string, int>& map, Tab *tab, int value)
{
    T *opt_cur = static_cast<T*>(tab->m_config->option(opt_key));
    for (size_t i = 0; i < opt_cur->values.size(); i++)
        map.emplace(opt_key + "#" + std::to_string(i), value);
}

void Tab::emplace_option(const std::string& opt_key, bool respect_vec_values/* = false*/)
{
    if (respect_vec_values) {
        switch (m_config->option(opt_key)->type())
        {
        case coInts:	add_correct_opts_to_options_list<ConfigOptionInts		>(opt_key, m_options_list, this, m_opt_status_value);	break;
        case coBools:	add_correct_opts_to_options_list<ConfigOptionBools		>(opt_key, m_options_list, this, m_opt_status_value);	break;
        case coFloats:	add_correct_opts_to_options_list<ConfigOptionFloats		>(opt_key, m_options_list, this, m_opt_status_value);	break;
        case coStrings:	add_correct_opts_to_options_list<ConfigOptionStrings	>(opt_key, m_options_list, this, m_opt_status_value);	break;
        case coPercents:add_correct_opts_to_options_list<ConfigOptionPercents	>(opt_key, m_options_list, this, m_opt_status_value);	break;
        case coPoints:	add_correct_opts_to_options_list<ConfigOptionPoints		>(opt_key, m_options_list, this, m_opt_status_value);	break;
        case coFloatsOrPercents:	add_correct_opts_to_options_list<ConfigOptionFloatsOrPercents		>(opt_key, m_options_list, this, m_opt_status_value);	break;
        case coEnums:	add_correct_opts_to_options_list<ConfigOptionEnumsGeneric>(opt_key, m_options_list, this, m_opt_status_value);	break;
        default:		m_options_list.emplace(opt_key, m_opt_status_value);		break;
        }
    }
    else 
        m_options_list.emplace(opt_key, m_opt_status_value);
}

void TabPrinter::init_options_list()
{
    Tab::init_options_list();

    if (m_printer_technology == ptFFF)
        m_options_list.emplace("extruders_count", m_opt_status_value);
}

void Tab::get_sys_and_mod_flags(const std::string& opt_key, bool& sys_page, bool& modified_page)
{
    auto opt = m_options_list.find(opt_key);
    if (opt == m_options_list.end()) 
        return;

    if (sys_page) sys_page = (opt->second & osSystemValue) != 0;
    modified_page |= (opt->second & osInitValue) == 0;
}

void Tab::update_changed_tree_ui()
{
    if (m_options_list.empty())
        return;
    auto cur_item = m_treectrl->GetFirstVisibleItem();
    if (!cur_item || !m_treectrl->IsVisible(cur_item))
        return;

    auto selected_item = m_treectrl->GetSelection();
    auto selection = selected_item ? m_treectrl->GetItemText(selected_item) : "";

    while (cur_item) {
        auto title = m_treectrl->GetItemText(cur_item);
        for (auto page : m_pages)
        {
            if (translate_category(page->title(), m_type) != title)
                continue;
            bool sys_page = true;
            bool modified_page = false;
            if (page->title() == "General") {
                std::initializer_list<const char*> optional_keys{ "extruders_count", "bed_shape" };
                for (auto &opt_key : optional_keys) {
                    get_sys_and_mod_flags(opt_key, sys_page, modified_page);
                }
            }
            if (m_type == Preset::TYPE_FILAMENT && page->title() == "Advanced") {
                get_sys_and_mod_flags("filament_ramming_parameters", sys_page, modified_page);
            }
            if (page->title() == "Dependencies") {
                if (m_type == Slic3r::Preset::TYPE_PRINTER) {
                    sys_page = m_presets->get_selected_preset_parent() != nullptr;
                    modified_page = false;
                } else {
                    if (m_type == Slic3r::Preset::TYPE_FILAMENT || m_type == Slic3r::Preset::TYPE_SLA_MATERIAL)
                        get_sys_and_mod_flags("compatible_prints", sys_page, modified_page);
                    get_sys_and_mod_flags("compatible_printers", sys_page, modified_page);
                }
            }
            for (auto group : page->m_optgroups)
            {
                if (!sys_page && modified_page)
                    break;
                for (const auto &kvp : group->opt_map()) {
                    const std::string& opt_key = kvp.first;
                    get_sys_and_mod_flags(opt_key, sys_page, modified_page);
                }
            }

            const wxColor *clr = sys_page		?	(m_is_default_preset ? &m_default_text_clr : &m_sys_label_clr) :
                                 modified_page	?	&m_modified_label_clr :
                                                    &m_default_text_clr;

            if (page->set_item_colour(clr))
                m_treectrl->SetItemTextColour(cur_item, *clr);

            page->m_is_nonsys_values = !sys_page;
            page->m_is_modified_values = modified_page;

            if (selection == title) {
                m_is_nonsys_values = page->m_is_nonsys_values;
                m_is_modified_values = page->m_is_modified_values;
            }
            break;
        }
        auto next_item = m_treectrl->GetNextVisible(cur_item);
        cur_item = next_item;
    }
    update_undo_buttons();
}

void Tab::update_undo_buttons()
{
    m_undo_btn->        SetBitmap_(m_is_modified_values ? m_bmp_value_revert.name(): m_bmp_white_bullet.name());
    m_undo_to_sys_btn-> SetBitmap_(m_is_nonsys_values   ? m_bmp_non_system->name() : m_bmp_value_lock.name());

    //m_undo_btn->        SetBitmap_(m_is_modified_values ? m_bmp_value_revert: m_bmp_white_bullet);
    //m_undo_to_sys_btn-> SetBitmap_(m_is_nonsys_values   ? *m_bmp_non_system : m_bmp_value_lock);

    m_undo_btn->SetToolTip(m_is_modified_values ? m_ttg_value_revert : m_ttg_white_bullet);
    m_undo_to_sys_btn->SetToolTip(m_is_nonsys_values ? *m_ttg_non_system : m_ttg_value_lock);
}

void Tab::on_roll_back_value(const bool to_sys /*= true*/)
{
    if (!m_active_page) return;

    int os;
    if (to_sys)	{
        if (!m_is_nonsys_values) return;
        os = osSystemValue;
    }
    else {
        if (!m_is_modified_values) return;
        os = osInitValue;
    }

    m_postpone_update_ui = true;

    for (auto group : m_active_page->m_optgroups) {
        if (group->title == "Capabilities") {
            if ((m_options_list["extruders_count"] & os) == 0)
                to_sys ? group->back_to_sys_value("extruders_count") : group->back_to_initial_value("extruders_count");
        }
        if (group->title == "Size and coordinates") {
            if ((m_options_list["bed_shape"] & os) == 0) {
                to_sys ? group->back_to_sys_value("bed_shape") : group->back_to_initial_value("bed_shape");
                load_key_value("bed_shape", true/*some value*/, true);
            }
        }
        if (group->title == "Toolchange parameters with single extruder MM printers") {
            if ((m_options_list["filament_ramming_parameters"] & os) == 0)
                to_sys ? group->back_to_sys_value("filament_ramming_parameters") : group->back_to_initial_value("filament_ramming_parameters");
        }
        if (group->title == "G-code Substitutions") {
            if ((m_options_list["gcode_substitutions"] & os) == 0) {
                to_sys ? group->back_to_sys_value("gcode_substitutions") : group->back_to_initial_value("gcode_substitutions");
                load_key_value("gcode_substitutions", true/*some value*/, true);
            }
        }
        if (group->title == "Profile dependencies") {
            // "compatible_printers" option doesn't exists in Printer Settimgs Tab
            if (m_type != Preset::TYPE_PRINTER && (m_options_list["compatible_printers"] & os) == 0) {
                to_sys ? group->back_to_sys_value("compatible_printers") : group->back_to_initial_value("compatible_printers");
                load_key_value("compatible_printers", true/*some value*/, true);
            }
            // "compatible_prints" option exists only in Filament Settimgs and Materials Tabs
            if ((m_type == Preset::TYPE_FILAMENT || m_type == Preset::TYPE_SLA_MATERIAL) && (m_options_list["compatible_prints"] & os) == 0) {
                to_sys ? group->back_to_sys_value("compatible_prints") : group->back_to_initial_value("compatible_prints");
                load_key_value("compatible_prints", true/*some value*/, true);
            }
        }
        for (const auto &kvp : group->opt_map()) {
            const std::string& opt_key = kvp.first;
            if ((m_options_list[opt_key] & os) == 0)
                to_sys ? group->back_to_sys_value(opt_key) : group->back_to_initial_value(opt_key);
        }
    }

    m_postpone_update_ui = false;

    // When all values are rolled, then we have to update whole tab in respect to the reverted values
    update();

    update_changed_ui();
}

// Update the combo box label of the selected preset based on its "dirty" state,
// comparing the selected preset config with $self->{config}.
void Tab::update_dirty()
{
    m_presets_choice->update_dirty();
    on_presets_changed();
    update_changed_ui();
}

void Tab::update_tab_ui()
{
    m_presets_choice->update();
}

// Load a provied DynamicConfig into the tab, modifying the active preset.
// This could be used for example by setting a Wipe Tower position by interactive manipulation in the 3D view.
void Tab::load_config(const DynamicPrintConfig& config)
{
    bool modified = 0;
    for(auto opt_key : m_config->diff(config)) {
        m_config->set_key_value(opt_key, config.option(opt_key)->clone());
        modified = 1;
    }
    if (modified) {
        update_dirty();
        //# Initialize UI components with the config values.
        reload_config();
        update();
    }
}

// Reload current $self->{config} (aka $self->{presets}->edited_preset->config) into the UI fields.
void Tab::reload_config()
{
    if (m_active_page)
        m_active_page->reload_config();
}

void Tab::update_mode()
{
    m_mode = wxGetApp().get_mode();

    update_visibility();
    update_sla_prusa_specific_visibility();

    update_changed_tree_ui();
}

void Tab::update_mode_markers()
{
    if (m_active_page)
        m_active_page->refresh();
}

void Tab::update_visibility()
{
    Freeze(); // There is needed Freeze/Thaw to avoid a flashing after Show/Layout

    for (auto page : m_pages)
        page->update_visibility(m_mode, page.get() == m_active_page);
    rebuild_page_tree();

    if (m_type != Preset::TYPE_PRINTER)
        update_description_lines();

    Layout();
    Thaw();
}

void Tab::msw_rescale()
{
    m_em_unit = em_unit(m_parent);

    m_presets_choice->msw_rescale();
    m_treectrl->SetMinSize(wxSize(20 * m_em_unit, -1));

    if (m_compatible_printers.checkbox)
        CheckBox::Rescale(m_compatible_printers.checkbox);
    if (m_compatible_prints.checkbox)
        CheckBox::Rescale(m_compatible_prints.checkbox);

    // rescale options_groups
    if (m_active_page)
        m_active_page->msw_rescale();

    Layout();
}

void Tab::sys_color_changed()
{
    m_presets_choice->sys_color_changed();

    // update buttons and cached bitmaps
    for (const auto btn : m_scaled_buttons)
        btn->sys_color_changed();
    for (const auto bmp : m_scaled_bitmaps)
        bmp->sys_color_changed();
    if (m_detach_preset_btn)
        m_detach_preset_btn->sys_color_changed();

    m_btn_hide_incompatible_presets->SetBitmap(*get_bmp_bundle(m_show_incompatible_presets ? "flag_red" : "flag_green"));

    // update icons for tree_ctrl
    wxVector <wxBitmapBundle> img_bundles;
    for (ScalableBitmap& bmp : m_scaled_icons_list) {
        bmp.sys_color_changed();
        img_bundles.push_back(bmp.bmp());
    }
    m_treectrl->SetImages(img_bundles);

    // Colors for ui "decoration"
    update_label_colours();
#ifdef _WIN32
    wxWindowUpdateLocker noUpdates(this);
    wxGetApp().UpdateDarkUI(this);
    wxGetApp().UpdateDarkUI(m_treectrl);
#endif
    update_changed_tree_ui();

    // update options_groups
    if (m_active_page)
        m_active_page->sys_color_changed();

    Layout();
    Refresh();
}

Field* Tab::get_field(const t_config_option_key& opt_key, int opt_index/* = -1*/) const
{
    return m_active_page ? m_active_page->get_field(opt_key, opt_index) : nullptr;
}

Line* Tab::get_line(const t_config_option_key& opt_key)
{
    return m_active_page ? m_active_page->get_line(opt_key) : nullptr;
}

std::pair<OG_CustomCtrl*, bool*> Tab::get_custom_ctrl_with_blinking_ptr(const t_config_option_key& opt_key, int opt_index/* = -1*/)
{
    if (!m_active_page)
        return {nullptr, nullptr};

    std::pair<OG_CustomCtrl*, bool*> ret = {nullptr, nullptr};

    for (auto opt_group : m_active_page->m_optgroups) {
        ret = opt_group->get_custom_ctrl_with_blinking_ptr(opt_key, opt_index);
        if (ret.first && ret.second)
            break;
    }
    return ret;
}

Field* Tab::get_field(const t_config_option_key& opt_key, Page** selected_page, int opt_index/* = -1*/)
{
    Field* field = nullptr;
    for (auto page : m_pages) {
        field = page->get_field(opt_key, opt_index);
        if (field != nullptr) {
            *selected_page = page.get();
            return field;
        }
    }
    return field;
}

void Tab::toggle_option(const std::string& opt_key, bool toggle, int opt_index/* = -1*/)
{
    if (!m_active_page)
        return;
    Field* field = m_active_page->get_field(opt_key, opt_index);
    if (field)
        field->toggle(toggle);
};

// To be called by custom widgets, load a value into a config,
// update the preset selection boxes (the dirty flags)
// If value is saved before calling this function, put saved_value = true,
// and value can be some random value because in this case it will not been used
void Tab::load_key_value(const std::string& opt_key, const boost::any& value, bool saved_value /*= false*/)
{
    if (!saved_value) OptionsGroup::change_opt_value(*m_config, opt_key, value);
    // Mark the print & filament enabled if they are compatible with the currently selected preset.
    if (opt_key == "compatible_printers" || opt_key == "compatible_prints") {
        // Don't select another profile if this profile happens to become incompatible.
        m_preset_bundle->update_compatible(PresetSelectCompatibleType::Never);
    }
    m_presets_choice->update_dirty();
    on_presets_changed();
    update();
}

static wxString support_combo_value_for_config(const DynamicPrintConfig &config, bool is_fff)
{
    std::string slatree = is_fff ? "" : get_sla_suptree_prefix(config);

    const std::string support         = is_fff ? "support_material"                 : "supports_enable";
    const std::string buildplate_only = is_fff ? "support_material_buildplate_only" : slatree + "support_buildplate_only";

    return
        ! config.opt_bool(support) ?
            _("None") :
               ((is_fff && !config.opt_bool("support_material_auto")) || (!is_fff && config.opt_bool("support_enforcers_only"))) ?
                _("For support enforcers only") :
                (config.opt_bool(buildplate_only) ? _("Support on build plate only") :
                                                    _("Everywhere"));
}

static wxString pad_combo_value_for_config(const DynamicPrintConfig &config)
{
    return config.opt_bool("pad_enable") ? (config.opt_bool("pad_around_object") ? _("Around object") : _("Below object")) : _("None");
}

void Tab::on_value_change(const std::string& opt_key, const boost::any& value)
{
    if (wxGetApp().plater() == nullptr) {
        return;
    }

    if (opt_key == "compatible_prints")
        this->compatible_widget_reload(m_compatible_prints);
    if (opt_key == "compatible_printers")
        this->compatible_widget_reload(m_compatible_printers);

    const bool is_fff = supports_printer_technology(ptFFF);
    ConfigOptionsGroup* og_freq_chng_params = wxGetApp().sidebar().og_freq_chng_params(is_fff);
    if (opt_key == "fill_density" || opt_key == "pad_enable")
    {
        boost::any val = og_freq_chng_params->get_config_value(*m_config, opt_key);
        og_freq_chng_params->set_value(opt_key, val);
    }
    
    if (opt_key == "pad_around_object") {
        for (PageShp &pg : m_pages) {
            Field * fld = pg->get_field(opt_key); /// !!! ysFIXME ????
            if (fld) fld->set_value(value, false);
        }
    }

    if (is_fff ?
            (opt_key == "support_material" || opt_key == "support_material_auto" || opt_key == "support_material_buildplate_only") :
            (opt_key == "supports_enable"  || opt_key == "support_tree_type" || opt_key == get_sla_suptree_prefix(*m_config) + "support_buildplate_only" || opt_key == "support_enforcers_only"))
        og_freq_chng_params->set_value("support", support_combo_value_for_config(*m_config, is_fff));

    if (! is_fff && (opt_key == "pad_enable" || opt_key == "pad_around_object"))
        og_freq_chng_params->set_value("pad", pad_combo_value_for_config(*m_config));

    if (opt_key == "brim_width")
    {
        bool val = m_config->opt_float("brim_width") > 0.0 ? true : false;
        og_freq_chng_params->set_value("brim", val);
    }

    if (opt_key == "wipe_tower" || opt_key == "single_extruder_multi_material" || opt_key == "extruders_count" )
        update_wiping_button_visibility();

    if (opt_key == "extruders_count")
        wxGetApp().sidebar().set_extruders_count(boost::any_cast<size_t>(value));

    if (m_postpone_update_ui) {
        // It means that not all values are rolled to the system/last saved values jet.
        // And call of the update() can causes a redundant check of the config values,
        // see https://github.com/prusa3d/PrusaSlicer/issues/7146
        return;
    }

    update();
}

// Show/hide the 'purging volumes' button
void Tab::update_wiping_button_visibility() {
    if (m_preset_bundle->printers.get_selected_preset().printer_technology() == ptSLA)
        return; // ys_FIXME
    bool wipe_tower_enabled = dynamic_cast<ConfigOptionBool*>(  (m_preset_bundle->prints.get_edited_preset().config  ).option("wipe_tower"))->value;
    bool multiple_extruders = dynamic_cast<ConfigOptionFloats*>((m_preset_bundle->printers.get_edited_preset().config).option("nozzle_diameter"))->values.size() > 1;
    bool single_extruder_multi_material = dynamic_cast<ConfigOptionBool*>((m_preset_bundle->printers.get_edited_preset().config).option("single_extruder_multi_material"))->value;

    auto wiping_dialog_button = wxGetApp().sidebar().get_wiping_dialog_button();
    if (wiping_dialog_button) {
        wiping_dialog_button->Show(wipe_tower_enabled && multiple_extruders && single_extruder_multi_material);
        wiping_dialog_button->GetParent()->Layout();
    }
}

void Tab::activate_option(const std::string& opt_key, const wxString& category)
{
    wxString page_title = translate_category(category, m_type);

    // We should to activate a tab with searched option, if it doesn't.
    // And do it before finding of the cur_item to avoid a case when Tab isn't activated jet and all treeItems are invisible
    wxGetApp().mainframe->select_tab(this);

    auto cur_item = m_treectrl->GetFirstVisibleItem();
    if (!cur_item)
        return;

    while (cur_item) {
        auto title = m_treectrl->GetItemText(cur_item);
        if (page_title != title) {
            cur_item = m_treectrl->GetNextVisible(cur_item);
            continue;
        }

        m_treectrl->SelectItem(cur_item);
        break;
    }

    auto set_focus = [](wxWindow* win) {
        win->SetFocus();
#ifdef WIN32
        if (wxTextCtrl* text = dynamic_cast<wxTextCtrl*>(win))
            text->SetSelection(-1, -1);
        else if (wxSpinCtrl* spin = dynamic_cast<wxSpinCtrl*>(win))
            spin->SetSelection(-1, -1);
#endif // WIN32
    };

    Field* field = get_field(opt_key);

    // focused selected field
    if (field)
        set_focus(field->getWindow());
    else if (category == "Single extruder MM setup") {
        // When we show and hide "Single extruder MM setup" page, 
        // related options are still in the search list
        // So, let's hightlighte a "single_extruder_multi_material" option, 
        // as a "way" to show hidden page again
        field = get_field("single_extruder_multi_material");
        if (field)
            set_focus(field->getWindow());
    }

    m_highlighter.init(get_custom_ctrl_with_blinking_ptr(opt_key));
}

void Tab::cache_config_diff(const std::vector<std::string>& selected_options, const DynamicPrintConfig* config/* = nullptr*/)
{
    m_cache_config.apply_only(config ? *config : m_presets->get_edited_preset().config, selected_options);
}

void Tab::apply_config_from_cache()
{
    bool was_applied = false;
    // check and apply extruders count for printer preset
    if (m_type == Preset::TYPE_PRINTER)
        was_applied = static_cast<TabPrinter*>(this)->apply_extruder_cnt_from_cache();

    if (!m_cache_config.empty()) {
        m_presets->get_edited_preset().config.apply(m_cache_config);
        m_cache_config.clear();

        was_applied = true;
    }

    if (was_applied)
        update_dirty();
}


// Call a callback to update the selection of presets on the plater:
// To update the content of the selection boxes,
// to update the filament colors of the selection boxes,
// to update the "dirty" flags of the selection boxes,
// to update number of "filament" selection boxes when the number of extruders change.
void Tab::on_presets_changed()
{
    if (wxGetApp().plater() == nullptr)
        return;

    // Instead of PostEvent (EVT_TAB_PRESETS_CHANGED) just call update_presets
    wxGetApp().plater()->sidebar().update_presets(m_type);

    // Printer selected at the Printer tab, update "compatible" marks at the print and filament selectors.
    for (auto t: m_dependent_tabs)
    {
        Tab* tab = wxGetApp().get_tab(t);
        // If the printer tells us that the print or filament/sla_material preset has been switched or invalidated,
        // refresh the print or filament/sla_material tab page.
        // But if there are options, moved from the previously selected preset, update them to edited preset
        tab->apply_config_from_cache();
        tab->load_current_preset();
    }
    // clear m_dependent_tabs after first update from select_preset()
    // to avoid needless preset loading from update() function
    m_dependent_tabs.clear();

    // Update Project dirty state, update application title bar.
    if (wxGetApp().mainframe)
        wxGetApp().plater()->update_project_dirty_from_presets();
}

void Tab::build_preset_description_line(ConfigOptionsGroup* optgroup)
{
    auto description_line = [this](wxWindow* parent) {
        return description_line_widget(parent, &m_parent_preset_description_line);
    };

    auto detach_preset_btn = [this](wxWindow* parent) {
        m_detach_preset_btn = new ScalableButton(parent, wxID_ANY, "lock_open_sys", _L("Detach from system preset"), 
                                                 wxDefaultSize, wxDefaultPosition, wxBU_LEFT | wxBU_EXACTFIT);
        ScalableButton* btn = m_detach_preset_btn;
        btn->SetFont(Slic3r::GUI::wxGetApp().normal_font());

        auto sizer = new wxBoxSizer(wxHORIZONTAL);
        sizer->Add(btn);

        btn->Bind(wxEVT_BUTTON, [this, parent](wxCommandEvent&)
        {
        	bool system = m_presets->get_edited_preset().is_system;
        	bool dirty  = m_presets->get_edited_preset().is_dirty;
            wxString msg_text = system ? 
            	_(L("A copy of the current system preset will be created, which will be detached from the system preset.")) :
                _(L("The current custom preset will be detached from the parent system preset."));
            if (dirty) {
	            msg_text += "\n\n";
            	msg_text += _(L("Modifications to the current profile will be saved."));
            }
            msg_text += "\n\n";
            msg_text += _(L("This action is not revertible.\nDo you want to proceed?"));

            //wxMessageDialog dialog(parent, msg_text, _(L("Detach preset")), wxICON_WARNING | wxYES_NO | wxCANCEL);
            MessageDialog dialog(parent, msg_text, _(L("Detach preset")), wxICON_WARNING | wxYES_NO | wxCANCEL);
            if (dialog.ShowModal() == wxID_YES)
                save_preset(m_presets->get_edited_preset().is_system ? std::string() : m_presets->get_edited_preset().name, true);
        });

        btn->Hide();

        return sizer;
    };

    Line line = Line{ "", "" };
    line.full_width = 1;

    line.append_widget(description_line);
    line.append_widget(detach_preset_btn);
    optgroup->append_line(line);
}

void Tab::update_preset_description_line()
{
    const Preset* parent = m_presets->get_selected_preset_parent();
    const Preset& preset = m_presets->get_edited_preset();

    wxString description_line;

    if (preset.is_default) {
        description_line = _(L("This is a default preset."));
    } else if (preset.is_system) {
        description_line = _(L("This is a system preset."));
    } else if (parent == nullptr) {
        description_line = _(L("Current preset is inherited from the default preset."));
    } else {
        std::string name = parent->name;
        boost::replace_all(name, "&", "&&");
        description_line = _(L("Current preset is inherited from")) + ":\n\t" + from_u8(name);
    }

    if (preset.is_default || preset.is_system)
        description_line += "\n\t" + _(L("It can't be deleted or modified.")) +
                            "\n\t" + _(L("Any modifications should be saved as a new preset inherited from this one.")) +
                            "\n\t" + _(L("To do that please specify a new name for the preset."));

    if (parent && parent->vendor)
    {
        description_line += "\n\n" + _(L("Additional information:")) + "\n";
        description_line += "\t" + _(L("vendor")) + ": " + (m_type == Slic3r::Preset::TYPE_PRINTER ? "\n\t\t" : "") + parent->vendor->name +
                            ", ver: " + parent->vendor->config_version.to_string();
        if (m_type == Slic3r::Preset::TYPE_PRINTER) {
            const std::string &printer_model = preset.config.opt_string("printer_model");
            if (! printer_model.empty())
                description_line += "\n\n\t" + _(L("printer model")) + ": \n\t\t" + printer_model;
            switch (preset.printer_technology()) {
            case ptFFF:
            {
                //FIXME add prefered_sla_material_profile for SLA
                const std::string              &default_print_profile = preset.config.opt_string("default_print_profile");
                const std::vector<std::string> &default_filament_profiles = preset.config.option<ConfigOptionStrings>("default_filament_profile")->values;
                if (!default_print_profile.empty())
                    description_line += "\n\n\t" + _(L("default print profile")) + ": \n\t\t" + default_print_profile;
                if (!default_filament_profiles.empty())
                {
                    description_line += "\n\n\t" + _(L("default filament profile")) + ": \n\t\t";
                    for (const std::string& profile : default_filament_profiles) {
                        if (&profile != &*default_filament_profiles.begin())
                            description_line += ", ";
                        description_line += from_u8(profile);
                    }
                }
                break;
            }
            case ptSLA:
            {
                //FIXME add prefered_sla_material_profile for SLA
                const std::string &default_sla_material_profile = preset.config.opt_string("default_sla_material_profile");
                if (!default_sla_material_profile.empty())
                    description_line += "\n\n\t" + _(L("default SLA material profile")) + ": \n\t\t" + default_sla_material_profile;

                const std::string &default_sla_print_profile = preset.config.opt_string("default_sla_print_profile");
                if (!default_sla_print_profile.empty())
                    description_line += "\n\n\t" + _(L("default SLA print profile")) + ": \n\t\t" + default_sla_print_profile;
                break;
            }
            default: break;
            }
        }
        else if (!preset.alias.empty())
        {
            description_line += "\n\n\t" + _(L("full profile name"))     + ": \n\t\t" + preset.name;
            description_line += "\n\t"   + _(L("symbolic profile name")) + ": \n\t\t" + preset.alias;
        }
    }

    m_parent_preset_description_line->SetText(description_line, false);

    if (m_detach_preset_btn)
        m_detach_preset_btn->Show(parent && parent->is_system && !preset.is_default);
    Layout();
}

void Tab::update_frequently_changed_parameters()
{
    const bool is_fff = supports_printer_technology(ptFFF);
    auto og_freq_chng_params = wxGetApp().sidebar().og_freq_chng_params(is_fff);
    if (!og_freq_chng_params) return;

    og_freq_chng_params->set_value("support", support_combo_value_for_config(*m_config, is_fff));
    if (! is_fff)
        og_freq_chng_params->set_value("pad", pad_combo_value_for_config(*m_config));

    const std::string updated_value_key = is_fff ? "fill_density" : "pad_enable";

    const boost::any val = og_freq_chng_params->get_config_value(*m_config, updated_value_key);
    og_freq_chng_params->set_value(updated_value_key, val);

    if (is_fff)
    {
        og_freq_chng_params->set_value("brim", bool(m_config->opt_float("brim_width") > 0.0));
        update_wiping_button_visibility();
    }
}

void TabPrint::build()
{
    m_presets = &m_preset_bundle->prints;
    load_initial_data();

    auto page = add_options_page(L("Layers and perimeters"), "layers");
        std::string category_path = "layers-and-perimeters_1748#";
        auto optgroup = page->new_optgroup(L("Layer height"));
        optgroup->append_single_option_line("layer_height", category_path + "layer-height");
        optgroup->append_single_option_line("first_layer_height", category_path + "first-layer-height");

        optgroup = page->new_optgroup(L("Vertical shells"));
        optgroup->append_single_option_line("perimeters", category_path + "perimeters");
        optgroup->append_single_option_line("spiral_vase", category_path + "spiral-vase");

        Line line { "", "" };
        line.full_width = 1;
        line.label_path = category_path + "recommended-thin-wall-thickness";
        line.widget = [this](wxWindow* parent) {
            return description_line_widget(parent, &m_recommended_thin_wall_thickness_description_line);
        };
        optgroup->append_line(line);

        optgroup = page->new_optgroup(L("Horizontal shells"));
        line = { L("Solid layers"), "" };
        line.label_path = category_path + "solid-layers-top-bottom";
        line.append_option(optgroup->get_option("top_solid_layers"));
        line.append_option(optgroup->get_option("bottom_solid_layers"));
        optgroup->append_line(line);
    	line = { L("Minimum shell thickness"), "" };
        line.append_option(optgroup->get_option("top_solid_min_thickness"));
        line.append_option(optgroup->get_option("bottom_solid_min_thickness"));
        optgroup->append_line(line);
		line = { "", "" };
	    line.full_width = 1;
	    line.widget = [this](wxWindow* parent) {
	        return description_line_widget(parent, &m_top_bottom_shell_thickness_explanation);
	    };
	    optgroup->append_line(line);

        optgroup = page->new_optgroup(L("Quality (slower slicing)"));
        optgroup->append_single_option_line("extra_perimeters", category_path + "extra-perimeters-if-needed");
        optgroup->append_single_option_line("extra_perimeters_on_overhangs", category_path + "extra-perimeters-on-overhangs");
        optgroup->append_single_option_line("ensure_vertical_shell_thickness", category_path + "ensure-vertical-shell-thickness");
        optgroup->append_single_option_line("avoid_crossing_curled_overhangs", category_path + "avoid-crossing-curled-overhangs");
        optgroup->append_single_option_line("avoid_crossing_perimeters", category_path + "avoid-crossing-perimeters");
        optgroup->append_single_option_line("avoid_crossing_perimeters_max_detour", category_path + "avoid_crossing_perimeters_max_detour");
        optgroup->append_single_option_line("thin_walls", category_path + "detect-thin-walls");
        optgroup->append_single_option_line("thick_bridges", category_path + "thick_bridges");
        optgroup->append_single_option_line("overhangs", category_path + "detect-bridging-perimeters");

        optgroup = page->new_optgroup(L("Advanced"));
        optgroup->append_single_option_line("seam_position", category_path + "seam-position");
        optgroup->append_single_option_line("seam_gap_distance", category_path + "seam-gap-distance");
        optgroup->append_single_option_line("staggered_inner_seams", category_path + "staggered-inner-seams");

        optgroup->append_single_option_line("scarf_seam_placement", category_path + "scarf-seam-placement");
        optgroup->append_single_option_line("scarf_seam_only_on_smooth", category_path + "scarf-seam-only-on-smooth");
        optgroup->append_single_option_line("scarf_seam_start_height", category_path + "scarf-seam-start-height");
        optgroup->append_single_option_line("scarf_seam_entire_loop", category_path + "scarf-seam-entire-loop");
        optgroup->append_single_option_line("scarf_seam_length", category_path + "scarf-seam-length");
        optgroup->append_single_option_line("scarf_seam_max_segment_length", category_path + "scarf-seam-max-segment-length");
        optgroup->append_single_option_line("scarf_seam_on_inner_perimeters", category_path + "scarf-seam-on-inner-perimeters");

        optgroup->append_single_option_line("external_perimeters_first", category_path + "external-perimeters-first");
        optgroup->append_single_option_line("gap_fill_enabled", category_path + "fill-gaps");
        optgroup->append_single_option_line("perimeter_generator");

        optgroup = page->new_optgroup(L("Fuzzy skin (experimental)"));
        category_path = "fuzzy-skin_246186/#";
        optgroup->append_single_option_line("fuzzy_skin", category_path + "fuzzy-skin-type");
        optgroup->append_single_option_line("fuzzy_skin_thickness", category_path + "fuzzy-skin-thickness");
        optgroup->append_single_option_line("fuzzy_skin_point_dist", category_path + "fuzzy-skin-point-distance");

        optgroup = page->new_optgroup(L("Only one perimeter"));
        optgroup->append_single_option_line("top_one_perimeter_type", category_path + "top-one-perimeter-type");
        optgroup->append_single_option_line("only_one_perimeter_first_layer", category_path + "only-one-perimeter-first-layer");

    page = add_options_page(L("Infill"), "infill");
        category_path = "infill_42#";
        optgroup = page->new_optgroup(L("Infill"));
        optgroup->append_single_option_line("fill_density", category_path + "fill-density");
        optgroup->append_single_option_line("fill_pattern", category_path + "fill-pattern");
        optgroup->append_single_option_line("infill_anchor", category_path + "fill-pattern");
        optgroup->append_single_option_line("infill_anchor_max", category_path + "fill-pattern");
        optgroup->append_single_option_line("top_fill_pattern", category_path + "top-fill-pattern");
        optgroup->append_single_option_line("bottom_fill_pattern", category_path + "bottom-fill-pattern");

        optgroup = page->new_optgroup(L("Ironing"));
        category_path = "ironing_177488#";
        optgroup->append_single_option_line("ironing", category_path);
        optgroup->append_single_option_line("ironing_type", category_path + "ironing-type");
        optgroup->append_single_option_line("ironing_flowrate", category_path + "flow-rate");
        optgroup->append_single_option_line("ironing_spacing", category_path + "spacing-between-ironing-passes");

        optgroup = page->new_optgroup(L("Reducing printing time"));
        category_path = "infill_42#";
        optgroup->append_single_option_line("automatic_infill_combination");
        optgroup->append_single_option_line("automatic_infill_combination_max_layer_height");
        optgroup->append_single_option_line("infill_every_layers", category_path + "combine-infill-every-x-layers");

        optgroup = page->new_optgroup(L("Advanced"));
        optgroup->append_single_option_line("solid_infill_every_layers", category_path + "solid-infill-every-x-layers");
        optgroup->append_single_option_line("fill_angle", category_path + "fill-angle");
        optgroup->append_single_option_line("solid_infill_below_area", category_path + "solid-infill-threshold-area");
        optgroup->append_single_option_line("bridge_angle");
        optgroup->append_single_option_line("only_retract_when_crossing_perimeters");
        optgroup->append_single_option_line("infill_first");

    page = add_options_page(L("Skirt and brim"), "skirt+brim");
        category_path = "skirt-and-brim_133969#";
        optgroup = page->new_optgroup(L("Skirt"));
        optgroup->append_single_option_line("skirts", category_path + "skirt");
        optgroup->append_single_option_line("skirt_distance", category_path + "skirt");
        optgroup->append_single_option_line("skirt_height", category_path + "skirt");
        optgroup->append_single_option_line("draft_shield", category_path + "skirt");
        optgroup->append_single_option_line("min_skirt_length", category_path + "skirt");

        optgroup = page->new_optgroup(L("Brim"));
        optgroup->append_single_option_line("brim_type", category_path + "brim");
        optgroup->append_single_option_line("brim_width", category_path + "brim");
        optgroup->append_single_option_line("brim_separation", category_path + "brim");

    page = add_options_page(L("Support material"), "support");
        category_path = "support-material_1698#";
        optgroup = page->new_optgroup(L("Support material"));
        optgroup->append_single_option_line("support_material", category_path + "generate-support-material");
        optgroup->append_single_option_line("support_material_auto", category_path + "auto-generated-supports");
        optgroup->append_single_option_line("support_material_threshold", category_path + "overhang-threshold");
        optgroup->append_single_option_line("support_material_enforce_layers", category_path + "enforce-support-for-the-first");
        optgroup->append_single_option_line("raft_first_layer_density", category_path + "raft-first-layer-density");
        optgroup->append_single_option_line("raft_first_layer_expansion", category_path + "raft-first-layer-expansion");

        optgroup = page->new_optgroup(L("Raft"));
        optgroup->append_single_option_line("raft_layers", category_path + "raft-layers");
        optgroup->append_single_option_line("raft_contact_distance", category_path + "raft-layers");
        optgroup->append_single_option_line("raft_expansion");

        optgroup = page->new_optgroup(L("Options for support material and raft"));
        optgroup->append_single_option_line("support_material_style", category_path + "style");
        optgroup->append_single_option_line("support_material_contact_distance", category_path + "contact-z-distance");
        optgroup->append_single_option_line("support_material_bottom_contact_distance", category_path + "contact-z-distance");
        optgroup->append_single_option_line("support_material_pattern", category_path + "pattern");
        optgroup->append_single_option_line("support_material_with_sheath", category_path + "with-sheath-around-the-support");
        optgroup->append_single_option_line("support_material_spacing", category_path + "pattern-spacing-0-inf");
        optgroup->append_single_option_line("support_material_angle", category_path + "pattern-angle");
        optgroup->append_single_option_line("support_material_closing_radius", category_path + "pattern-angle");
        optgroup->append_single_option_line("support_material_interface_layers", category_path + "interface-layers");
        optgroup->append_single_option_line("support_material_bottom_interface_layers", category_path + "interface-layers");
        optgroup->append_single_option_line("support_material_interface_pattern", category_path + "interface-pattern");
        optgroup->append_single_option_line("support_material_interface_spacing", category_path + "interface-pattern-spacing");
        optgroup->append_single_option_line("support_material_interface_contact_loops", category_path + "interface-loops");
        optgroup->append_single_option_line("support_material_buildplate_only", category_path + "support-on-build-plate-only");
        optgroup->append_single_option_line("support_material_xy_spacing", category_path + "xy-separation-between-an-object-and-its-support");
        optgroup->append_single_option_line("dont_support_bridges", category_path + "dont-support-bridges");
        optgroup->append_single_option_line("support_material_synchronize_layers", category_path + "synchronize-with-object-layers");

        optgroup = page->new_optgroup(L("Organic supports"));
        const std::string path = "organic-supports_480131#organic-supports-settings";
        optgroup->append_single_option_line("support_tree_angle", path);
        optgroup->append_single_option_line("support_tree_angle_slow", path);
        optgroup->append_single_option_line("support_tree_branch_diameter", path);
        optgroup->append_single_option_line("support_tree_branch_diameter_angle", path);
        optgroup->append_single_option_line("support_tree_branch_diameter_double_wall", path);
        optgroup->append_single_option_line("support_tree_tip_diameter", path);
        optgroup->append_single_option_line("support_tree_branch_distance", path);
        optgroup->append_single_option_line("support_tree_top_rate", path);

    page = add_options_page(L("Speed"), "time");
        optgroup = page->new_optgroup(L("Speed for print moves"));
        optgroup->append_single_option_line("perimeter_speed");
        optgroup->append_single_option_line("small_perimeter_speed");
        optgroup->append_single_option_line("external_perimeter_speed");
        optgroup->append_single_option_line("infill_speed");
        optgroup->append_single_option_line("solid_infill_speed");
        optgroup->append_single_option_line("top_solid_infill_speed");
        optgroup->append_single_option_line("support_material_speed");
        optgroup->append_single_option_line("support_material_interface_speed");
        optgroup->append_single_option_line("bridge_speed");
        optgroup->append_single_option_line("gap_fill_speed");
        optgroup->append_single_option_line("ironing_speed");

        optgroup = page->new_optgroup(L("Dynamic overhang speed"));
        optgroup->append_single_option_line("enable_dynamic_overhang_speeds");
        optgroup->append_single_option_line("overhang_speed_0");
        optgroup->append_single_option_line("overhang_speed_1");
        optgroup->append_single_option_line("overhang_speed_2");
        optgroup->append_single_option_line("overhang_speed_3");

        optgroup = page->new_optgroup(L("Speed for non-print moves"));
        optgroup->append_single_option_line("travel_speed");
        optgroup->append_single_option_line("travel_speed_z");

        optgroup = page->new_optgroup(L("Modifiers"));
        optgroup->append_single_option_line("first_layer_speed");
        optgroup->append_single_option_line("first_layer_speed_over_raft");

        optgroup = page->new_optgroup(L("Acceleration control (advanced)"));
        optgroup->append_single_option_line("external_perimeter_acceleration");
        optgroup->append_single_option_line("perimeter_acceleration");
        optgroup->append_single_option_line("top_solid_infill_acceleration");
        optgroup->append_single_option_line("solid_infill_acceleration");
        optgroup->append_single_option_line("infill_acceleration");
        optgroup->append_single_option_line("bridge_acceleration");
        optgroup->append_single_option_line("first_layer_acceleration");
        optgroup->append_single_option_line("first_layer_acceleration_over_raft");
        optgroup->append_single_option_line("wipe_tower_acceleration");
        optgroup->append_single_option_line("travel_acceleration");
        optgroup->append_single_option_line("default_acceleration");

        optgroup = page->new_optgroup(L("Autospeed (advanced)"));
        optgroup->append_single_option_line("max_print_speed", "max-volumetric-speed_127176");
        optgroup->append_single_option_line("max_volumetric_speed", "max-volumetric-speed_127176");

        optgroup = page->new_optgroup(L("Pressure equalizer (experimental)"));
        optgroup->append_single_option_line("max_volumetric_extrusion_rate_slope_positive", "pressure-equlizer_331504");
        optgroup->append_single_option_line("max_volumetric_extrusion_rate_slope_negative", "pressure-equlizer_331504");

    page = add_options_page(L("Multiple Extruders"), "funnel");
        optgroup = page->new_optgroup(L("Extruders"));
        optgroup->append_single_option_line("perimeter_extruder");
        optgroup->append_single_option_line("infill_extruder");
        optgroup->append_single_option_line("solid_infill_extruder");
        optgroup->append_single_option_line("support_material_extruder");
        optgroup->append_single_option_line("support_material_interface_extruder");
        optgroup->append_single_option_line("wipe_tower_extruder");

        optgroup = page->new_optgroup(L("Ooze prevention"));
        optgroup->append_single_option_line("ooze_prevention");
        optgroup->append_single_option_line("standby_temperature_delta");

        optgroup = page->new_optgroup(L("Wipe tower"));
        optgroup->append_single_option_line("wipe_tower");
        optgroup->append_single_option_line("wipe_tower_width");        
        optgroup->append_single_option_line("wipe_tower_brim_width");
        optgroup->append_single_option_line("wipe_tower_bridging");
        optgroup->append_single_option_line("wipe_tower_cone_angle");
        optgroup->append_single_option_line("wipe_tower_extra_spacing");
        optgroup->append_single_option_line("wipe_tower_extra_flow");
        optgroup->append_single_option_line("wipe_tower_no_sparse_layers");
        optgroup->append_single_option_line("single_extruder_multi_material_priming");

        optgroup = page->new_optgroup(L("Advanced"));
        optgroup->append_single_option_line("interface_shells");
        optgroup->append_single_option_line("mmu_segmented_region_max_width");
        optgroup->append_single_option_line("mmu_segmented_region_interlocking_depth");

    page = add_options_page(L("Advanced"), "wrench");
        optgroup = page->new_optgroup(L("Extrusion width"));
        optgroup->append_single_option_line("extrusion_width");
        optgroup->append_single_option_line("first_layer_extrusion_width");
        optgroup->append_single_option_line("perimeter_extrusion_width");
        optgroup->append_single_option_line("external_perimeter_extrusion_width");
        optgroup->append_single_option_line("infill_extrusion_width");
        optgroup->append_single_option_line("solid_infill_extrusion_width");
        optgroup->append_single_option_line("top_infill_extrusion_width");
        optgroup->append_single_option_line("support_material_extrusion_width");
        optgroup->append_single_option_line("automatic_extrusion_widths");

        optgroup = page->new_optgroup(L("Overlap"));
        optgroup->append_single_option_line("infill_overlap");

        optgroup = page->new_optgroup(L("Flow"));
        optgroup->append_single_option_line("bridge_flow_ratio");

        optgroup = page->new_optgroup(L("Slicing"));
        optgroup->append_single_option_line("slice_closing_radius");
        optgroup->append_single_option_line("slicing_mode");
        optgroup->append_single_option_line("resolution");
        optgroup->append_single_option_line("gcode_resolution");
        optgroup->append_single_option_line("arc_fitting");
        optgroup->append_single_option_line("xy_size_compensation");
        optgroup->append_single_option_line("elefant_foot_compensation", "elephant-foot-compensation_114487");

        optgroup = page->new_optgroup(L("Arachne perimeter generator"));
        optgroup->append_single_option_line("wall_transition_angle");
        optgroup->append_single_option_line("wall_transition_filter_deviation");
        optgroup->append_single_option_line("wall_transition_length");
        optgroup->append_single_option_line("wall_distribution_count");
        optgroup->append_single_option_line("min_bead_width");
        optgroup->append_single_option_line("min_feature_size");

    page = add_options_page(L("Output options"), "output+page_white");
        optgroup = page->new_optgroup(L("Sequential printing"));
        optgroup->append_single_option_line("complete_objects", "sequential-printing_124589");
        line = { L("Extruder clearance"), "" };
        line.append_option(optgroup->get_option("extruder_clearance_radius"));
        line.append_option(optgroup->get_option("extruder_clearance_height"));
        optgroup->append_line(line);

        optgroup = page->new_optgroup(L("Output file"));
        optgroup->append_single_option_line("gcode_comments");
        optgroup->append_single_option_line("gcode_label_objects");
        Option option = optgroup->get_option("output_filename_format");
        option.opt.full_width = true;
        optgroup->append_single_option_line(option);

        optgroup = page->new_optgroup(L("Other"));

        create_line_with_widget(optgroup.get(), "gcode_substitutions", "g-code-substitutions_301694", [this](wxWindow* parent) {
            return create_manage_substitution_widget(parent);
        });
        line = { "", "" };
        line.full_width = 1;
        line.widget = [this](wxWindow* parent) {
            return create_substitutions_widget(parent);
        };
        optgroup->append_line(line);

        optgroup = page->new_optgroup(L("Post-processing scripts"), 0);
        line = { "", "" };
        line.full_width = 1;
        line.widget = [this](wxWindow* parent) {
            return description_line_widget(parent, &m_post_process_explanation);
        };
        optgroup->append_line(line);
        option = optgroup->get_option("post_process");
        option.opt.full_width = true;
        option.opt.height = 5;//50;
        optgroup->append_single_option_line(option);

    page = add_options_page(L("Notes"), "note");
        optgroup = page->new_optgroup(L("Notes"), 0);
        option = optgroup->get_option("notes");
        option.opt.full_width = true;
        option.opt.height = 25;//250;
        optgroup->append_single_option_line(option);

    page = add_options_page(L("Dependencies"), "wrench");
        optgroup = page->new_optgroup(L("Profile dependencies"));

        create_line_with_widget(optgroup.get(), "compatible_printers", "", [this](wxWindow* parent) {
            return compatible_widget_create(parent, m_compatible_printers);
        });
        
        option = optgroup->get_option("compatible_printers_condition");
        option.opt.full_width = true;
        optgroup->append_single_option_line(option);

        build_preset_description_line(optgroup.get());
}

void TabPrint::update_description_lines()
{
    Tab::update_description_lines();

    if (m_preset_bundle->printers.get_selected_preset().printer_technology() == ptSLA)
        return;

    if (m_active_page && m_active_page->title() == "Layers and perimeters" && 
        m_recommended_thin_wall_thickness_description_line && m_top_bottom_shell_thickness_explanation)
    {
        m_recommended_thin_wall_thickness_description_line->SetText(
            from_u8(PresetHints::recommended_thin_wall_thickness(*m_preset_bundle)));
        m_top_bottom_shell_thickness_explanation->SetText(
            from_u8(PresetHints::top_bottom_shell_thickness_explanation(*m_preset_bundle)));
    }

    if (m_active_page && m_active_page->title() == "Output options") {
        if (m_post_process_explanation) {
            m_post_process_explanation->SetText(
                _L("Post processing scripts shall modify G-code file in place."));
            m_post_process_explanation->SetPathEnd("post-processing-scripts_283913");
        }
        // update G-code substitutions from the current configuration
        {
            m_subst_manager.update_from_config();
            if (m_del_all_substitutions_btn)
                m_del_all_substitutions_btn->Show(!m_subst_manager.is_empty_substitutions());
        }
    }
}

void TabPrint::toggle_options()
{
    if (!m_active_page) return;

    m_config_manipulation.toggle_print_fff_options(m_config);
}

void TabPrint::update()
{
    if (m_preset_bundle->printers.get_selected_preset().printer_technology() == ptSLA)
        return; // ys_FIXME

    m_update_cnt++;

    // see https://github.com/prusa3d/PrusaSlicer/issues/6814
    // ysFIXME: It's temporary workaround and should be clewer reworked:
    // Note: This workaround works till "support_material" and "overhangs" is exclusive sets of mutually no-exclusive parameters.
    // But it should be corrected when we will have more such sets.
    // Disable check of the compatibility of the "support_material" and "overhangs" options for saved user profile
    // NOTE: Initialization of the support_material_overhangs_queried value have to be processed just ones
    if (!m_config_manipulation.is_initialized_support_material_overhangs_queried())
    {
        const Preset& selected_preset = m_preset_bundle->prints.get_selected_preset();
        bool is_user_and_saved_preset = !selected_preset.is_system && !selected_preset.is_dirty;
        bool support_material_overhangs_queried = m_config->opt_bool("support_material") && !m_config->opt_bool("overhangs");
        m_config_manipulation.initialize_support_material_overhangs_queried(is_user_and_saved_preset && support_material_overhangs_queried);
    }

    m_config_manipulation.update_print_fff_config(m_config, true);

    update_description_lines();
    Layout();

    m_update_cnt--;

    if (m_update_cnt==0) {
        toggle_options();

        // update() could be called during undo/redo execution
        // Update of objectList can cause a crash in this case (because m_objects doesn't match ObjectList) 
        if (!wxGetApp().plater()->inside_snapshot_capture())
            wxGetApp().obj_list()->update_and_show_object_settings_item();

        wxGetApp().mainframe->on_config_changed(m_config);
    }
}

void TabPrint::clear_pages()
{
    Tab::clear_pages();

    m_recommended_thin_wall_thickness_description_line = nullptr;
    m_top_bottom_shell_thickness_explanation = nullptr;
    m_post_process_explanation = nullptr;

    m_del_all_substitutions_btn = nullptr;
}

bool Tab::validate_custom_gcode(const wxString& title, const std::string& gcode)
{
    std::vector<std::string> tags;
    bool invalid = GCodeProcessor::contains_reserved_tags(gcode, 5, tags);
    if (invalid) {
        std::string lines = ":\n";
        for (const std::string& keyword : tags)
            lines += ";" + keyword + "\n";
        wxString reports = format_wxstr(
            _L_PLURAL("The following line %s contains reserved keywords.\nPlease remove it, as it may cause problems in G-code visualization and printing time estimation.", 
                      "The following lines %s contain reserved keywords.\nPlease remove them, as they may cause problems in G-code visualization and printing time estimation.", 
                      tags.size()),
            lines);
        //wxMessageDialog dialog(wxGetApp().mainframe, reports, _L("Found reserved keywords in") + " " + _(title), wxICON_WARNING | wxOK);
        MessageDialog dialog(wxGetApp().mainframe, reports, _L("Found reserved keywords in") + " " + _(title), wxICON_WARNING | wxOK);
        dialog.ShowModal();
    }
    return !invalid;
}

static void validate_custom_gcode_cb(Tab* tab, const wxString& title, const t_config_option_key& opt_key, const boost::any& value) {
    tab->validate_custom_gcodes_was_shown = !Tab::validate_custom_gcode(title, boost::any_cast<std::string>(value));
    tab->update_dirty();
    tab->on_value_change(opt_key, value);
}

void Tab::edit_custom_gcode(const t_config_option_key& opt_key)
{
    EditGCodeDialog dlg = EditGCodeDialog(this, opt_key, get_custom_gcode(opt_key));
    if (dlg.ShowModal() == wxID_OK) {
        set_custom_gcode(opt_key, dlg.get_edited_gcode());
        update_dirty();
        update();
    }
}

const std::string& Tab::get_custom_gcode(const t_config_option_key& opt_key)
{
    return m_config->opt_string(opt_key);
}

void Tab::set_custom_gcode(const t_config_option_key& opt_key, const std::string& value)
{
    DynamicPrintConfig new_conf = *m_config;
    new_conf.set_key_value(opt_key, new ConfigOptionString(value));
    load_config(new_conf);
}

const std::string& TabFilament::get_custom_gcode(const t_config_option_key& opt_key)
{
    return m_config->opt_string(opt_key, unsigned(0));
}

void TabFilament::set_custom_gcode(const t_config_option_key& opt_key, const std::string& value)
{
    std::vector<std::string> gcodes = static_cast<const ConfigOptionStrings*>(m_config->option(opt_key))->values;
    gcodes[0] = value;

    DynamicPrintConfig new_conf = *m_config;
    new_conf.set_key_value(opt_key, new ConfigOptionStrings(gcodes));
    load_config(new_conf);
}

void TabFilament::create_line_with_near_label_widget(ConfigOptionsGroupShp optgroup, const std::string& opt_key, int opt_index/* = 0*/)
{
    Line line {"",""};
    if (opt_key == "filament_retract_lift_above" || opt_key == "filament_retract_lift_below") {
        Option opt = optgroup->get_option(opt_key);
        opt.opt.label = opt.opt.full_label;
        line = optgroup->create_single_option_line(opt);
    }
    else
        line = optgroup->create_single_option_line(optgroup->get_option(opt_key));

    line.near_label_widget = [this, optgroup_wk = ConfigOptionsGroupWkp(optgroup), opt_key, opt_index](wxWindow* parent) {
        wxWindow* check_box = CheckBox::GetNewWin(parent);
        wxGetApp().UpdateDarkUI(check_box);

        check_box->Bind(wxEVT_CHECKBOX, [optgroup_wk, opt_key, opt_index](wxCommandEvent& evt) {
            const bool is_checked = evt.IsChecked();
            if (auto optgroup_sh = optgroup_wk.lock(); optgroup_sh) {
                if (Field *field = optgroup_sh->get_fieldc(opt_key, opt_index); field != nullptr) {
                    field->toggle(is_checked);
                    if (is_checked)
                        field->set_last_meaningful_value();
                    else
                        field->set_na_value();
                }
            }
        });

        m_overrides_options[opt_key] = check_box;
        return check_box;
    };

    optgroup->append_line(line);
}

void TabFilament::update_line_with_near_label_widget(ConfigOptionsGroupShp optgroup, const std::string& opt_key, int opt_index/* = 0*/, bool is_checked/* = true*/)
{
    if (!m_overrides_options[opt_key])
        return;
    m_overrides_options[opt_key]->Enable(is_checked);

    is_checked &= !m_config->option(opt_key)->is_nil();
    CheckBox::SetValue(m_overrides_options[opt_key], is_checked);

    Field* field = optgroup->get_fieldc(opt_key, opt_index);
    if (field != nullptr)
        field->toggle(is_checked);
}

std::vector<std::pair<std::string, std::vector<std::string>>> filament_overrides_option_keys {
    {"Travel lift", {
        "filament_retract_lift",
        "filament_travel_ramping_lift",
        "filament_travel_max_lift",
        "filament_travel_slope",
        "filament_travel_lift_before_obstacle",
        "filament_retract_lift_above",
        "filament_retract_lift_below"
    }},
    {"Retraction", {
        "filament_retract_length",
        "filament_retract_speed",
        "filament_deretract_speed",
        "filament_retract_restart_extra",
        "filament_retract_before_travel",
        "filament_retract_layer_change",
        "filament_wipe",
        "filament_retract_before_wipe",
    }},
    {"Retraction when tool is disabled", {
        "filament_retract_length_toolchange",
        "filament_retract_restart_extra_toolchange"
    }},
    {"Seams", {
        "filament_seam_gap_distance"
    }}
};

void TabFilament::add_filament_overrides_page()
{
    PageShp page = add_options_page(L("Filament Overrides"), "wrench");

    const int extruder_idx = 0; // #ys_FIXME

    for (const auto&[title, keys] : filament_overrides_option_keys) {
        ConfigOptionsGroupShp optgroup = page->new_optgroup(L(title));
        for (const std::string& opt_key : keys) {
            create_line_with_near_label_widget(optgroup, opt_key, extruder_idx);
        }
    }
}

std::optional<ConfigOptionsGroupShp> get_option_group(const Page* page, const std::string& title) {
    auto og_it = std::find_if(
        page->m_optgroups.begin(), page->m_optgroups.end(),
        [&](const ConfigOptionsGroupShp& og) {
            return og->title == title;
        }
    );
    if (og_it == page->m_optgroups.end())
        return {};
    return *og_it;
}

void TabFilament::update_filament_overrides_page()
{
    if (!m_active_page || m_active_page->title() != "Filament Overrides")
        return;
    Page* page = m_active_page;


    const int extruder_idx = 0; // #ys_FIXME

    const bool have_retract_length = (
        m_config->option("filament_retract_length")->is_nil()
        || m_config->opt_float("filament_retract_length", extruder_idx) > 0
    );

    const bool uses_ramping_lift = (
        m_config->option("filament_travel_ramping_lift")->is_nil()
        || m_config->opt_bool("filament_travel_ramping_lift", extruder_idx)
    );

    const bool is_lifting =  (
        m_config->option("filament_travel_max_lift")->is_nil()
        || m_config->opt_float("filament_travel_max_lift", extruder_idx) > 0
        || m_config->option("filament_retract_lift")->is_nil()
        || m_config->opt_float("filament_retract_lift", extruder_idx) > 0
    );

    for (const auto&[title, keys] : filament_overrides_option_keys) {
        std::optional<ConfigOptionsGroupShp> optgroup{get_option_group(page, title)};
        if (!optgroup) {
            continue;
        }

        for (const std::string& opt_key : keys) {
            bool is_checked{true};
            if (
                title == "Retraction"
                && opt_key != "filament_retract_length"
                && !have_retract_length
            ) {
                is_checked = false;
            }

            if (
                title == "Travel lift"
                && uses_ramping_lift
                && opt_key == "filament_retract_lift"
                && !m_config->option("filament_travel_ramping_lift")->is_nil()
                && m_config->opt_bool("filament_travel_ramping_lift", extruder_idx)
            ) {
                is_checked = false;
            }

            if (
                title == "Travel lift"
                && !is_lifting
                && (
                    opt_key == "filament_retract_lift_above"
                    || opt_key == "filament_retract_lift_below"
                )
            ) {
                is_checked = false;
            }

            if (
                title == "Travel lift"
                && !uses_ramping_lift
                && opt_key != "filament_travel_ramping_lift"
                && opt_key != "filament_retract_lift"
                && opt_key != "filament_retract_lift_above"
                && opt_key != "filament_retract_lift_below"
            ) {
                is_checked = false;
            }

            update_line_with_near_label_widget(*optgroup, opt_key, extruder_idx, is_checked);
        }
    }
}

void TabFilament::create_extruder_combobox()
{
    m_extruders_cb = new BitmapComboBox(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(12 * m_em_unit, -1), 0, nullptr, wxCB_READONLY);
    m_extruders_cb->Hide();

    m_extruders_cb->Bind(wxEVT_COMBOBOX, [this](wxCommandEvent&) {
        set_active_extruder(m_extruders_cb->GetSelection());
    });

    m_h_buttons_sizer->AddSpacer(3*em_unit(this));
    m_h_buttons_sizer->Add(m_extruders_cb, 0, wxALIGN_CENTER_VERTICAL);
}

void TabFilament::update_extruder_combobox_visibility()
{
    const size_t extruder_cnt = static_cast<const ConfigOptionFloats*>(m_preset_bundle->printers.get_edited_preset().config.option("nozzle_diameter"))->values.size();
    m_extruders_cb->Show(extruder_cnt > 1);
}

void TabFilament::update_extruder_combobox()
{
    const size_t extruder_cnt = m_preset_bundle->printers.get_selected_preset().printer_technology() == ptSLA ? m_extruders_cb->GetCount() :
                                static_cast<const ConfigOptionFloats*>(m_preset_bundle->printers.get_edited_preset().config.option("nozzle_diameter"))->values.size();

    if (extruder_cnt != m_extruders_cb->GetCount()) {
        m_extruders_cb->Clear();
        for (size_t id = 1; id <= extruder_cnt; id++)
            m_extruders_cb->Append(format_wxstr("%1% %2%", _L("Extruder"), id), *get_bmp_bundle("funnel"));
    }

    if (m_active_extruder >= int(extruder_cnt)) {
        m_active_extruder = 0;
        // update selected and, as a result, editing preset
        const std::string& preset_name = m_preset_bundle->extruders_filaments[0].get_selected_preset_name();
        m_presets->select_preset_by_name(preset_name, true);

        // To avoid inconsistance between value of active_extruder in FilamentTab and TabPresetComboBox,
        // which can causes a crash on switch preset from MM printer to SM printer
        m_presets_choice->set_extruder_idx(m_active_extruder);
    }

    m_extruders_cb->SetSelection(m_active_extruder);
    m_extruders_cb->Show(extruder_cnt > 1);
}

bool TabFilament::set_active_extruder(int new_selected_extruder)
{
    if (m_active_extruder == new_selected_extruder)
        return true;

    const int old_extruder_id = m_active_extruder;
    m_active_extruder = new_selected_extruder;
    m_presets_choice->set_extruder_idx(m_active_extruder);

    if (!select_preset(m_preset_bundle->extruders_filaments[m_active_extruder].get_selected_preset_name())) {
        m_active_extruder = old_extruder_id;
        m_presets_choice->set_extruder_idx(m_active_extruder);
        m_extruders_cb->SetSelection(m_active_extruder);
        return false;
    }

    if (m_active_extruder != m_extruders_cb->GetSelection())
        m_extruders_cb->Select(m_active_extruder);

    return true;
}

void TabFilament::build()
{
    // add extruder combobox
    create_extruder_combobox();

    m_presets = &m_preset_bundle->filaments;
    load_initial_data();

    auto page = add_options_page(L("Filament"), "spool");
        auto optgroup = page->new_optgroup(L("Filament"));
        optgroup->append_single_option_line("filament_colour");
        optgroup->append_single_option_line("filament_diameter");
        optgroup->append_single_option_line("extrusion_multiplier");
        optgroup->append_single_option_line("filament_density");
        optgroup->append_single_option_line("filament_cost");
        optgroup->append_single_option_line("filament_spool_weight");

        optgroup->on_change = [this](t_config_option_key opt_key, boost::any value)
        {
            update_dirty();
            if (opt_key == "filament_spool_weight") {
                // Change of this option influences for an update of "Sliced Info"
                wxGetApp().sidebar().update_sliced_info_sizer();
                wxGetApp().sidebar().Layout();
            }
            else
                on_value_change(opt_key, value);
        };

        optgroup = page->new_optgroup(L("Temperature"));

        create_line_with_near_label_widget(optgroup, "idle_temperature");

        Line line = { L("Nozzle"), "" };
        line.append_option(optgroup->get_option("first_layer_temperature"));
        line.append_option(optgroup->get_option("temperature"));
        optgroup->append_line(line);

        line = { L("Bed"), "" };
        line.append_option(optgroup->get_option("first_layer_bed_temperature"));
        line.append_option(optgroup->get_option("bed_temperature"));
        optgroup->append_line(line);

        line = { L("Chamber"), "" };
        line.append_option(optgroup->get_option("chamber_temperature"));
        line.append_option(optgroup->get_option("chamber_minimal_temperature"));
        optgroup->append_line(line);

    page = add_options_page(L("Cooling"), "cooling");
        std::string category_path = "cooling_127569#";
        optgroup = page->new_optgroup(L("Enable"));
        optgroup->append_single_option_line("fan_always_on");
        optgroup->append_single_option_line("cooling");

        line = { "", "" };
        line.full_width = 1;
        line.widget = [this](wxWindow* parent) {
            return description_line_widget(parent, &m_cooling_description_line);
        };
        optgroup->append_line(line);

        optgroup = page->new_optgroup(L("Fan settings"));
        line = { L("Fan speed"), "" };
        line.label_path = category_path + "fan-settings";
        line.append_option(optgroup->get_option("min_fan_speed"));
        line.append_option(optgroup->get_option("max_fan_speed"));
        optgroup->append_line(line);

        optgroup->append_single_option_line("bridge_fan_speed", category_path + "fan-settings");
        optgroup->append_single_option_line("disable_fan_first_layers", category_path + "fan-settings");
        optgroup->append_single_option_line("full_fan_speed_layer", category_path + "fan-settings");

        optgroup = page->new_optgroup(L("Dynamic fan speeds"), 25);
        optgroup->append_single_option_line("enable_dynamic_fan_speeds", category_path + "dynamic-fan-speeds");
        optgroup->append_single_option_line("overhang_fan_speed_0", category_path + "dynamic-fan-speeds");
        optgroup->append_single_option_line("overhang_fan_speed_1", category_path + "dynamic-fan-speeds");
        optgroup->append_single_option_line("overhang_fan_speed_2", category_path + "dynamic-fan-speeds");
        optgroup->append_single_option_line("overhang_fan_speed_3", category_path + "dynamic-fan-speeds");

        optgroup = page->new_optgroup(L("Cooling thresholds"), 25);
        optgroup->append_single_option_line("fan_below_layer_time", category_path + "cooling-thresholds");
        optgroup->append_single_option_line("slowdown_below_layer_time", category_path + "cooling-thresholds");
        optgroup->append_single_option_line("min_print_speed", category_path + "cooling-thresholds");

    page = add_options_page(L("Advanced"), "wrench");
        optgroup = page->new_optgroup(L("Filament properties"));
        // Set size as all another fields for a better alignment
        Option option = optgroup->get_option("filament_type");
        option.opt.width = Field::def_width();
        optgroup->append_single_option_line(option);
        optgroup->append_single_option_line("filament_soluble");
        optgroup->append_single_option_line("filament_abrasive");

        optgroup = page->new_optgroup(L("Print speed override"));
        optgroup->append_single_option_line("filament_max_volumetric_speed", "max-volumetric-speed_127176");

        line = { "", "" };
        line.full_width = 1;
        line.widget = [this](wxWindow* parent) {
            return description_line_widget(parent, &m_volumetric_speed_description_line);
        };
        optgroup->append_line(line);

        optgroup->append_single_option_line("filament_infill_max_speed", "max-simple-infill-speed");
        optgroup->append_single_option_line("filament_infill_max_crossing_speed", "max-crossing-infill-speed");

        optgroup = page->new_optgroup(L("Shrinkage compensation"));
        optgroup->append_single_option_line("filament_shrinkage_compensation_xy");
        optgroup->append_single_option_line("filament_shrinkage_compensation_z");

        optgroup = page->new_optgroup(L("Wipe tower parameters"));
        optgroup->append_single_option_line("filament_minimal_purge_on_wipe_tower");

        optgroup = page->new_optgroup(L("Toolchange parameters with single extruder MM printers"));
        optgroup->append_single_option_line("filament_loading_speed_start");
        optgroup->append_single_option_line("filament_loading_speed");
        optgroup->append_single_option_line("filament_unloading_speed_start");
        optgroup->append_single_option_line("filament_unloading_speed");
        optgroup->append_single_option_line("filament_load_time");
        optgroup->append_single_option_line("filament_unload_time");
        optgroup->append_single_option_line("filament_toolchange_delay");
        optgroup->append_single_option_line("filament_cooling_moves");
        optgroup->append_single_option_line("filament_cooling_initial_speed");
        optgroup->append_single_option_line("filament_cooling_final_speed");
        optgroup->append_single_option_line("filament_stamping_loading_speed");
        optgroup->append_single_option_line("filament_stamping_distance");
        optgroup->append_single_option_line("filament_purge_multiplier");

        create_line_with_widget(optgroup.get(), "filament_ramming_parameters", "", [this](wxWindow* parent) {
            auto ramming_dialog_btn = new wxButton(parent, wxID_ANY, _(L("Ramming settings"))+dots, wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
            wxGetApp().SetWindowVariantForButton(ramming_dialog_btn);
            wxGetApp().UpdateDarkUI(ramming_dialog_btn);
            ramming_dialog_btn->SetFont(Slic3r::GUI::wxGetApp().normal_font());
            ramming_dialog_btn->SetSize(ramming_dialog_btn->GetBestSize());
            auto sizer = new wxBoxSizer(wxHORIZONTAL);
            sizer->Add(ramming_dialog_btn);

            ramming_dialog_btn->Bind(wxEVT_BUTTON, [this](wxCommandEvent& e) {
                RammingDialog dlg(this,(m_config->option<ConfigOptionStrings>("filament_ramming_parameters"))->get_at(0));
                if (dlg.ShowModal() == wxID_OK) {
                    load_key_value("filament_ramming_parameters", dlg.get_parameters());
                    update_changed_ui();
                }
            });
            return sizer;
        });


        optgroup = page->new_optgroup(L("Toolchange parameters with multi extruder MM printers"));
        optgroup->append_single_option_line("filament_multitool_ramming");
        optgroup->append_single_option_line("filament_multitool_ramming_volume");
        optgroup->append_single_option_line("filament_multitool_ramming_flow");


    add_filament_overrides_page();


        const int gcode_field_height = 15; // 150
        const int notes_field_height = 25; // 250

    page = add_options_page(L("Custom G-code"), "cog");
        optgroup = page->new_optgroup(L("Start G-code"), 0);
        optgroup->on_change = [this, &optgroup_title = optgroup->title](const t_config_option_key& opt_key, const boost::any& value) {
            validate_custom_gcode_cb(this, optgroup_title, opt_key, value);
        };
        optgroup->edit_custom_gcode = [this](const t_config_option_key& opt_key) { edit_custom_gcode(opt_key); };
        option = optgroup->get_option("start_filament_gcode");
        option.opt.full_width = true;
        option.opt.is_code = true;
        option.opt.height = gcode_field_height;// 150;
        optgroup->append_single_option_line(option);

        optgroup = page->new_optgroup(L("End G-code"), 0);
        optgroup->on_change = [this, &optgroup_title = optgroup->title](const t_config_option_key& opt_key, const boost::any& value) {
            validate_custom_gcode_cb(this, optgroup_title, opt_key, value);
        };
        optgroup->edit_custom_gcode = [this](const t_config_option_key& opt_key) { edit_custom_gcode(opt_key); };
        option = optgroup->get_option("end_filament_gcode");
        option.opt.full_width = true;
        option.opt.is_code = true;
        option.opt.height = gcode_field_height;// 150;
        optgroup->append_single_option_line(option);

    page = add_options_page(L("Notes"), "note");
        optgroup = page->new_optgroup(L("Notes"), 0);
        optgroup->label_width = 0;
        option = optgroup->get_option("filament_notes");
        option.opt.full_width = true;
        option.opt.height = notes_field_height;// 250;
        optgroup->append_single_option_line(option);

    page = add_options_page(L("Dependencies"), "wrench");
        optgroup = page->new_optgroup(L("Profile dependencies"));
        create_line_with_widget(optgroup.get(), "compatible_printers", "", [this](wxWindow* parent) {
            return compatible_widget_create(parent, m_compatible_printers);
        });

        option = optgroup->get_option("compatible_printers_condition");
        option.opt.full_width = true;
        optgroup->append_single_option_line(option);

        create_line_with_widget(optgroup.get(), "compatible_prints", "", [this](wxWindow* parent) {
            return compatible_widget_create(parent, m_compatible_prints);
        });

        option = optgroup->get_option("compatible_prints_condition");
        option.opt.full_width = true;
        optgroup->append_single_option_line(option);

        build_preset_description_line(optgroup.get());
}

void TabFilament::update_volumetric_flow_preset_hints()
{
    wxString text;
    try {
        text = from_u8(PresetHints::maximum_volumetric_flow_description(*m_preset_bundle));
    } catch (std::exception &ex) {
        text = _(L("Volumetric flow hints not available")) + "\n\n" + from_u8(ex.what());
    }
    m_volumetric_speed_description_line->SetText(text);
}

void TabFilament::update_description_lines()
{
    Tab::update_description_lines();

    if (!m_active_page)
        return;

    if (m_active_page->title() == "Cooling" && m_cooling_description_line)
        m_cooling_description_line->SetText(from_u8(PresetHints::cooling_description(m_presets->get_edited_preset())));
    if (m_active_page->title() == "Advanced" && m_volumetric_speed_description_line)
        this->update_volumetric_flow_preset_hints();
}

void TabFilament::toggle_options()
{
    if (!m_active_page)
        return;

    if (m_active_page->title() == "Cooling")
    {
        bool cooling = m_config->opt_bool("cooling", 0);
        bool fan_always_on = cooling || m_config->opt_bool("fan_always_on", 0);

        for (auto el : { "max_fan_speed", "fan_below_layer_time", "slowdown_below_layer_time", "min_print_speed" })
            toggle_option(el, cooling);

        for (auto el : { "min_fan_speed", "disable_fan_first_layers", "full_fan_speed_layer" })
            toggle_option(el, fan_always_on);

        bool dynamic_fan_speeds = m_config->opt_bool("enable_dynamic_fan_speeds", 0);
        for (int i = 0; i < 4; i++) {
            toggle_option("overhang_fan_speed_"+std::to_string(i),dynamic_fan_speeds);
        }
    }

    if (m_active_page->title() == "Advanced")
    {
        bool multitool_ramming = m_config->opt_bool("filament_multitool_ramming", 0);
        toggle_option("filament_multitool_ramming_volume", multitool_ramming);
        toggle_option("filament_multitool_ramming_flow", multitool_ramming);
    }

    if (m_active_page->title() == "Filament Overrides")
        update_filament_overrides_page();

    if (m_active_page->title() == "Filament") {
        Page* page = m_active_page;

        const auto og_it = std::find_if(page->m_optgroups.begin(), page->m_optgroups.end(), [](const ConfigOptionsGroupShp og) { return og->title == "Temperature"; });
        if (og_it != page->m_optgroups.end())
            update_line_with_near_label_widget(*og_it, "idle_temperature");
    }
}

void TabFilament::update()
{
    if (m_preset_bundle->printers.get_selected_preset().printer_technology() == ptSLA)
        return; // ys_FIXME

    m_update_cnt++;

    update_description_lines();
    Layout();

    toggle_options();

    m_update_cnt--;

    if (m_update_cnt == 0 && wxGetApp().mainframe)
        wxGetApp().mainframe->on_config_changed(m_config);
}

void TabFilament::clear_pages()
{
    Tab::clear_pages();

    m_volumetric_speed_description_line = nullptr;
	m_cooling_description_line = nullptr;

    for (auto& over_opt : m_overrides_options)
        over_opt.second = nullptr;
}

void TabFilament::msw_rescale()
{
    for (const auto& over_opt : m_overrides_options)
        if (wxWindow* win = over_opt.second)
            win->SetInitialSize(win->GetBestSize());
    Tab::msw_rescale();
}

void TabFilament::sys_color_changed()
{
    wxGetApp().UpdateDarkUI(m_extruders_cb);
    m_extruders_cb->Clear();
    update_extruder_combobox();

    Tab::sys_color_changed();

    for (const auto& over_opt : m_overrides_options)
        if (wxWindow* check_box = over_opt.second) {
            wxGetApp().UpdateDarkUI(check_box);
            CheckBox::SysColorChanged(check_box);
        }
}



void TabFilament::load_current_preset()
{
    const std::string& selected_filament_name = m_presets->get_selected_preset_name();
    if (m_active_extruder < 0) {
        // active extruder was invalidated before load new project file or configuration,
        // so we have to update active extruder selection from selected filament
        const std::string& edited_filament_name = m_presets->get_edited_preset().name;
        assert(!selected_filament_name.empty() && selected_filament_name == edited_filament_name);

        for (int i = 0; i < int(m_preset_bundle->extruders_filaments.size()); i++) {
            const std::string& selected_extr_filament_name = m_preset_bundle->extruders_filaments[i].get_selected_preset_name();
            if (selected_extr_filament_name == edited_filament_name) {
                m_active_extruder = i;
                break;
            }
        }
        assert(m_active_extruder >= 0);

        m_presets_choice->set_extruder_idx(m_active_extruder);
        if (m_active_extruder != m_extruders_cb->GetSelection())
            m_extruders_cb->Select(m_active_extruder);
    }

    assert(m_active_extruder >= 0 && size_t(m_active_extruder) < m_preset_bundle->extruders_filaments.size());
    const std::string& selected_extr_filament_name = m_preset_bundle->extruders_filaments[m_active_extruder].get_selected_preset_name();
    if (selected_extr_filament_name != selected_filament_name) {
        m_presets->select_preset_by_name(selected_extr_filament_name, false);

        // To avoid inconsistance between value of active_extruder in FilamentTab and TabPresetComboBox,
        // which can causes a crash on switch preset from MM printer to SM printer
        m_presets_choice->set_extruder_idx(m_active_extruder);
    }

    Tab::load_current_preset();
}

bool TabFilament::select_preset_by_name(const std::string &name_w_suffix, bool force)
{
    const bool is_selected_filament      = Tab::select_preset_by_name(name_w_suffix, force);
    const bool is_selected_extr_filament = m_preset_bundle->extruders_filaments[m_active_extruder].select_filament(name_w_suffix, force);
    return is_selected_filament && is_selected_extr_filament;
}

bool TabFilament::save_current_preset(const std::string &new_name, bool detach)
{
    m_preset_bundle->cache_extruder_filaments_names();
    const bool is_saved = Tab::save_current_preset(new_name, detach);// Note: is_saved is "true", if preset with new_name was saved as new preset

    // Reset extruder_filaments only if preset with new_name was saved as new preset
    if (is_saved)
        m_preset_bundle->reset_extruder_filaments();

    // Saved preset have to be selected for active extruder in any case 
    m_preset_bundle->extruders_filaments[m_active_extruder].select_filament(m_presets->get_idx_selected());
    return is_saved;
}

bool TabFilament::delete_current_preset()
{
    m_preset_bundle->cache_extruder_filaments_names();
    const bool is_deleted = Tab::delete_current_preset();
    if (is_deleted)
        m_preset_bundle->reset_extruder_filaments();
    return is_deleted;
}

wxSizer* Tab::description_line_widget(wxWindow* parent, ogStaticText* *StaticText, wxString text /*= wxEmptyString*/)
{
    *StaticText = new ogStaticText(parent, text);

//	auto font = (new wxSystemSettings)->GetFont(wxSYS_DEFAULT_GUI_FONT);
    (*StaticText)->SetFont(wxGetApp().normal_font());

    auto sizer = new wxBoxSizer(wxHORIZONTAL);
    sizer->Add(*StaticText, 1, wxEXPAND|wxALL, 0);
    return sizer;
}

bool Tab::saved_preset_is_dirty() const { return m_presets->saved_is_dirty(); }
void Tab::update_saved_preset_from_current_preset() { m_presets->update_saved_preset_from_current_preset(); }
bool Tab::current_preset_is_dirty() const { return m_presets->current_is_dirty(); }

void TabPrinter::build()
{
    m_presets = &m_preset_bundle->printers;
    m_printer_technology = m_presets->get_selected_preset().printer_technology();

    // For DiffPresetDialog we use options list which is saved in Searcher class.
    // Options for the Searcher is added in the moment of pages creation.
    // So, build first of all printer pages for non-selected printer technology...
    std::string def_preset_name = "- default " + std::string(m_printer_technology == ptSLA ? "FFF" : "SLA") + " -";
    m_config = &m_presets->find_preset(def_preset_name)->config;
    m_printer_technology == ptSLA ? build_fff() : build_sla();
    if (m_printer_technology == ptSLA)
        m_extruders_count_old = 0;// revert this value 

    // ... and than for selected printer technology
    load_initial_data();
    m_printer_technology == ptSLA ? build_sla() : build_fff();
}

void TabPrinter::build_print_host_upload_group(Page* page)
{
    ConfigOptionsGroupShp optgroup = page->new_optgroup(L("Print Host upload"));

    wxString description_line_text = _L(""
        "Note: All parameters from this group are moved to the Physical Printer settings (see changelog).\n\n"
        "A new Physical Printer profile is created by clicking on the \"cog\" icon right of the Printer profiles combo box, "
        "by selecting the \"Add physical printer\" item in the Printer combo box. The Physical Printer profile editor opens "
        "also when clicking on the \"cog\" icon in the Printer settings tab. The Physical Printer profiles are being stored "
        "into PrusaSlicer/physical_printer directory.");

    Line line = { "", "" };
    line.full_width = 1;
    line.widget = [this, description_line_text](wxWindow* parent) {
        return description_line_widget(parent, m_presets->get_selected_preset().printer_technology() == ptFFF ?
                                       &m_fff_print_host_upload_description_line : &m_sla_print_host_upload_description_line,
                                       description_line_text);
    };
    optgroup->append_line(line);
}

static wxString get_info_klipper_string()
{
    return _L("Emitting machine limits to G-code is not supported with Klipper G-code flavor.\n"
              "The option was switched to \"Use for time estimate\".");
}

void TabPrinter::build_fff()
{
    if (!m_pages.empty())
        m_pages.resize(0);
    // to avoid redundant memory allocation / deallocation during extruders count changing
    m_pages.reserve(30);

    auto   *nozzle_diameter = dynamic_cast<const ConfigOptionFloats*>(m_config->option("nozzle_diameter"));
    m_initial_extruders_count = m_extruders_count = nozzle_diameter->values.size();
    wxGetApp().sidebar().update_objects_list_extruder_column(m_initial_extruders_count);

    const Preset* parent_preset = m_printer_technology == ptSLA ? nullptr // just for first build, if SLA printer preset is selected 
                                  : m_presets->get_selected_preset_parent();
    m_sys_extruders_count = parent_preset == nullptr ? 0 :
            static_cast<const ConfigOptionFloats*>(parent_preset->config.option("nozzle_diameter"))->values.size();

    auto page = add_options_page(L("General"), "printer");
        auto optgroup = page->new_optgroup(L("Size and coordinates"));

        create_line_with_widget(optgroup.get(), "bed_shape", "custom-svg-and-png-bed-textures_124612", [this](wxWindow* parent) {
            return 	create_bed_shape_widget(parent);
        });

        optgroup->append_single_option_line("max_print_height");
        optgroup->append_single_option_line("z_offset");

        optgroup = page->new_optgroup(L("Capabilities"));
        ConfigOptionDef def;
            def.type =  coInt,
            def.set_default_value(new ConfigOptionInt(1));
            def.label = L("Extruders");
            def.tooltip = L("Number of extruders of the printer.");
            def.min = 1;
            def.max = 256;
            def.mode = comExpert;
        Option option(def, "extruders_count");
        optgroup->append_single_option_line(option);
        optgroup->append_single_option_line("single_extruder_multi_material");

        optgroup->on_change = [this, optgroup_wk = ConfigOptionsGroupWkp(optgroup)](t_config_option_key opt_key, boost::any value) {
            auto optgroup_sh = optgroup_wk.lock();
            if (!optgroup_sh)
                return;

            // optgroup->get_value() return int for def.type == coInt,
            // Thus, there should be boost::any_cast<int> !
            // Otherwise, boost::any_cast<size_t> causes an "unhandled unknown exception"
            size_t extruders_count = size_t(boost::any_cast<int>(optgroup_sh->get_value("extruders_count")));
            wxTheApp->CallAfter([this, opt_key, value, extruders_count]() {
                if (opt_key == "extruders_count" || opt_key == "single_extruder_multi_material") {
                    extruders_count_changed(extruders_count);
                    init_options_list(); // m_options_list should be updated before UI updating
                    update_dirty();
                    if (opt_key == "single_extruder_multi_material") { // the single_extruder_multimaterial was added to force pages
                        on_value_change(opt_key, value);                      // rebuild - let's make sure the on_value_change is not skipped

                        if (boost::any_cast<bool>(value) && m_extruders_count > 1) {
                            SuppressBackgroundProcessingUpdate sbpu;
                            std::vector<double> nozzle_diameters = static_cast<const ConfigOptionFloats*>(m_config->option("nozzle_diameter"))->values;
                            std::vector<unsigned char> high_flow_nozzles = static_cast<const ConfigOptionBools*>(m_config->option("nozzle_high_flow"))->values;
                            assert(nozzle_diameters.size() == high_flow_nozzles.size());

                            for (size_t i = 1; i < nozzle_diameters.size(); ++i) {
                                // if value is differs from first nozzle diameter value
                                if (fabs(nozzle_diameters[i] - nozzle_diameters[0]) > EPSILON || high_flow_nozzles[i] != high_flow_nozzles[0]) {
                                    const wxString msg_text = _(L("This is a single extruder multimaterial printer, \n"
                                                                  "all extruders must have the same nozzle diameter and 'High flow' state.\n"
                                                                  "Do you want to change these values for all extruders to first extruder values?"));
                                    MessageDialog dialog(parent(), msg_text, _(L("Extruder settings do not match")), wxICON_WARNING | wxYES_NO);

                                    DynamicPrintConfig new_conf = *m_config;
                                    if (dialog.ShowModal() == wxID_YES) {
                                        for (size_t i = 1; i < nozzle_diameters.size(); i++) {
                                            nozzle_diameters[i] = nozzle_diameters[0];
                                            high_flow_nozzles[i] = high_flow_nozzles[0];
                                        }

                                        new_conf.set_key_value("nozzle_diameter", new ConfigOptionFloats(nozzle_diameters));
                                        new_conf.set_key_value("nozzle_high_flow", new ConfigOptionBools(high_flow_nozzles));
                                    }
                                    else
                                        new_conf.set_key_value("single_extruder_multi_material", new ConfigOptionBool(false));

                                    load_config(new_conf);
                                    break;
                                }
                            }
                        }

                        m_preset_bundle->update_compatible(PresetSelectCompatibleType::Never);
                        // Upadte related comboboxes on Sidebar and Tabs
                        Sidebar& sidebar = wxGetApp().plater()->sidebar();
                        for (const Preset::Type& type : {Preset::TYPE_PRINT, Preset::TYPE_FILAMENT}) {
                            sidebar.update_presets(type);
                            wxGetApp().get_tab(type)->update_tab_ui();
                        }
                    }
                }
                else {
                    update_dirty();
                    on_value_change(opt_key, value);
                }
            });
        };

        build_print_host_upload_group(page.get());

        optgroup = page->new_optgroup(L("Firmware"));
        optgroup->append_single_option_line("gcode_flavor");

        option = optgroup->get_option("thumbnails");
        option.opt.full_width = true;
        optgroup->append_single_option_line(option);

        optgroup->append_single_option_line("silent_mode");
        optgroup->append_single_option_line("remaining_times");
        optgroup->append_single_option_line("binary_gcode");

        optgroup->on_change = [this](t_config_option_key opt_key, boost::any value) {
            wxTheApp->CallAfter([this, opt_key, value]() {
                if (opt_key == "thumbnails" && m_config->has("thumbnails_format")) {
                    // to backward compatibility we need to update "thumbnails_format" from new "thumbnails"
                    if (const std::string val = boost::any_cast<std::string>(value); !value.empty()) {
                        auto [thumbnails_list, errors] = GCodeThumbnails::make_and_check_thumbnail_list(val);

                        if (errors != enum_bitmask<ThumbnailError>()) {
                            // TRN: First argument is parameter name, the second one is the value.
                            std::string error_str = format(_u8L("Invalid value provided for parameter %1%: %2%"), "thumbnails", val);
                            error_str += GCodeThumbnails::get_error_string(errors);
                            InfoDialog(parent(), _L("G-code flavor is switched"), from_u8(error_str)).ShowModal();
                        }

                        if (!thumbnails_list.empty()) {
                            GCodeThumbnailsFormat old_format = GCodeThumbnailsFormat(m_config->option("thumbnails_format")->getInt());
                            GCodeThumbnailsFormat new_format = thumbnails_list.begin()->first;
                            if (old_format != new_format) {
                                DynamicPrintConfig new_conf = *m_config;

                                auto* opt = m_config->option("thumbnails_format")->clone();
                                opt->setInt(int(new_format));
                                new_conf.set_key_value("thumbnails_format", opt);

                                load_config(new_conf);
                            }
                        }
                    }
                }
                if (opt_key == "silent_mode") {
                    bool val = boost::any_cast<bool>(value);
                    if (m_use_silent_mode != val) {
                        m_rebuild_kinematics_page = true;
                        m_use_silent_mode = val;
                    }
                }
                if (opt_key == "gcode_flavor") {
                    const GCodeFlavor flavor = static_cast<GCodeFlavor>(boost::any_cast<int>(value));
                    bool supports_travel_acceleration = GCodeWriter::supports_separate_travel_acceleration(flavor);
                    bool supports_min_feedrates       = (flavor == gcfMarlinFirmware || flavor == gcfMarlinLegacy);
                    if (supports_travel_acceleration != m_supports_travel_acceleration || supports_min_feedrates != m_supports_min_feedrates) {
                        m_rebuild_kinematics_page = true;
                        m_supports_travel_acceleration = supports_travel_acceleration;
                        m_supports_min_feedrates = supports_min_feedrates;
                    }

                    const bool is_emit_to_gcode = m_config->option("machine_limits_usage")->getInt() == static_cast<int>(MachineLimitsUsage::EmitToGCode);
                    if ((flavor == gcfKlipper && is_emit_to_gcode) || (!m_supports_min_feedrates && m_use_silent_mode)) {
                        DynamicPrintConfig new_conf = *m_config;
                        wxString msg;

                        if (flavor == gcfKlipper && is_emit_to_gcode) {
                            msg = get_info_klipper_string();

                            auto machine_limits_usage = static_cast<ConfigOptionEnum<MachineLimitsUsage>*>(m_config->option("machine_limits_usage")->clone());
                            machine_limits_usage->value = MachineLimitsUsage::TimeEstimateOnly;
                            new_conf.set_key_value("machine_limits_usage", machine_limits_usage);
                        }

                        if (!m_supports_min_feedrates && m_use_silent_mode) {
                            if (!msg.IsEmpty())
                                msg += "\n\n";
                            msg += _L("The selected G-code flavor does not support the machine limitation for Stealth mode.\n"
                                      "Stealth mode will not be applied and will be disabled.");

                            auto silent_mode = static_cast<ConfigOptionBool*>(m_config->option("silent_mode")->clone());
                            silent_mode->value = false;
                            new_conf.set_key_value("silent_mode", silent_mode);
                        }

                        InfoDialog(parent(), _L("G-code flavor is switched"), msg).ShowModal();
                        load_config(new_conf);
                    }
                }
                build_unregular_pages();
                update_dirty();
                on_value_change(opt_key, value);
            });
        };

        optgroup = page->new_optgroup(L("Advanced"));
        optgroup->append_single_option_line("use_relative_e_distances");
        optgroup->append_single_option_line("use_firmware_retraction");
        optgroup->append_single_option_line("use_volumetric_e");
        optgroup->append_single_option_line("variable_layer_height");
        optgroup->append_single_option_line("prefer_clockwise_movements");

    const int gcode_field_height = 15; // 150
    const int notes_field_height = 25; // 250
    page = add_options_page(L("Custom G-code"), "cog");
        optgroup = page->new_optgroup(L("Start G-code"), 0);
        optgroup->on_change = [this, &optgroup_title = optgroup->title](const t_config_option_key& opt_key, const boost::any& value) {
            validate_custom_gcode_cb(this, optgroup_title, opt_key, value);
        };
        optgroup->edit_custom_gcode = [this](const t_config_option_key& opt_key) { edit_custom_gcode(opt_key); };
        option = optgroup->get_option("start_gcode");
        option.opt.full_width = true;
        option.opt.is_code = true;
        option.opt.height = 3 * gcode_field_height;//150;
        optgroup->append_single_option_line(option);

        optgroup = page->new_optgroup(L("Start G-Code options"));
        optgroup->append_single_option_line("autoemit_temperature_commands");

        optgroup = page->new_optgroup(L("End G-code"), 0);
        optgroup->on_change = [this, &optgroup_title = optgroup->title](const t_config_option_key& opt_key, const boost::any& value) {
            validate_custom_gcode_cb(this, optgroup_title, opt_key, value);
        };
        optgroup->edit_custom_gcode = [this](const t_config_option_key& opt_key) { edit_custom_gcode(opt_key); };
        option = optgroup->get_option("end_gcode");
        option.opt.full_width = true;
        option.opt.is_code = true;
        option.opt.height = 1.75 * gcode_field_height;//150;
        optgroup->append_single_option_line(option);

        optgroup = page->new_optgroup(L("Before layer change G-code"), 0);
        optgroup->on_change = [this, &optgroup_title = optgroup->title](const t_config_option_key& opt_key, const boost::any& value) {
            validate_custom_gcode_cb(this, optgroup_title, opt_key, value);
        };
        optgroup->edit_custom_gcode = [this](const t_config_option_key& opt_key) { edit_custom_gcode(opt_key); };
        option = optgroup->get_option("before_layer_gcode");
        option.opt.full_width = true;
        option.opt.is_code = true;
        option.opt.height = gcode_field_height;//150;
        optgroup->append_single_option_line(option);

        optgroup = page->new_optgroup(L("After layer change G-code"), 0);
        optgroup->on_change = [this, &optgroup_title = optgroup->title](const t_config_option_key& opt_key, const boost::any& value) {
            validate_custom_gcode_cb(this, optgroup_title, opt_key, value);
        };
        optgroup->edit_custom_gcode = [this](const t_config_option_key& opt_key) { edit_custom_gcode(opt_key); };
        option = optgroup->get_option("layer_gcode");
        option.opt.full_width = true;
        option.opt.is_code = true;
        option.opt.height = gcode_field_height;//150;
        optgroup->append_single_option_line(option);

        optgroup = page->new_optgroup(L("Tool change G-code"), 0);
        optgroup->on_change = [this, &optgroup_title = optgroup->title](const t_config_option_key& opt_key, const boost::any& value) {
            validate_custom_gcode_cb(this, optgroup_title, opt_key, value);
        };
        optgroup->edit_custom_gcode = [this](const t_config_option_key& opt_key) { edit_custom_gcode(opt_key); };
        option = optgroup->get_option("toolchange_gcode");
        option.opt.full_width = true;
        option.opt.is_code = true;
        option.opt.height = gcode_field_height;//150;
        optgroup->append_single_option_line(option);

        optgroup = page->new_optgroup(L("Between objects G-code (for sequential printing)"), 0);
        optgroup->on_change = [this, &optgroup_title = optgroup->title](const t_config_option_key& opt_key, const boost::any& value) {
            validate_custom_gcode_cb(this, optgroup_title, opt_key, value);
        };
        optgroup->edit_custom_gcode = [this](const t_config_option_key& opt_key) { edit_custom_gcode(opt_key); };
        option = optgroup->get_option("between_objects_gcode");
        option.opt.full_width = true;
        option.opt.is_code = true;
        option.opt.height = gcode_field_height;//150;
        optgroup->append_single_option_line(option);

        optgroup = page->new_optgroup(L("Color Change G-code"), 0);
        optgroup->on_change = [this, &optgroup_title = optgroup->title](const t_config_option_key& opt_key, const boost::any& value) {
            validate_custom_gcode_cb(this, optgroup_title, opt_key, value);
        };
        optgroup->edit_custom_gcode = [this](const t_config_option_key& opt_key) { edit_custom_gcode(opt_key); };
        option = optgroup->get_option("color_change_gcode");
        option.opt.is_code = true;
        option.opt.height = gcode_field_height;//150;
        optgroup->append_single_option_line(option);

        optgroup = page->new_optgroup(L("Pause Print G-code"), 0);
        optgroup->on_change = [this, &optgroup_title = optgroup->title](const t_config_option_key& opt_key, const boost::any& value) {
            validate_custom_gcode_cb(this, optgroup_title, opt_key, value);
        };
        optgroup->edit_custom_gcode = [this](const t_config_option_key& opt_key) { edit_custom_gcode(opt_key); };
        option = optgroup->get_option("pause_print_gcode");
        option.opt.is_code = true;
        option.opt.height = gcode_field_height;//150;
        optgroup->append_single_option_line(option);

        optgroup = page->new_optgroup(L("Template Custom G-code"), 0);
        optgroup->on_change = [this, &optgroup_title = optgroup->title](const t_config_option_key& opt_key, const boost::any& value) {
            validate_custom_gcode_cb(this, optgroup_title, opt_key, value);
        };
        optgroup->edit_custom_gcode = [this](const t_config_option_key& opt_key) { edit_custom_gcode(opt_key); };
        option = optgroup->get_option("template_custom_gcode");
        option.opt.is_code = true;
        option.opt.height = gcode_field_height;//150;
        optgroup->append_single_option_line(option);

    page = add_options_page(L("Notes"), "note");
        optgroup = page->new_optgroup(L("Notes"), 0);
        option = optgroup->get_option("printer_notes");
        option.opt.full_width = true;
        option.opt.height = notes_field_height;//250;
        optgroup->append_single_option_line(option);

    page = add_options_page(L("Dependencies"), "wrench");
        optgroup = page->new_optgroup(L("Profile dependencies"));

        build_preset_description_line(optgroup.get());

    build_unregular_pages(true);
}

void TabPrinter::build_sla()
{
    if (!m_pages.empty())
        m_pages.resize(0);
    auto page = add_options_page(L("General"), "printer");
    auto optgroup = page->new_optgroup(L("Size and coordinates"));

    create_line_with_widget(optgroup.get(), "bed_shape", "custom-svg-and-png-bed-textures_124612", [this](wxWindow* parent) {
        return 	create_bed_shape_widget(parent);
    });
    optgroup->append_single_option_line("max_print_height");

    optgroup = page->new_optgroup(L("Display"));
    optgroup->append_single_option_line("display_width");
    optgroup->append_single_option_line("display_height");

    auto option = optgroup->get_option("display_pixels_x");
    Line line = { option.opt.full_label, "" };
    line.append_option(option);
    line.append_option(optgroup->get_option("display_pixels_y"));
    optgroup->append_line(line);
    optgroup->append_single_option_line("display_orientation");

    // FIXME: This should be on one line in the UI
    optgroup->append_single_option_line("display_mirror_x");
    optgroup->append_single_option_line("display_mirror_y");

    optgroup = page->new_optgroup(L("Tilt"));
    line = { L("Tilt time"), "" };
    line.append_option(optgroup->get_option("fast_tilt_time"));
    line.append_option(optgroup->get_option("slow_tilt_time"));
    line.append_option(optgroup->get_option("high_viscosity_tilt_time"));
    optgroup->append_line(line);
//    optgroup->append_single_option_line("area_fill");

    optgroup = page->new_optgroup(L("Corrections"));
    line = Line{ m_config->def()->get("relative_correction")->full_label, "" };
    for (auto& axis : { "X", "Y", "Z" }) {
        auto opt = optgroup->get_option(std::string("relative_correction_") + char(std::tolower(axis[0])));
        opt.opt.label = axis;
        line.append_option(opt);
    }
    optgroup->append_line(line);
    optgroup->append_single_option_line("absolute_correction");
    optgroup->append_single_option_line("elefant_foot_compensation");
    optgroup->append_single_option_line("elefant_foot_min_width");
    optgroup->append_single_option_line("gamma_correction");
    
    optgroup = page->new_optgroup(L("Exposure"));
    optgroup->append_single_option_line("min_exposure_time");
    optgroup->append_single_option_line("max_exposure_time");
    optgroup->append_single_option_line("min_initial_exposure_time");
    optgroup->append_single_option_line("max_initial_exposure_time");


    optgroup = page->new_optgroup(L("Output"));
    optgroup->append_single_option_line("sla_archive_format");
    optgroup->append_single_option_line("sla_output_precision");

    build_print_host_upload_group(page.get());

    const int notes_field_height = 25; // 250

    page = add_options_page(L("Notes"), "note");
    optgroup = page->new_optgroup(L("Notes"), 0);
    option = optgroup->get_option("printer_notes");
    option.opt.full_width = true;
    option.opt.height = notes_field_height;//250;
    optgroup->append_single_option_line(option);

    page = add_options_page(L("Dependencies"), "wrench");
    optgroup = page->new_optgroup(L("Profile dependencies"));

    build_preset_description_line(optgroup.get());
}

void TabPrinter::extruders_count_changed(size_t extruders_count)
{
    bool is_count_changed = false;
    bool is_updated_mm_filament_presets = false;
    if (m_extruders_count != extruders_count) {
        m_extruders_count = extruders_count;
        m_preset_bundle->printers.get_edited_preset().set_num_extruders(extruders_count);
        is_count_changed = is_updated_mm_filament_presets = true;
    }
    else if (m_extruders_count == 1 &&
             m_preset_bundle->project_config.option<ConfigOptionFloats>("wiping_volumes_matrix")->values.size()>1) {
        is_updated_mm_filament_presets = true;
    }

    if (is_updated_mm_filament_presets) {
        m_preset_bundle->update_multi_material_filament_presets();
        m_preset_bundle->update_filaments_compatible(PresetSelectCompatibleType::OnlyIfWasCompatible);
    }

    /* This function should be call in any case because of correct updating/rebuilding
     * of unregular pages of a Printer Settings
     */
    build_unregular_pages();

    if (is_count_changed) {
        on_value_change("extruders_count", extruders_count);
        wxGetApp().sidebar().update_objects_list_extruder_column(extruders_count);
    }
}

void TabPrinter::append_option_line(ConfigOptionsGroupShp optgroup, const std::string opt_key)
{
    auto option = optgroup->get_option(opt_key, 0);
    auto line = Line{ option.opt.full_label, "" };
    line.append_option(option);
    if (m_use_silent_mode 
        || m_printer_technology == ptSLA // just for first build, if SLA printer preset is selected 
        )
        line.append_option(optgroup->get_option(opt_key, 1));
    optgroup->append_line(line);
}

// Legend for OptionsGroups                                     column's   name         tooltip
static void create_legend(Slic3r::GUI::PageShp page, const std::vector<std::pair<std::string, std::string>>& columns, ConfigOptionMode mode, bool is_wider = false)
{
    auto optgroup = page->new_optgroup("");
    auto line = Line{ "", "" };

    ConfigOptionDef def;
    def.type = coString;
    def.width = is_wider ? Field::def_width_wider() : Field::def_width();
    def.gui_type = ConfigOptionDef::GUIType::legend;
    def.mode = mode;

    for (auto& [name, tooltip] : columns) {
        def.tooltip = tooltip;
        def.set_default_value(new ConfigOptionString{ into_u8(_(name)) });

        auto option = Option(def, name + "_legend");
        line.append_option(option);
    }

    optgroup->append_line(line);
}

PageShp TabPrinter::build_kinematics_page()
{
    auto page = add_options_page(L("Machine limits"), "cog", true);

    auto optgroup = page->new_optgroup(L("General"));
    {
	    optgroup->append_single_option_line("machine_limits_usage");
        Line line { "", "" };
        line.full_width = 1;
        line.widget = [this](wxWindow* parent) {
            return description_line_widget(parent, &m_machine_limits_description_line);
        };
        optgroup->append_line(line);
    }

    optgroup->on_change = [this](const t_config_option_key& opt_key, boost::any value)
    {
        if (opt_key == "machine_limits_usage" &&
            static_cast<MachineLimitsUsage>(boost::any_cast<int>(value)) == MachineLimitsUsage::EmitToGCode &&
            static_cast<GCodeFlavor>(m_config->option("gcode_flavor")->getInt()) == gcfKlipper)
        {
            DynamicPrintConfig new_conf = *m_config;

            auto machine_limits_usage = static_cast<ConfigOptionEnum<MachineLimitsUsage>*>(m_config->option("machine_limits_usage")->clone());
            machine_limits_usage->value = MachineLimitsUsage::TimeEstimateOnly;

            new_conf.set_key_value("machine_limits_usage", machine_limits_usage);

            InfoDialog(parent(), wxEmptyString, get_info_klipper_string()).ShowModal();
            load_config(new_conf);
        }

        update_dirty();
        update();
    };

    if (m_use_silent_mode) {
        std::vector<std::pair<std::string, std::string>> legend_columns = {
            {L("Normal"), L("Values in this column are for Normal mode")},
            {L("Stealth"), L("Values in this column are for Stealth mode")}
        };

        create_legend(page, legend_columns, comAdvanced);
    }

    const std::vector<std::string> axes{ "x", "y", "z", "e" };
    optgroup = page->new_optgroup(L("Maximum feedrates"));
        for (const std::string &axis : axes)	{
            append_option_line(optgroup, "machine_max_feedrate_" + axis);
        }

    optgroup = page->new_optgroup(L("Maximum accelerations"));
        for (const std::string &axis : axes)	{
            append_option_line(optgroup, "machine_max_acceleration_" + axis);
        }
        append_option_line(optgroup, "machine_max_acceleration_extruding");
        append_option_line(optgroup, "machine_max_acceleration_retracting");
        if (m_supports_travel_acceleration)
            append_option_line(optgroup, "machine_max_acceleration_travel");

    optgroup = page->new_optgroup(L("Jerk limits"));
        for (const std::string &axis : axes)	{
            append_option_line(optgroup, "machine_max_jerk_" + axis);
        }

        if (m_supports_min_feedrates) {
            optgroup = page->new_optgroup(L("Minimum feedrates"));
            append_option_line(optgroup, "machine_min_extruding_rate");
            append_option_line(optgroup, "machine_min_travel_rate");
        }

    return page;
}

const std::vector<std::string> extruder_options = {
    "min_layer_height", "max_layer_height", "extruder_offset",
    "retract_length", "retract_lift", "retract_lift_above", "retract_lift_below",
    "retract_speed", "deretract_speed", "retract_restart_extra", "retract_before_travel",
    "retract_layer_change", "wipe", "retract_before_wipe", "travel_ramping_lift",
    "travel_slope", "travel_max_lift", "travel_lift_before_obstacle", "nozzle_high_flow",
    "retract_length_toolchange", "retract_restart_extra_toolchange",
};

void TabPrinter::build_extruder_pages(size_t n_before_extruders)
{
    for (auto extruder_idx = m_extruders_count_old; extruder_idx < m_extruders_count; ++extruder_idx) {
        //# build page
        const wxString&page_name = wxString::Format("Extruder %d", int(extruder_idx + 1));
        auto           page      = add_options_page(page_name, "funnel", true);
        m_pages.insert(m_pages.begin() + n_before_extruders + extruder_idx, page);

        auto optgroup = page->new_optgroup(L("Size"));
        optgroup->append_single_option_line("nozzle_diameter", "", extruder_idx);

        optgroup->on_change = [this, extruder_idx](const t_config_option_key&opt_key, boost::any value)
        {
            const bool is_single_extruder_MM = m_config->opt_bool("single_extruder_multi_material");
            const bool is_nozzle_diameter_changed = opt_key.find("nozzle_diameter") != std::string::npos;
            const bool is_high_flow_changed = opt_key.find("nozzle_high_flow") != std::string::npos;

            if (is_single_extruder_MM && m_extruders_count > 1 && is_nozzle_diameter_changed)
            {
                SuppressBackgroundProcessingUpdate sbpu;
                const double new_nd = boost::any_cast<double>(value);
                std::vector<double> nozzle_diameters = static_cast<const ConfigOptionFloats*>(m_config->option("nozzle_diameter"))->values;

                // if value was changed
                if (fabs(nozzle_diameters[extruder_idx == 0 ? 1 : 0] - new_nd) > EPSILON)
                {
                    const wxString msg_text = _L("This is a single extruder multimaterial printer, diameters of all extruders "
                                                 "will be set to the new value. Do you want to proceed?");
                    MessageDialog dialog(parent(), msg_text, _L("Nozzle diameter"), wxICON_WARNING | wxYES_NO);

                    DynamicPrintConfig new_conf = *m_config;
                    if (dialog.ShowModal() == wxID_YES) {
                        for (size_t i = 0; i < nozzle_diameters.size(); i++) {
                            if (i==extruder_idx)
                                continue;
                            nozzle_diameters[i] = new_nd;
                        }
                    }
                    else
                        nozzle_diameters[extruder_idx] = nozzle_diameters[extruder_idx == 0 ? 1 : 0];

                    new_conf.set_key_value("nozzle_diameter", new ConfigOptionFloats(nozzle_diameters));
                    load_config(new_conf);
                }
            }

            if (is_single_extruder_MM && m_extruders_count > 1 && is_high_flow_changed)
            {
                SuppressBackgroundProcessingUpdate sbpu;
                const unsigned char new_hf = boost::any_cast<unsigned char>(value);
                std::vector<unsigned char> nozzle_high_flow = static_cast<const ConfigOptionBools*>(m_config->option("nozzle_high_flow"))->values;

                // if value was changed
                if (nozzle_high_flow[extruder_idx == 0 ? 1 : 0] != new_hf)
                {
                    const wxString msg_text = _L("This is a single extruder multimaterial printer, 'high_flow' state of all extruders "
                                                 "will be set to the new value. Do you want to proceed?");
                    MessageDialog dialog(parent(), msg_text, _L("Extruder settings do not match"), wxICON_WARNING | wxYES_NO);

                    DynamicPrintConfig new_conf = *m_config;
                    if (dialog.ShowModal() == wxID_YES) {
                        for (size_t i = 0; i < nozzle_high_flow.size(); i++) {
                            if (i==extruder_idx)
                                continue;
                            nozzle_high_flow[i] = new_hf;
                        }
                    }
                    else
                        nozzle_high_flow[extruder_idx] = nozzle_high_flow[extruder_idx == 0 ? 1 : 0];

                    new_conf.set_key_value("nozzle_high_flow", new ConfigOptionBools(nozzle_high_flow));
                    load_config(new_conf);
                }
            }

            if (is_nozzle_diameter_changed || is_high_flow_changed) {
                if (extruder_idx == 0)
                    // Mark the print & filament enabled if they are compatible with the currently selected preset.
                    // If saving the preset changes compatibility with other presets, keep the now incompatible dependent presets selected, however with a "red flag" icon showing that they are no more compatible.
                    m_preset_bundle->update_compatible(PresetSelectCompatibleType::Never);
                else
                    m_preset_bundle->update_filaments_compatible(PresetSelectCompatibleType::Never, extruder_idx);
            }

            update_dirty();
            update();
        };

        optgroup->append_single_option_line("nozzle_high_flow", "", extruder_idx);

        optgroup = page->new_optgroup(L("Preview"));

        auto reset_to_filament_color = [this, extruder_idx](wxWindow*parent) {
            ScalableButton* btn = new ScalableButton(parent, wxID_ANY, "undo", _L("Reset to Filament Color"),
                                                     wxDefaultSize, wxDefaultPosition, wxBU_LEFT | wxBU_EXACTFIT);
            btn->SetFont(wxGetApp().normal_font());
            btn->SetSize(btn->GetBestSize());
            auto sizer = new wxBoxSizer(wxHORIZONTAL);
            sizer->Add(btn);

            btn->Bind(wxEVT_BUTTON, [this, extruder_idx](wxCommandEvent&e)
            {
                std::vector<std::string> colors = static_cast<const ConfigOptionStrings*>(m_config->option("extruder_colour"))->values;
                colors[extruder_idx]            = "";

                DynamicPrintConfig new_conf = *m_config;
                new_conf.set_key_value("extruder_colour", new ConfigOptionStrings(colors));
                load_config(new_conf);

                update_dirty();
                update();
            });

            parent->Bind(wxEVT_UPDATE_UI, [this, extruder_idx](wxUpdateUIEvent& evt) {
                evt.Enable(!static_cast<const ConfigOptionStrings*>(m_config->option("extruder_colour"))->values[extruder_idx].empty());
            }, btn->GetId());

            return sizer;
        };
        Line line = optgroup->create_single_option_line("extruder_colour", "", extruder_idx);
        line.append_widget(reset_to_filament_color);
        optgroup->append_line(line);

        optgroup = page->new_optgroup("");

        auto copy_settings_btn = 
        line            = { "", ""};
        line.full_width = 1;
        line.widget = [this, extruder_idx](wxWindow* parent) {
            ScalableButton* btn = new ScalableButton(parent, wxID_ANY, "copy", _L("Apply below setting to other extruders"),
                                                     wxDefaultSize, wxDefaultPosition, wxBU_LEFT | wxBU_EXACTFIT);
            auto sizer = new wxBoxSizer(wxHORIZONTAL);
            sizer->Add(btn);

            btn->Bind(wxEVT_BUTTON, [this, extruder_idx](wxCommandEvent& e) {
                DynamicPrintConfig new_conf = *m_config;

                for (const std::string& opt : extruder_options) {
                    const ConfigOption* other_opt = m_config->option(opt);
                    for (size_t extruder = 0; extruder < m_extruders_count; ++extruder) {
                        if (extruder == extruder_idx)
                            continue;
                        static_cast<ConfigOptionVectorBase*>(new_conf.option(opt, false))->set_at(other_opt, extruder, extruder_idx);
                    }
                }
                load_config(new_conf);

                update_dirty();
                update();
            });

            auto has_changes = [this]() {
                auto dirty_options = m_presets->current_dirty_options(true);
#if 1
                dirty_options.erase(std::remove_if(dirty_options.begin(), dirty_options.end(), 
                    [](const std::string& opt) { return opt.find("extruder_colour") != std::string::npos || opt.find("nozzle_diameter") != std::string::npos; }), dirty_options.end());
                return !dirty_options.empty();
#else
                // if we wont to apply enable status for each extruder separately
                for (const std::string& opt : extruder_options)
                    if (std::find(dirty_options.begin(), dirty_options.end(), opt+"#"+std::to_string(extruder_idx)) != dirty_options.end())
                        return true;
                return false;
#endif
            };

            parent->Bind(wxEVT_UPDATE_UI, [this, has_changes](wxUpdateUIEvent& evt) {
                evt.Enable(m_extruders_count > 1 && has_changes());
            }, btn->GetId());

            return sizer;
        };
        optgroup->append_line(line);

        optgroup = page->new_optgroup(L("Layer height limits"));
        optgroup->append_single_option_line("min_layer_height", "", extruder_idx);
        optgroup->append_single_option_line("max_layer_height", "", extruder_idx);

        optgroup = page->new_optgroup(L("Position (for multi-extruder printers)"));
        optgroup->append_single_option_line("extruder_offset", "", extruder_idx);

        optgroup = page->new_optgroup(L("Travel lift"));
        optgroup->append_single_option_line("retract_lift", "", extruder_idx);
        optgroup->append_single_option_line("travel_ramping_lift", "", extruder_idx);
        optgroup->append_single_option_line("travel_max_lift", "", extruder_idx);
        optgroup->append_single_option_line("travel_slope", "", extruder_idx);
        optgroup->append_single_option_line("travel_lift_before_obstacle", "", extruder_idx);

        line = { L("Only lift"), "" };
        line.append_option(optgroup->get_option("retract_lift_above", extruder_idx));
        line.append_option(optgroup->get_option("retract_lift_below", extruder_idx));
        optgroup->append_line(line);

        optgroup = page->new_optgroup(L("Retraction"));
        optgroup->append_single_option_line("retract_length", "", extruder_idx);
        optgroup->append_single_option_line("retract_speed", "", extruder_idx);
        optgroup->append_single_option_line("deretract_speed", "", extruder_idx);
        optgroup->append_single_option_line("retract_restart_extra", "", extruder_idx);
        optgroup->append_single_option_line("retract_before_travel", "", extruder_idx);
        optgroup->append_single_option_line("retract_layer_change", "", extruder_idx);
        optgroup->append_single_option_line("wipe", "", extruder_idx);
        optgroup->append_single_option_line("retract_before_wipe", "", extruder_idx);

        optgroup = page->new_optgroup(L("Retraction when tool is disabled (advanced settings for multi-extruder setups)"));
        optgroup->append_single_option_line("retract_length_toolchange", "", extruder_idx);
        optgroup->append_single_option_line("retract_restart_extra_toolchange", "", extruder_idx);
    }

    // # remove extra pages
    if (m_extruders_count < m_extruders_count_old)
        m_pages.erase(	m_pages.begin() + n_before_extruders + m_extruders_count,
                        m_pages.begin() + n_before_extruders + m_extruders_count_old);
}

/* Previous name build_extruder_pages().
 *
 * This function was renamed because of now it implements not just an extruder pages building,
 * but "Machine limits" and "Single extruder MM setup" too
 * (These pages can changes according to the another values of a current preset)
 * */
void TabPrinter::build_unregular_pages(bool from_initial_build/* = false*/)
{
    size_t		n_before_extruders = 2;			//	Count of pages before Extruder pages
    auto        flavor = m_config->option<ConfigOptionEnum<GCodeFlavor>>("gcode_flavor")->value;
    bool		show_mach_limits = (flavor == gcfMarlinLegacy || flavor == gcfMarlinFirmware || flavor == gcfRepRapFirmware || flavor == gcfKlipper);

    /* ! Freeze/Thaw in this function is needed to avoid call OnPaint() for erased pages
     * and be cause of application crash, when try to change Preset in moment,
     * when one of unregular pages is selected.
     *  */
    Freeze();

    // Add/delete Kinematics page according to show_mach_limits
    size_t existed_page = 0;
    for (size_t i = n_before_extruders; i < m_pages.size(); ++i) // first make sure it's not there already
        if (m_pages[i]->title().find(L("Machine limits")) != std::string::npos) {
            if (!show_mach_limits || m_rebuild_kinematics_page)
                m_pages.erase(m_pages.begin() + i);
            else
                existed_page = i;
            break;
        }

    if (existed_page < n_before_extruders && (show_mach_limits || from_initial_build)) {
        auto page = build_kinematics_page();
        if (from_initial_build && !show_mach_limits)
            page->clear();
        else
            m_pages.insert(m_pages.begin() + n_before_extruders, page);
    }

    if (show_mach_limits)
        n_before_extruders++;
    size_t		n_after_single_extruder_MM = 2; //	Count of pages after single_extruder_multi_material page

    if (m_extruders_count_old == m_extruders_count ||
        (m_has_single_extruder_MM_page && m_extruders_count == 1))
    {
        // if we have a single extruder MM setup, add a page with configuration options:
        for (size_t i = 0; i < m_pages.size(); ++i) // first make sure it's not there already
            if (m_pages[i]->title().find(L("Single extruder MM setup")) != std::string::npos) {
                m_pages.erase(m_pages.begin() + i);
                break;
            }
        m_has_single_extruder_MM_page = false;
    }
    if (from_initial_build ||
        (m_extruders_count > 1 && m_config->opt_bool("single_extruder_multi_material") && !m_has_single_extruder_MM_page)) {
        // create a page, but pretend it's an extruder page, so we can add it to m_pages ourselves
        auto page = add_options_page(L("Single extruder MM setup"), "printer", true);
        auto optgroup = page->new_optgroup(L("Single extruder multimaterial parameters"));
        optgroup->append_single_option_line("cooling_tube_retraction");
        optgroup->append_single_option_line("cooling_tube_length");
        optgroup->append_single_option_line("parking_pos_retraction");
        optgroup->append_single_option_line("extra_loading_move");
        optgroup->append_single_option_line("multimaterial_purging");
        optgroup->append_single_option_line("high_current_on_filament_swap");
        if (from_initial_build)
            page->clear();
        else {
            m_pages.insert(m_pages.end() - n_after_single_extruder_MM, page);
            m_has_single_extruder_MM_page = true;
        }
    }

    // Build missed extruder pages
    build_extruder_pages(n_before_extruders);

    Thaw();

    m_extruders_count_old = m_extruders_count;

    if (from_initial_build && m_printer_technology == ptSLA)
        return; // next part of code is no needed to execute at this moment

    rebuild_page_tree();

    // Reload preset pages with current configuration values
    reload_config();
}

// this gets executed after preset is loaded and before GUI fields are updated
void TabPrinter::on_preset_loaded()
{
    // update the extruders count field
    auto   *nozzle_diameter = dynamic_cast<const ConfigOptionFloats*>(m_config->option("nozzle_diameter"));
    size_t extruders_count = nozzle_diameter->values.size();
    // update the GUI field according to the number of nozzle diameters supplied
    extruders_count_changed(extruders_count);
}

void TabPrinter::update_pages()
{
    // update m_pages ONLY if printer technology is changed
    const PrinterTechnology new_printer_technology = m_presets->get_edited_preset().printer_technology();
    if (new_printer_technology == m_printer_technology)
        return;

    //clear all active pages before switching
    clear_pages();

    // set m_pages to m_pages_(technology before changing)
    m_printer_technology == ptFFF ? m_pages.swap(m_pages_fff) : m_pages.swap(m_pages_sla);

    // build Tab according to the technology, if it's not exist jet OR
    // set m_pages_(technology after changing) to m_pages
    // m_printer_technology will be set by Tab::load_current_preset()
    if (new_printer_technology == ptFFF)
    {
        if (m_pages_fff.empty())
        {
            build_fff();
            if (m_extruders_count > 1)
            {
                m_preset_bundle->update_multi_material_filament_presets();
                m_preset_bundle->update_filaments_compatible(PresetSelectCompatibleType::OnlyIfWasCompatible);
                on_value_change("extruders_count", m_extruders_count);
            }
        }
        else
            m_pages.swap(m_pages_fff);

         wxGetApp().sidebar().update_objects_list_extruder_column(m_extruders_count);
    }
    else
        m_pages_sla.empty() ? build_sla() : m_pages.swap(m_pages_sla);

    rebuild_page_tree();
}

void TabPrinter::reload_config()
{
    Tab::reload_config();

    // "extruders_count" doesn't update from the update_config(),
    // so update it implicitly
    if (m_active_page && m_active_page->title() == "General")
        m_active_page->set_value("extruders_count", int(m_extruders_count));
}

void TabPrinter::activate_selected_page(std::function<void()> throw_if_canceled)
{
    Tab::activate_selected_page(throw_if_canceled);

    // "extruders_count" doesn't update from the update_config(),
    // so update it implicitly
    if (m_active_page && m_active_page->title() == "General")
        m_active_page->set_value("extruders_count", int(m_extruders_count));
}

void TabPrinter::clear_pages()
{
    Tab::clear_pages();

    m_machine_limits_description_line           = nullptr;
    m_fff_print_host_upload_description_line    = nullptr;
    m_sla_print_host_upload_description_line    = nullptr;
}

void TabPrinter::toggle_options()
{
    if (!m_active_page || m_presets->get_edited_preset().printer_technology() == ptSLA)
        return;

    const GCodeFlavor flavor = m_config->option<ConfigOptionEnum<GCodeFlavor>>("gcode_flavor")->value;
    bool have_multiple_extruders = m_extruders_count > 1;
    if (m_active_page->title() == "Custom G-code")
        toggle_option("toolchange_gcode", have_multiple_extruders);
    if (m_active_page->title() == "General") {
        toggle_option("single_extruder_multi_material", have_multiple_extruders);

        bool is_marlin_flavor = flavor == gcfMarlinLegacy || flavor == gcfMarlinFirmware;
        // Disable silent mode for non-marlin firmwares.
        toggle_option("silent_mode", is_marlin_flavor);
    }

    wxString extruder_number;
    long val;
    if (m_active_page->title().StartsWith("Extruder ", &extruder_number) && extruder_number.ToLong(&val) &&
        val > 0 && (size_t)val <= m_extruders_count)
    {
        size_t i = size_t(val - 1);
        bool have_retract_length = m_config->opt_float("retract_length", i) > 0;
        const bool ramping_lift = m_config->opt_bool("travel_ramping_lift", i);
        const bool lifts_z = (ramping_lift && m_config->opt_float("travel_max_lift", i) > 0)
                          || (! ramping_lift && m_config->opt_float("retract_lift", i) > 0);


        // when using firmware retraction, firmware decides retraction length
        bool use_firmware_retraction = m_config->opt_bool("use_firmware_retraction");
        toggle_option("retract_length", !use_firmware_retraction, i);

        toggle_option("retract_lift", ! ramping_lift, i);
        toggle_option("travel_max_lift", ramping_lift, i);
        toggle_option("travel_slope", ramping_lift, i);

        // user can customize travel length if we have retraction length or we"re using
        // firmware retraction
        toggle_option("retract_before_travel", have_retract_length || use_firmware_retraction, i);

        // user can customize other retraction options if retraction is enabled
        bool retraction = (have_retract_length || use_firmware_retraction);
        std::vector<std::string> vec = {  };
        for (auto el : vec)
            toggle_option("retract_layer_change", retraction, i);

        // retract lift above / below only applies if using retract lift
        vec.resize(0);
        vec = { "retract_lift_above", "retract_lift_below" };
        for (auto el : vec)
            toggle_option(el, lifts_z, i);

        // some options only apply when not using firmware retraction
        vec.resize(0);
        vec = { "retract_speed", "deretract_speed", "retract_before_wipe", "retract_restart_extra", "wipe" };
        for (auto el : vec)
            toggle_option(el, retraction && !use_firmware_retraction, i);

        bool wipe = m_config->opt_bool("wipe", i);
        toggle_option("retract_before_wipe", wipe, i);

        if (use_firmware_retraction && wipe) {
            //wxMessageDialog dialog(parent(),
            MessageDialog dialog(parent(),
                _(L("The Wipe option is not available when using the Firmware Retraction mode.\n"
                    "\nShall I disable it in order to enable Firmware Retraction?")),
                _(L("Firmware Retraction")), wxICON_WARNING | wxYES | wxNO);

            DynamicPrintConfig new_conf = *m_config;
            if (dialog.ShowModal() == wxID_YES) {
                auto wipe = static_cast<ConfigOptionBools*>(m_config->option("wipe")->clone());
                for (size_t w = 0; w < wipe->values.size(); w++)
                    wipe->values[w] = false;
                new_conf.set_key_value("wipe", wipe);
            }
            else {
                new_conf.set_key_value("use_firmware_retraction", new ConfigOptionBool(false));
            }
            load_config(new_conf);
        }

        toggle_option("travel_lift_before_obstacle", ramping_lift, i);

        toggle_option("retract_length_toolchange", have_multiple_extruders, i);

        bool toolchange_retraction = m_config->opt_float("retract_length_toolchange", i) > 0;
        toggle_option("retract_restart_extra_toolchange", have_multiple_extruders && toolchange_retraction, i);
    }

    if (m_active_page->title() == "Machine limits" && m_machine_limits_description_line) {
        assert(flavor == gcfMarlinLegacy
            || flavor == gcfMarlinFirmware
            || flavor == gcfRepRapFirmware
            || flavor == gcfKlipper);
		const auto *machine_limits_usage = m_config->option<ConfigOptionEnum<MachineLimitsUsage>>("machine_limits_usage");
		bool enabled = machine_limits_usage->value != MachineLimitsUsage::Ignore;
        bool silent_mode = m_config->opt_bool("silent_mode");
        int  max_field = silent_mode ? 2 : 1;
    	for (const std::string &opt : Preset::machine_limits_options())
            for (int i = 0; i < max_field; ++ i)
	            toggle_option(opt, enabled, i);
        update_machine_limits_description(machine_limits_usage->value);
    }
}

void TabPrinter::update()
{
    m_update_cnt++;
    m_presets->get_edited_preset().printer_technology() == ptFFF ? update_fff() : update_sla();
    m_update_cnt--;

    update_description_lines();
    Layout();

    if (m_update_cnt == 0)
        wxGetApp().mainframe->on_config_changed(m_config);
}

void TabPrinter::update_fff()
{
    if (m_use_silent_mode != m_config->opt_bool("silent_mode"))	{
        m_rebuild_kinematics_page = true;
        m_use_silent_mode = m_config->opt_bool("silent_mode");
    }

    const auto flavor = m_config->option<ConfigOptionEnum<GCodeFlavor>>("gcode_flavor")->value;
    bool supports_travel_acceleration = (flavor == gcfMarlinFirmware || flavor == gcfRepRapFirmware);
    bool supports_min_feedrates       = (flavor == gcfMarlinFirmware || flavor == gcfMarlinLegacy);
    if (m_supports_travel_acceleration != supports_travel_acceleration || m_supports_min_feedrates != supports_min_feedrates) {
        m_rebuild_kinematics_page = true;
        m_supports_travel_acceleration = supports_travel_acceleration;
        m_supports_min_feedrates = supports_min_feedrates;
    }

    toggle_options();
}

bool Tab::is_prusa_printer() const
{
    const Preset& edited_preset = m_preset_bundle->printers.get_edited_preset();
    std::string  printer_model = edited_preset.trim_vendor_repo_prefix(edited_preset.config.opt_string("printer_model"));
    return SLAPrint::is_prusa_print(printer_model);
}

void TabPrinter::update_sla()
{
}

void Tab::update_ui_items_related_on_parent_preset(const Preset* selected_preset_parent)
{
    m_is_default_preset = selected_preset_parent != nullptr && selected_preset_parent->is_default;

    m_bmp_non_system = selected_preset_parent ? &m_bmp_value_unlock : &m_bmp_white_bullet;
    m_ttg_non_system = selected_preset_parent ? &m_ttg_value_unlock : &m_ttg_white_bullet_ns;
    m_tt_non_system  = selected_preset_parent ? &m_tt_value_unlock  : &m_ttg_white_bullet_ns;
}

// Initialize the UI from the current preset
void Tab::load_current_preset()
{
    const Preset& preset = m_presets->get_edited_preset();

    update_btns_enabling();

    update();
    if (m_type == Slic3r::Preset::TYPE_PRINTER) {
        // For the printer profile, generate the extruder pages.
        if (preset.printer_technology() == ptFFF)
            on_preset_loaded();
        else
            wxGetApp().sidebar().update_objects_list_extruder_column(1);
        // Check and show "Physical printer" page if needed
        wxGetApp().show_printer_webview_tab();
    }
    // Reload preset pages with the new configuration values.
    reload_config();

    update_ui_items_related_on_parent_preset(m_presets->get_selected_preset_parent());

//	m_undo_to_sys_btn->Enable(!preset.is_default);

    {
        // checking out if this Tab exists till this moment
        if (!wxGetApp().checked_tab(this))
            return;
        update_tab_ui();

        // update show/hide tabs
        if (m_type == Slic3r::Preset::TYPE_PRINTER) {
            const PrinterTechnology printer_technology = m_presets->get_edited_preset().printer_technology();
            if (printer_technology != static_cast<TabPrinter*>(this)->m_printer_technology)
            {
                // The change of the technology requires to remove some of unrelated Tabs
                // During this action, wxNoteBook::RemovePage invoke wxEVT_NOTEBOOK_PAGE_CHANGED
                // and as a result a function select_active_page() is called fron Tab::OnActive()
                // But we don't need it. So, to avoid activation of the page, set m_active_page to NULL 
                // till unusable Tabs will be deleted
                Page* tmp_page = m_active_page;
                m_active_page = nullptr;
                for (auto tab : wxGetApp().tabs_list) {
                    if (tab->type() == Preset::TYPE_PRINTER) {
                        // Printer tab is shown every time
                        continue;
                    }
                    if (tab->supports_printer_technology(printer_technology))
                    {
                        dynamic_cast<TopBar*>(wxGetApp().tab_panel())->InsertNewPage(wxGetApp().tab_panel()->FindPage(this), tab, tab->title(),"");
                    }
                    else {
                        int page_id = wxGetApp().tab_panel()->FindPage(tab);
                        wxGetApp().tab_panel()->GetPage(page_id)->Show(false);
                        wxGetApp().tab_panel()->RemovePage(page_id);
                    }
                }
                static_cast<TabPrinter*>(this)->m_printer_technology = printer_technology;
                m_active_page = tmp_page;
            }
            on_presets_changed();
            if (printer_technology == ptFFF) {
                static_cast<TabPrinter*>(this)->m_initial_extruders_count = static_cast<const ConfigOptionFloats*>(m_presets->get_selected_preset().config.option("nozzle_diameter"))->values.size(); //static_cast<TabPrinter*>(this)->m_extruders_count;
                const Preset* parent_preset = m_presets->get_selected_preset_parent();
                static_cast<TabPrinter*>(this)->m_sys_extruders_count = parent_preset == nullptr ? 0 :
                    static_cast<const ConfigOptionFloats*>(parent_preset->config.option("nozzle_diameter"))->values.size();
            }
        }
        else {
            on_presets_changed();
            if (m_type == Preset::TYPE_SLA_PRINT || m_type == Preset::TYPE_PRINT)
                update_frequently_changed_parameters();
        }

        m_opt_status_value = (m_presets->get_selected_preset_parent() ? osSystemValue : 0) | osInitValue;
        init_options_list();
        update_visibility();
        update_sla_prusa_specific_visibility();
        update_changed_ui();
    }
#if 0
    );
#endif
}

//Regerenerate content of the page tree.
void Tab::rebuild_page_tree()
{
    // get label of the currently selected item
    const auto sel_item = m_treectrl->GetSelection();
    const auto selected = sel_item ? m_treectrl->GetItemText(sel_item) : "";
    const auto rootItem = m_treectrl->GetRootItem();

    wxTreeItemId item;

    // Delete/Append events invoke wxEVT_TREE_SEL_CHANGED event.
    // To avoid redundant clear/activate functions call
    // suppress activate page before page_tree rebuilding
    m_disable_tree_sel_changed_event = true;
    m_treectrl->DeleteChildren(rootItem);

    for (auto p : m_pages)
    {
        if (!p->get_show())
            continue;
        auto itemId = m_treectrl->AppendItem(rootItem, translate_category(p->title(), m_type), p->iconID());
        m_treectrl->SetItemTextColour(itemId, p->get_item_colour());
        m_treectrl->SetItemFont(itemId, wxGetApp().normal_font());
        if (translate_category(p->title(), m_type) == selected)
            item = itemId;
    }
    if (!item) {
        // this is triggered on first load, so we don't disable the sel change event
        item = m_treectrl->GetFirstVisibleItem();
    }

    // allow activate page before selection of a page_tree item
    m_disable_tree_sel_changed_event = false;
    if (item)
        m_treectrl->SelectItem(item);
}

void Tab::update_btns_enabling()
{
    // we can delete any preset from the physical printer
    // and any user preset
    const Preset& preset = m_presets->get_edited_preset();
    const bool is_printer_and_selected_physical = m_type == Preset::TYPE_PRINTER && m_preset_bundle->physical_printers.has_selection();

    m_btn_delete_preset->Show(is_printer_and_selected_physical || (!preset.is_default && !preset.is_system));

    m_btn_rename_preset->Show(!is_printer_and_selected_physical && !preset.is_default && !preset.is_system && !preset.is_external);

    if (m_btn_edit_ph_printer)
        m_btn_edit_ph_printer->SetToolTip( m_preset_bundle->physical_printers.has_selection() ?
                                           _L("Edit physical printer") : _L("Add physical printer"));
    m_h_buttons_sizer->Layout();
}

void Tab::update_preset_choice()
{
    m_presets_choice->update();
    update_btns_enabling();
}

// Called by the UI combo box when the user switches profiles, and also to delete the current profile.
// Select a preset by a name.If !defined(name), then the default preset is selected.
// If the current profile is modified, user is asked to save the changes.
bool Tab::select_preset(std::string preset_name, bool delete_current /*=false*/, const std::string& last_selected_ph_printer_name/* =""*/)
{
    if (preset_name.empty()) {
        if (delete_current) {
            // Find an alternate preset to be selected after the current preset is deleted.
            const std::deque<Preset> &presets 		= m_presets->get_presets();
            size_t    				  idx_current   = m_presets->get_idx_selected();
            // Find the next visible preset.
            size_t 				      idx_new       = idx_current + 1;
            if (idx_new < presets.size())
                for (; idx_new < presets.size() && ! presets[idx_new].is_visible; ++ idx_new) ;
            if (idx_new == presets.size())
                for (idx_new = idx_current - 1; idx_new > 0 && ! presets[idx_new].is_visible; -- idx_new);
            preset_name = presets[idx_new].name;
        } else {
            // If no name is provided, select the "-- default --" preset.
            preset_name = m_presets->default_preset().name;
        }
    }
    assert(! delete_current || (m_presets->get_edited_preset().name != preset_name && m_presets->get_edited_preset().is_user()));
    bool current_dirty = ! delete_current && m_presets->current_is_dirty();
    bool print_tab     = m_presets->type() == Preset::TYPE_PRINT || m_presets->type() == Preset::TYPE_SLA_PRINT;
    bool printer_tab   = m_presets->type() == Preset::TYPE_PRINTER;
    bool canceled      = false;
    bool technology_changed = false;
    m_dependent_tabs.clear();
    if (current_dirty && ! may_discard_current_dirty_preset(nullptr, preset_name)) {
        canceled = true;
    } else if (print_tab) {
        // Before switching the print profile to a new one, verify, whether the currently active filament or SLA material
        // are compatible with the new print.
        // If it is not compatible and the current filament or SLA material are dirty, let user decide
        // whether to discard the changes or keep the current print selection.
        PresetWithVendorProfile printer_profile = m_preset_bundle->printers.get_edited_preset_with_vendor_profile();
        PrinterTechnology  printer_technology = printer_profile.preset.printer_technology();
        PresetCollection  &dependent = (printer_technology == ptFFF) ? m_preset_bundle->filaments : m_preset_bundle->sla_materials;
        bool 			   old_preset_dirty = dependent.current_is_dirty();
        bool 			   new_preset_compatible = is_compatible_with_print(dependent.get_edited_preset_with_vendor_profile(), 
        	m_presets->get_preset_with_vendor_profile(*m_presets->find_preset(preset_name, true)), printer_profile);
        if (! canceled)
            canceled = old_preset_dirty && ! new_preset_compatible && ! may_discard_current_dirty_preset(&dependent, preset_name);
        if (! canceled) {
            // The preset will be switched to a different, compatible preset, or the '-- default --'.
            m_dependent_tabs.emplace_back((printer_technology == ptFFF) ? Preset::Type::TYPE_FILAMENT : Preset::Type::TYPE_SLA_MATERIAL);
            if (old_preset_dirty && ! new_preset_compatible)
                dependent.discard_current_changes();
        }
    } else if (printer_tab) {
        // Before switching the printer to a new one, verify, whether the currently active print and filament
        // are compatible with the new printer.
        // If they are not compatible and the current print or filament are dirty, let user decide
        // whether to discard the changes or keep the current printer selection.
        //
        // With the introduction of the SLA printer types, we need to support switching between
        // the FFF and SLA printers.
        const Preset 		&new_printer_preset     = *m_presets->find_preset(preset_name, true);
		const PresetWithVendorProfile new_printer_preset_with_vendor_profile = m_presets->get_preset_with_vendor_profile(new_printer_preset);
        PrinterTechnology    old_printer_technology = m_presets->get_edited_preset().printer_technology();
        PrinterTechnology    new_printer_technology = new_printer_preset.printer_technology();
        if (new_printer_technology == ptSLA && old_printer_technology == ptFFF && !wxGetApp().may_switch_to_SLA_preset(_L("New printer preset selected")))
            canceled = true;
        else {
            struct PresetUpdate {
                Preset::Type         tab_type;
                PresetCollection 	*presets;
                PrinterTechnology    technology;
                bool    	         old_preset_dirty;
                bool         	     new_preset_compatible;
            };
            std::vector<PresetUpdate> updates = {
                { Preset::Type::TYPE_PRINT,         &m_preset_bundle->prints,       ptFFF },
                { Preset::Type::TYPE_SLA_PRINT,     &m_preset_bundle->sla_prints,   ptSLA },
                { Preset::Type::TYPE_FILAMENT,      &m_preset_bundle->filaments,    ptFFF },
                { Preset::Type::TYPE_SLA_MATERIAL,  &m_preset_bundle->sla_materials,ptSLA }
            };
            for (PresetUpdate &pu : updates) {
                pu.old_preset_dirty = (old_printer_technology == pu.technology) && pu.presets->current_is_dirty();
                pu.new_preset_compatible = (new_printer_technology == pu.technology) && is_compatible_with_printer(pu.presets->get_edited_preset_with_vendor_profile(), new_printer_preset_with_vendor_profile);
                bool force_update_edited_preset = false;
                if (pu.tab_type == Preset::TYPE_FILAMENT && pu.new_preset_compatible) {
                    // check if edited preset will be still correct after selection new printer 
                    const int active_extruder    = dynamic_cast<const TabFilament*>(wxGetApp().get_tab(Preset::TYPE_FILAMENT))->get_active_extruder();
                    const int extruder_count_new = int(dynamic_cast<const ConfigOptionFloats*>(new_printer_preset.config.option("nozzle_diameter"))->size());
                    // if active_extruder is bigger than extruders_count,
                    // then it means that edited filament preset will be changed and we have to check this changes
                    force_update_edited_preset = active_extruder >= extruder_count_new;
                }
                if (!canceled)
                    canceled = pu.old_preset_dirty && (!pu.new_preset_compatible || force_update_edited_preset) && !may_discard_current_dirty_preset(pu.presets, preset_name);
            }
            if (!canceled) {
                for (PresetUpdate &pu : updates) {
                    // The preset will be switched to a different, compatible preset, or the '-- default --'.
                    if (pu.technology == new_printer_technology)
                        m_dependent_tabs.emplace_back(pu.tab_type);
                    if (pu.old_preset_dirty && !pu.new_preset_compatible)
                        pu.presets->discard_current_changes();
                }
            }
        }
        if (! canceled)
        	technology_changed = old_printer_technology != new_printer_technology;
    }

    if (! canceled && delete_current) {
        // Delete the file and select some other reasonable preset.
        // It does not matter which preset will be made active as the preset will be re-selected from the preset_name variable.
        // The 'external' presets will only be removed from the preset list, their files will not be deleted.
        try {
            // cache previously selected names
            delete_current_preset();
        } catch (const std::exception & /* e */) {
            //FIXME add some error reporting!
            canceled = true;
        }
    }

    if (canceled) {
        if (m_type == Preset::TYPE_PRINTER) {
            if (!last_selected_ph_printer_name.empty() &&
                m_presets->get_edited_preset().name == PhysicalPrinter::get_preset_name(last_selected_ph_printer_name)) {
                // If preset selection was canceled and previously was selected physical printer, we should select it back
                m_preset_bundle->physical_printers.select_printer(last_selected_ph_printer_name);
            }
            else if (m_preset_bundle->physical_printers.has_selection()) {
                // If preset selection was canceled and physical printer was selected
                // we must disable selection marker for the physical printers
                m_preset_bundle->physical_printers.unselect_printer();
            }
        }

        // ! update preset combobox, to revert previously selection
        update_tab_ui();

        // Trigger the on_presets_changed event so that we also restore the previous value in the plater selector,
        // if this action was initiated from the plater.
        on_presets_changed();
    } else {
        if (current_dirty)
            m_presets->discard_current_changes();

        const bool is_selected = select_preset_by_name(preset_name, false) || delete_current;
        assert(m_presets->get_edited_preset().name == preset_name || ! is_selected);
        // Mark the print & filament enabled if they are compatible with the currently selected preset.
        // The following method should not discard changes of current print or filament presets on change of a printer profile,
        // if they are compatible with the current printer.
        auto update_compatible_type = [delete_current](bool technology_changed, bool on_page, bool show_incompatible_presets) {
        	return (delete_current || technology_changed) ? PresetSelectCompatibleType::Always :
        	       on_page                                ? PresetSelectCompatibleType::Never  :
        	       show_incompatible_presets              ? PresetSelectCompatibleType::OnlyIfWasCompatible : PresetSelectCompatibleType::Always;
        };
        if (current_dirty || delete_current || print_tab || printer_tab)
            m_preset_bundle->update_compatible(
            	update_compatible_type(technology_changed, print_tab,   (print_tab ? this : wxGetApp().get_tab(Preset::TYPE_PRINT))->m_show_incompatible_presets),
            	update_compatible_type(technology_changed, false, 		wxGetApp().get_tab(Preset::TYPE_FILAMENT)->m_show_incompatible_presets));
        // Initialize the UI from the current preset.
        if (printer_tab)
            static_cast<TabPrinter*>(this)->update_pages();

        if (! is_selected && printer_tab)
        {
            /* There is a case, when :
             * after Config Wizard applying we try to select previously selected preset, but
             * in a current configuration this one:
             *  1. doesn't exist now,
             *  2. have another printer_technology
             * So, it is necessary to update list of dependent tabs
             * to the corresponding printer_technology
             */
            const PrinterTechnology printer_technology = m_presets->get_edited_preset().printer_technology();
            if (printer_technology == ptFFF && m_dependent_tabs.front() != Preset::Type::TYPE_PRINT)
                m_dependent_tabs = { Preset::Type::TYPE_PRINT, Preset::Type::TYPE_FILAMENT };
            else if (printer_technology == ptSLA && m_dependent_tabs.front() != Preset::Type::TYPE_SLA_PRINT)
                m_dependent_tabs = { Preset::Type::TYPE_SLA_PRINT, Preset::Type::TYPE_SLA_MATERIAL };
        }

        // check if there is something in the cache to move to the new selected preset
        apply_config_from_cache();

        load_current_preset();
    }

    if (technology_changed)
        wxGetApp().mainframe->technology_changed();

    return !canceled;
}

// If the current preset is dirty, the user is asked whether the changes may be discarded.
// if the current preset was not dirty, or the user agreed to discard the changes, 1 is returned.
bool Tab::may_discard_current_dirty_preset(PresetCollection* presets /*= nullptr*/, const std::string& new_printer_name /*= ""*/)
{
    if (presets == nullptr) presets = m_presets;

    UnsavedChangesDialog dlg(m_type, presets, new_printer_name);
    if (wxGetApp().app_config->get("default_action_on_select_preset") == "none" && dlg.ShowModal() == wxID_CANCEL)
        return false;

    if (dlg.save_preset())  // save selected changes
    {
        const std::vector<std::string>& unselected_options = dlg.get_unselected_options(presets->type());
        const std::string& name = dlg.get_preset_name();

        if (m_type == presets->type()) // save changes for the current preset from this tab
        {
            // revert unselected options to the old values
            presets->get_edited_preset().config.apply_only(presets->get_selected_preset().config, unselected_options);
            save_preset(name);
        }
        else
        {
            m_preset_bundle->save_changes_for_preset(name, presets->type(), unselected_options);

            // If filament preset is saved for multi-material printer preset,
            // there are cases when filament comboboxs are updated for old (non-modified) colors,
            // but in full_config a filament_colors option aren't.
            if (presets->type() == Preset::TYPE_FILAMENT && wxGetApp().extruders_edited_cnt() > 1)
                wxGetApp().plater()->force_filament_colors_update();
        }
    }
    else if (dlg.transfer_changes()) // move selected changes
    {
        std::vector<std::string> selected_options = dlg.get_selected_options();
        if (m_type == presets->type()) // move changes for the current preset from this tab
        {
            if (m_type == Preset::TYPE_PRINTER) {
                auto it = std::find(selected_options.begin(), selected_options.end(), "extruders_count");
                if (it != selected_options.end()) {
                    // erase "extruders_count" option from the list
                    selected_options.erase(it);
                    // cache the extruders count
                    static_cast<TabPrinter*>(this)->cache_extruder_cnt();
                }
            }

            // copy selected options to the cache from edited preset
            cache_config_diff(selected_options);
        }
        else
            wxGetApp().get_tab(presets->type())->cache_config_diff(selected_options);
    }

    return true;
}

void Tab::clear_pages()
{
    // invalidated highlighter, if any exists
    m_highlighter.invalidate();
    m_page_sizer->Clear(true);
    // clear pages from the controlls
    for (auto p : m_pages)
        p->clear();

    // nulling pointers
    m_parent_preset_description_line = nullptr;
    m_detach_preset_btn = nullptr;

    m_compatible_printers.checkbox  = nullptr;
    m_compatible_printers.btn       = nullptr;

    m_compatible_prints.checkbox    = nullptr;
    m_compatible_prints.btn         = nullptr;
}

void Tab::update_description_lines()
{
    if (m_active_page && m_active_page->title() == "Dependencies" && m_parent_preset_description_line)
        update_preset_description_line();
}

void Tab::activate_selected_page(std::function<void()> throw_if_canceled)
{
    if (!m_active_page)
        return;

    m_active_page->activate(m_mode, throw_if_canceled);

    if (m_active_page->title() == "Dependencies") {
        if (m_compatible_printers.checkbox)
            this->compatible_widget_reload(m_compatible_printers);
        if (m_compatible_prints.checkbox)
            this->compatible_widget_reload(m_compatible_prints);
    }

    update_sla_prusa_specific_visibility();
    update_changed_ui();
    update_description_lines();
    toggle_options();
}

#ifdef WIN32
// Override the wxCheckForInterrupt to process inperruptions just from key or mouse
// and to avoid an unwanted early call of CallAfter()
static bool CheckForInterrupt(wxWindow* wnd)
{
    wxCHECK(wnd, false);

    MSG msg;
    while (::PeekMessage(&msg, ((HWND)((wnd)->GetHWND())), WM_KEYFIRST, WM_KEYLAST, PM_REMOVE))
    {
        ::TranslateMessage(&msg);
        ::DispatchMessage(&msg);
    }
    while (::PeekMessage(&msg, ((HWND)((wnd)->GetHWND())), WM_MOUSEFIRST, WM_MOUSELAST, PM_REMOVE))
    {
        ::TranslateMessage(&msg);
        ::DispatchMessage(&msg);
    }
    return true;
}
#endif //WIN32

bool Tab::tree_sel_change_delayed()
{
    // There is a bug related to Ubuntu overlay scrollbars, see https://github.com/prusa3d/PrusaSlicer/issues/898 and https://github.com/prusa3d/PrusaSlicer/issues/952.
    // The issue apparently manifests when Show()ing a window with overlay scrollbars while the UI is frozen. For this reason,
    // we will Thaw the UI prematurely on Linux. This means destroing the no_updates object prematurely.
#ifdef __linux__
    std::unique_ptr<wxWindowUpdateLocker> no_updates(new wxWindowUpdateLocker(this));
#else
    /* On Windows we use DoubleBuffering during rendering,
     * so on Window is no needed to call a Freeze/Thaw functions.
     * But under OSX (builds compiled with MacOSX10.14.sdk) wxStaticBitmap rendering is broken without Freeze/Thaw call.
     */
//#ifdef __WXOSX__  // Use Freeze/Thaw to avoid flickering during clear/activate new page
    wxWindowUpdateLocker noUpdates(this);
//#endif
#endif

    Page* page = nullptr;
    const auto sel_item = m_treectrl->GetSelection();
    const auto selection = sel_item ? m_treectrl->GetItemText(sel_item) : "";
    for (auto p : m_pages)
        if (translate_category(p->title(), m_type) == selection)
        {
            page = p.get();
            m_is_nonsys_values = page->m_is_nonsys_values;
            m_is_modified_values = page->m_is_modified_values;
            break;
        }
    if (page == nullptr || m_active_page == page)
        return false;

    // clear pages from the controls
    m_active_page = page;
    
    auto throw_if_canceled = std::function<void()>([this](){
#ifdef WIN32
            CheckForInterrupt(m_treectrl);
            if (m_page_switch_planned)
                throw UIBuildCanceled();
#else // WIN32
            (void)this; // silence warning
#endif
        });

    try {
        clear_pages();
        throw_if_canceled();

        if (wxGetApp().mainframe!=nullptr && wxGetApp().mainframe->is_active_and_shown_tab(this))
            activate_selected_page(throw_if_canceled);

        #ifdef __linux__
            no_updates.reset(nullptr);
        #endif

        update_undo_buttons();
        throw_if_canceled();

        m_hsizer->Layout();
        throw_if_canceled();
        Refresh();
    } catch (const UIBuildCanceled&) {
	    if (m_active_page)
		    m_active_page->clear();
        return true;
    }

    return false;
}

void Tab::OnKeyDown(wxKeyEvent& event)
{
    if (event.GetKeyCode() == WXK_TAB)
        m_treectrl->Navigate(event.ShiftDown() ? wxNavigationKeyEvent::IsBackward : wxNavigationKeyEvent::IsForward);
    else
        event.Skip();
}

void Tab::compare_preset()
{
    wxGetApp().mainframe->diff_dialog.show(m_type);
}

void Tab::transfer_options(const std::string &name_from, const std::string &name_to, std::vector<std::string> options)
{
    if (options.empty())
        return;

    Preset* preset_from = m_presets->find_preset(name_from);
    Preset* preset_to = m_presets->find_preset(name_to);

    if (m_type == Preset::TYPE_PRINTER) {
         auto it = std::find(options.begin(), options.end(), "extruders_count");
         if (it != options.end()) {
             // erase "extruders_count" option from the list
             options.erase(it);
             // cache the extruders count
             static_cast<TabPrinter*>(this)->cache_extruder_cnt(&preset_from->config);
         }
    }
    cache_config_diff(options, &preset_from->config);

    if (name_to != m_presets->get_edited_preset().name )
        select_preset(preset_to->name);

    apply_config_from_cache();
    load_current_preset();
}

// Save the current preset into file.
// This removes the "dirty" flag of the preset, possibly creates a new preset under a new name,
// and activates the new preset.
// Wizard calls save_preset with a name "My Settings", otherwise no name is provided and this method
// opens a Slic3r::GUI::SavePresetDialog dialog.
void Tab::save_preset(std::string name /*= ""*/, bool detach)
{
    // since buttons(and choices too) don't get focus on Mac, we set focus manually
    // to the treectrl so that the EVT_* events are fired for the input field having
    // focus currently.is there anything better than this ?
//!	m_treectrl->OnSetFocus();

    Preset& edited_preset = m_presets->get_edited_preset();
    bool from_template = false;
    std::string edited_printer;
    if (m_type == Preset::TYPE_FILAMENT && edited_preset.vendor && edited_preset.vendor->templates_profile) {
        edited_printer = wxGetApp().preset_bundle->printers.get_edited_preset().config.opt_string("printer_model");
        from_template = !edited_printer.empty();
    }

    if (name.empty()) {
        SavePresetDialog dlg(m_parent, { m_type }, detach ? _u8L("Detached") : "", from_template);
        if (dlg.ShowModal() != wxID_OK)
            return;
        name = dlg.get_name();
        if (from_template)
            from_template = dlg.get_template_filament_checkbox();
    }

    if (detach && m_type == Preset::TYPE_PRINTER)
        m_config->opt_string("printer_model", true) = "";

    // Update compatible printers
    if (from_template && !edited_printer.empty()) {
        std::string cond = edited_preset.compatible_printers_condition();
        if (!cond.empty())
            cond += " and ";
        cond += "printer_model == \"" + edited_printer + "\"";
        edited_preset.config.opt_string("compatible_printers_condition") = cond;
    }

    // Save the preset into Slic3r::data_dir / presets / section_name / preset_name.ini
    save_current_preset(name, detach);

    if (detach && m_type == Preset::TYPE_PRINTER)
        wxGetApp().mainframe->on_config_changed(m_config);


    // Mark the print & filament enabled if they are compatible with the currently selected preset.
    // If saving the preset changes compatibility with other presets, keep the now incompatible dependent presets selected, however with a "red flag" icon showing that they are no more compatible.
    m_preset_bundle->update_compatible(PresetSelectCompatibleType::Never);
    // Add the new item into the UI component, remove dirty flags and activate the saved item.
    update_tab_ui();
    // Update the selection boxes at the plater.
    on_presets_changed();
    // If current profile is saved, "delete/rename preset" buttons have to be shown
    m_btn_delete_preset->Show();
    m_btn_rename_preset->Show(!m_presets_choice->is_selected_physical_printer());
    m_btn_delete_preset->GetParent()->Layout();

    if (m_type == Preset::TYPE_PRINTER)
        static_cast<TabPrinter*>(this)->m_initial_extruders_count = static_cast<TabPrinter*>(this)->m_extruders_count;

    // Parent preset is "default" after detaching, so we should to update UI values, related on parent preset  
    if (detach)
        update_ui_items_related_on_parent_preset(m_presets->get_selected_preset_parent());

    update_changed_ui();

    /* If filament preset is saved for multi-material printer preset, 
     * there are cases when filament comboboxs are updated for old (non-modified) colors, 
     * but in full_config a filament_colors option aren't.*/
    if (m_type == Preset::TYPE_FILAMENT && wxGetApp().extruders_edited_cnt() > 1)
        wxGetApp().plater()->force_filament_colors_update();

    {
        // Profile compatiblity is updated first when the profile is saved.
        // Update profile selection combo boxes at the depending tabs to reflect modifications in profile compatibility.
        std::vector<Preset::Type> dependent;
        switch (m_type) {
        case Preset::TYPE_PRINT:
            dependent = { Preset::TYPE_FILAMENT };
            break;
        case Preset::TYPE_SLA_PRINT:
            dependent = { Preset::TYPE_SLA_MATERIAL };
            break;
        case Preset::TYPE_PRINTER:
            if (static_cast<const TabPrinter*>(this)->m_printer_technology == ptFFF)
                dependent = { Preset::TYPE_PRINT, Preset::TYPE_FILAMENT };
            else
                dependent = { Preset::TYPE_SLA_PRINT, Preset::TYPE_SLA_MATERIAL };
            break;
        default:
            break;
        }
        for (Preset::Type preset_type : dependent)
            wxGetApp().get_tab(preset_type)->update_tab_ui();
    }

    // update preset comboboxes in DiffPresetDlg
    wxGetApp().mainframe->diff_dialog.update_presets(m_type);

    if (detach)
        update_description_lines();
}

void Tab::rename_preset()
{
    if (m_presets_choice->is_selected_physical_printer())
        return;

    wxString msg;

    if (m_type == Preset::TYPE_PRINTER && !m_preset_bundle->physical_printers.empty()) {
        // Check preset for rename in physical printers
        std::vector<std::string> ph_printers = m_preset_bundle->physical_printers.get_printers_with_preset(m_presets->get_selected_preset().name);
        if (!ph_printers.empty()) {
            msg += _L_PLURAL("The physical printer below is based on the preset, you are going to rename.",
                "The physical printers below are based on the preset, you are going to rename.", ph_printers.size());
            for (const std::string& printer : ph_printers)
                msg += "\n    \"" + from_u8(printer) + "\",";
            msg.RemoveLast();
            msg += "\n" + _L_PLURAL("Note, that the selected preset will be renamed in this printer too.",
                "Note, that the selected preset will be renamed in these printers too.", ph_printers.size()) + "\n\n";
        }
    }

    // get new name

    SavePresetDialog dlg(m_parent, m_type, msg);
    if (dlg.ShowModal() != wxID_OK)
        return;

    const std::string new_name = dlg.get_name();
    if (new_name.empty() || new_name == m_presets->get_selected_preset().name)
        return;

    // Note: selected preset can be changed, if in SavePresetDialog was selected name of existing preset
    Preset& selected_preset = m_presets->get_selected_preset();
    Preset& edited_preset   = m_presets->get_edited_preset();

    const std::string old_name      = selected_preset.name;
    const std::string old_file_name = selected_preset.file;

    assert(old_name == edited_preset.name);

    if (m_type == Preset::TYPE_FILAMENT) {
        // Filaments will be sorted inside collection after remaning,
        // so, cache preset names for each extruder to reset them after renaming
        m_preset_bundle->cache_extruder_filaments_names();
    }

    bool was_renamed = true;
    using namespace boost;
    try {
        // rename selected and edited presets

        selected_preset.name = new_name;
        replace_last(selected_preset.file, old_name, new_name);

        edited_preset.name = new_name;
        replace_last(edited_preset.file, old_name, new_name);

        // rename file with renamed preset configuration

        filesystem::rename(old_file_name, selected_preset.file);

        // rename selected preset in printers, if it's needed

        if (!msg.IsEmpty())
            m_preset_bundle->physical_printers.rename_preset_in_printers(old_name, new_name);
    }
    catch (const exception& ex) {
        const std::string exception = diagnostic_information(ex);
        printf("Can't rename a preset : %s", exception.c_str());
        was_renamed = false;
    }

    // sort presets after renaming
    std::sort(m_presets->begin(), m_presets->end());

    if (was_renamed && m_type == Preset::TYPE_FILAMENT) {
        // Reset extruder_filaments only if preset was renamed
        m_preset_bundle->reset_extruder_filaments();
        // and update compatibility for extruders after reset
        m_preset_bundle->update_filaments_compatible(PresetSelectCompatibleType::OnlyIfWasCompatible);
    }
    // update selection
    select_preset_by_name(new_name, true);

    m_presets_choice->update();
    on_presets_changed();
}

// Called for a currently selected preset.
void Tab::delete_preset()
{
    auto current_preset = m_presets->get_selected_preset();
    // Don't let the user delete the ' - default - ' configuration.
    wxString action = current_preset.is_external ? _L("remove") : _L("delete");

    PhysicalPrinterCollection& physical_printers = m_preset_bundle->physical_printers;
    wxString msg;
    if (m_presets_choice->is_selected_physical_printer())
    {
        PhysicalPrinter& printer = physical_printers.get_selected_printer();
        if (printer.preset_names.size() == 1) {
            if (m_presets_choice->del_physical_printer(_L("It's a last preset for this physical printer."))) {
                // Hide "Physical printer" page
                wxGetApp().show_printer_webview_tab();
                Layout();
            }
            return;
        }
        
        msg = format_wxstr(_L("Are you sure you want to delete \"%1%\" preset from the physical printer \"%2%\"?"), current_preset.name, printer.name);
    }
    else
    {
        if (m_type == Preset::TYPE_PRINTER && !physical_printers.empty())
        {
            // Check preset for delete in physical printers
            // Ask a customer about next action, if there is a printer with just one preset and this preset is equal to delete
            std::vector<std::string> ph_printers        = physical_printers.get_printers_with_preset(current_preset.name, false);
            std::vector<std::string> ph_printers_only   = physical_printers.get_printers_with_only_preset(current_preset.name);

            if (!ph_printers.empty()) {
                msg += _L_PLURAL("The physical printer below is based on the preset, you are going to delete.", 
                                 "The physical printers below are based on the preset, you are going to delete.", ph_printers.size());
                for (const std::string& printer : ph_printers)
                    msg += "\n    \"" + from_u8(printer) + "\",";
                msg.RemoveLast();
                msg += "\n" + _L_PLURAL("Note, that the selected preset will be deleted from this printer too.", 
                                        "Note, that the selected preset will be deleted from these printers too.", ph_printers.size()) + "\n\n";
            }

            if (!ph_printers_only.empty()) {
                msg += _L_PLURAL("The physical printer below is based only on the preset, you are going to delete.", 
                                 "The physical printers below are based only on the preset, you are going to delete.", ph_printers_only.size());
                for (const std::string& printer : ph_printers_only)
                    msg += "\n    \"" + from_u8(printer) + "\",";
                msg.RemoveLast();
                msg += "\n" + _L_PLURAL("Note, that this printer will be deleted after deleting the selected preset.",
                                        "Note, that these printers will be deleted after deleting the selected preset.", ph_printers_only.size()) + "\n\n";
            }
        }

        // TRN "remove/delete"
        msg += from_u8((boost::format(_u8L("Are you sure you want to %1% the selected preset?")) % action).str());
    }

    action = current_preset.is_external ? _L("Remove") : _L("Delete");
    // TRN Settings Tabs: Button in toolbar: "Remove/Delete"
    wxString title = format_wxstr(_L("%1% Preset"), action);
    if (current_preset.is_default ||
        //wxID_YES != wxMessageDialog(parent(), msg, title, wxYES_NO | wxNO_DEFAULT | wxICON_QUESTION).ShowModal())
        wxID_YES != MessageDialog(parent(), msg, title, wxYES_NO | wxNO_DEFAULT | wxICON_QUESTION).ShowModal())
        return;

    // if we just delete preset from the physical printer
    if (m_presets_choice->is_selected_physical_printer()) {
        PhysicalPrinter& printer = physical_printers.get_selected_printer();

        // just delete this preset from the current physical printer
        printer.delete_preset(m_presets->get_edited_preset().name);
        // select first from the possible presets for this printer
        physical_printers.select_printer(printer);

        this->select_preset(physical_printers.get_selected_printer_preset_name());
        return;
    }

    // delete selected preset from printers and printer, if it's needed
    if (m_type == Preset::TYPE_PRINTER && !physical_printers.empty())
        physical_printers.delete_preset_from_printers(current_preset.name);

    // Select will handle of the preset dependencies, of saving & closing the depending profiles, and
    // finally of deleting the preset.
    this->select_preset("", true);
}

void Tab::toggle_show_hide_incompatible()
{
    m_show_incompatible_presets = !m_show_incompatible_presets;
    update_compatibility_ui();
}

void Tab::update_compatibility_ui()
{
    m_btn_hide_incompatible_presets->SetBitmap(*get_bmp_bundle(m_show_incompatible_presets ? "flag_red" : "flag_green"));
    m_btn_hide_incompatible_presets->SetToolTip(m_show_incompatible_presets ?
        "Both compatible an incompatible presets are shown. Click to hide presets not compatible with the current printer." :
        "Only compatible presets are shown. Click to show both the presets compatible and not compatible with the current printer.");

    m_presets_choice->set_show_incompatible_presets(m_show_incompatible_presets);
    m_presets_choice->update();
}

void Tab::update_ui_from_settings()
{
    // Show the 'show / hide presets' button only for the print and filament tabs
    if (m_type == Slic3r::Preset::TYPE_PRINTER)
        return;

    // and only if enabled in application preferences.
    bool show = wxGetApp().app_config->get_bool("show_incompatible_presets");
    if (m_show_btn_incompatible_presets == show)
        return;

    m_show_btn_incompatible_presets = show;
    m_btn_hide_incompatible_presets->Show(m_show_btn_incompatible_presets);
    Layout();
    
    if (show)
        update_compatibility_ui();
    else {
        m_presets_choice->set_show_incompatible_presets(false);
        m_presets_choice->update();
    }
}

void Tab::create_line_with_widget(ConfigOptionsGroup* optgroup, const std::string& opt_key, const std::string& path, widget_t widget)
{
    Line line = optgroup->create_single_option_line(opt_key);
    line.widget = widget;
    line.label_path = path;

    // set default undo ui
    line.set_undo_bitmap(&m_bmp_white_bullet);
    line.set_undo_to_sys_bitmap(&m_bmp_white_bullet);
    line.set_undo_tooltip(&m_tt_white_bullet);
    line.set_undo_to_sys_tooltip(&m_tt_white_bullet);
    line.set_label_colour(&m_default_text_clr);

    optgroup->append_line(line);
}

// Return a callback to create a Tab widget to mark the preferences as compatible / incompatible to the current printer.
wxSizer* Tab::compatible_widget_create(wxWindow* parent, PresetDependencies &deps)
{
    deps.checkbox = CheckBox::GetNewWin(parent, _L("All"));
    deps.checkbox->SetFont(Slic3r::GUI::wxGetApp().normal_font());
    wxGetApp().UpdateDarkUI(deps.checkbox);
    deps.btn = new ScalableButton(parent, wxID_ANY, "printer", format_wxstr(" %s %s", _L("Set"), dots),
                                  wxDefaultSize, wxDefaultPosition, wxBU_LEFT | wxBU_EXACTFIT);
    deps.btn->SetFont(Slic3r::GUI::wxGetApp().normal_font());
    deps.btn->SetSize(deps.btn->GetBestSize());

    auto sizer = new wxBoxSizer(wxHORIZONTAL);
    sizer->Add((deps.checkbox), 0, wxALIGN_CENTER_VERTICAL);
    sizer->Add((deps.btn), 0, wxALIGN_CENTER_VERTICAL);

    deps.checkbox->Bind(wxEVT_CHECKBOX, ([this, &deps](wxCommandEvent e)
    {
        const bool is_checked = CheckBox::GetValue(deps.checkbox);
        deps.btn->Enable(!is_checked);
        // All printers have been made compatible with this preset.
        if (is_checked)
            this->load_key_value(deps.key_list, std::vector<std::string> {});
        this->get_field(deps.key_condition)->toggle(is_checked);
        this->update_changed_ui();
    }) );

    deps.btn->Bind(wxEVT_BUTTON, ([this, parent, &deps](wxCommandEvent e)
    {
        // Collect names of non-default non-external profiles.
        PrinterTechnology printer_technology = m_preset_bundle->printers.get_edited_preset().printer_technology();
        PresetCollection &depending_presets  = (deps.type == Preset::TYPE_PRINTER) ? m_preset_bundle->printers :
                (printer_technology == ptFFF) ? m_preset_bundle->prints : m_preset_bundle->sla_prints;
        wxArrayString presets;
        for (size_t idx = 0; idx < depending_presets.size(); ++ idx)
        {
            Preset& preset = depending_presets.preset(idx);
            bool add = ! preset.is_default && ! preset.is_external;
            if (add && deps.type == Preset::TYPE_PRINTER)
                // Only add printers with the same technology as the active printer.
                add &= preset.printer_technology() == printer_technology;
            if (add)
                presets.Add(from_u8(preset.name));
        }

        wxMultiChoiceDialog dlg(parent, deps.dialog_title, deps.dialog_label, presets);
        wxGetApp().UpdateDlgDarkUI(&dlg);
        // Collect and set indices of depending_presets marked as compatible.
        wxArrayInt selections;
        auto *compatible_printers = dynamic_cast<const ConfigOptionStrings*>(m_config->option(deps.key_list));
        if (compatible_printers != nullptr || !compatible_printers->values.empty())
            for (auto preset_name : compatible_printers->values)
                for (size_t idx = 0; idx < presets.GetCount(); ++idx)
                    if (presets[idx] == preset_name) {
                        selections.Add(idx);
                        break;
                    }
        dlg.SetSelections(selections);
        std::vector<std::string> value;
        // Show the dialog.
        if (dlg.ShowModal() == wxID_OK) {
            selections.Clear();
            selections = dlg.GetSelections();
            for (auto idx : selections)
                value.push_back(presets[idx].ToUTF8().data());
            if (value.empty()) {
                CheckBox::SetValue(deps.checkbox, true);
                deps.btn->Disable();
            }
            // All depending_presets have been made compatible with this preset.
            this->load_key_value(deps.key_list, value);
            this->update_changed_ui();
        }
    }));

    return sizer;
}

// G-code substitutions

void SubstitutionManager::init(DynamicPrintConfig* config, wxWindow* parent, wxFlexGridSizer* grid_sizer)
{
    m_config = config;
    m_parent = parent;
    m_grid_sizer = grid_sizer;
    m_em = em_unit(parent);

    m_substitutions = m_config->option<ConfigOptionStrings>("gcode_substitutions")->values;
    m_chb_match_single_lines.clear();
}

void SubstitutionManager::validate_length()
{
    if ((m_substitutions.size() % 4) != 0) {
        WarningDialog(m_parent, "Value of gcode_substitutions parameter will be cut to valid length",
                                "Invalid length of gcode_substitutions parameter").ShowModal();
        m_substitutions.resize(m_substitutions.size() - (m_substitutions.size() % 4));
        // save changes from m_substitutions to config 
        m_config->option<ConfigOptionStrings>("gcode_substitutions")->values = m_substitutions;
    }
}

bool SubstitutionManager::is_compatible_with_ui()
{
    if (int(m_substitutions.size() / 4) != m_grid_sizer->GetEffectiveRowsCount() - 1) {
        ErrorDialog(m_parent, "Invalid compatibility between UI and BE", false).ShowModal();
        return false;
    }
    return true;
};

bool SubstitutionManager::is_valid_id(int substitution_id, const wxString& message)
{
    if (int(m_substitutions.size() / 4) < substitution_id) {
        ErrorDialog(m_parent, message, false).ShowModal();
        return false;
    }
    return true;
}

void SubstitutionManager::create_legend()
{
    if (!m_grid_sizer->IsEmpty())
        return;
    // name of the first column is empty
    m_grid_sizer->Add(new wxStaticText(m_parent, wxID_ANY, wxEmptyString));

    // Legend for another columns
    auto legend_sizer = new wxBoxSizer(wxHORIZONTAL); // "Find", "Replace", "Notes"
    legend_sizer->Add(new wxStaticText(m_parent, wxID_ANY, _L("Find")),         3, wxEXPAND);
    legend_sizer->Add(new wxStaticText(m_parent, wxID_ANY, _L("Replace with")), 3, wxEXPAND);
    legend_sizer->Add(new wxStaticText(m_parent, wxID_ANY, _L("Notes")),      2, wxEXPAND);

    m_grid_sizer->Add(legend_sizer, 1, wxEXPAND);
}

// delete substitution_id from substitutions
void SubstitutionManager::delete_substitution(int substitution_id)
{
    validate_length();
    if (!is_valid_id(substitution_id, "Invalid substitution_id to delete"))
        return;

    // delete substitution
    std::vector<std::string>& substitutions = m_config->option<ConfigOptionStrings>("gcode_substitutions")->values;
    substitutions.erase(std::next(substitutions.begin(), substitution_id * 4), std::next(substitutions.begin(), substitution_id * 4 + 4));

    call_ui_update();

    // update grid_sizer
    update_from_config();
}

// Add substitution line
void SubstitutionManager::add_substitution( int substitution_id, 
                                            const std::string& plain_pattern, 
                                            const std::string& format, 
                                            const std::string& params,
                                            const std::string& notes)
{
    bool call_after_layout = false;
    
    if (substitution_id < 0) {
        if (m_grid_sizer->IsEmpty()) {
            create_legend();
        }
        substitution_id = m_grid_sizer->GetEffectiveRowsCount() - 1;

        // create new substitution
        // it have to be added to config too
        for (size_t i = 0; i < 4; i ++)
            m_substitutions.push_back(std::string());

        // save changes from config to m_substitutions
        m_config->option<ConfigOptionStrings>("gcode_substitutions")->values = m_substitutions;

        call_after_layout = true;
    }

    auto del_btn = new ScalableButton(m_parent, wxID_ANY, "cross");
    del_btn->Bind(wxEVT_BUTTON, [substitution_id, this](wxEvent&) {
        delete_substitution(substitution_id);
    });

    m_grid_sizer->Add(del_btn, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT | wxLEFT, int(0.5*m_em));

    auto top_sizer = new wxBoxSizer(wxHORIZONTAL);
    auto add_text_editor = [substitution_id, top_sizer, this](const wxString& value, int opt_pos, int proportion) {
        auto editor = new ::TextInput(m_parent, value, "", "", wxDefaultPosition, wxSize(15 * m_em, wxDefaultCoord), wxTE_PROCESS_ENTER);

        editor->SetFont(wxGetApp().normal_font());
        wxGetApp().UpdateDarkUI(editor);
        top_sizer->Add(editor, proportion, wxALIGN_CENTER_VERTICAL | wxRIGHT, m_em);

        editor->Bind(wxEVT_TEXT_ENTER, [this, editor, substitution_id, opt_pos](wxEvent& e) {
#if !defined(__WXGTK__)
            e.Skip();
#endif // __WXGTK__
            edit_substitution(substitution_id, opt_pos, into_u8(editor->GetValue()));
        });

        editor->Bind(wxEVT_KILL_FOCUS, [this, editor, substitution_id, opt_pos](wxEvent& e) {
            e.Skip();
            edit_substitution(substitution_id, opt_pos, into_u8(editor->GetValue()));
        });
    };

    add_text_editor(from_u8(plain_pattern), 0, 3);
    add_text_editor(from_u8(format),        1, 3);
    add_text_editor(from_u8(notes),         3, 2);

    auto params_sizer = new wxBoxSizer(wxHORIZONTAL);
    bool regexp              = strchr(params.c_str(), 'r') != nullptr || strchr(params.c_str(), 'R') != nullptr;
    bool case_insensitive    = strchr(params.c_str(), 'i') != nullptr || strchr(params.c_str(), 'I') != nullptr;
    bool whole_word          = strchr(params.c_str(), 'w') != nullptr || strchr(params.c_str(), 'W') != nullptr;
    bool match_single_line   = strchr(params.c_str(), 's') != nullptr || strchr(params.c_str(), 'S') != nullptr;

    auto chb_regexp = CheckBox::GetNewWin(m_parent, _L("Regular expression"));
    CheckBox::SetValue(chb_regexp, regexp);
    params_sizer->Add(chb_regexp, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, m_em);

    auto chb_case_insensitive = CheckBox::GetNewWin(m_parent, _L("Case insensitive"));
    CheckBox::SetValue(chb_case_insensitive, case_insensitive);
    params_sizer->Add(chb_case_insensitive, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT | wxLEFT, m_em);

    auto chb_whole_word = CheckBox::GetNewWin(m_parent, _L("Whole word"));
    CheckBox::SetValue(chb_whole_word, whole_word);
    params_sizer->Add(chb_whole_word, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT | wxLEFT, m_em);

    auto chb_match_single_line = CheckBox::GetNewWin(m_parent, _L("Match single line"));
    CheckBox::SetValue(chb_match_single_line, match_single_line);
    chb_match_single_line->Show(regexp);
    m_chb_match_single_lines.emplace_back(chb_match_single_line);

    params_sizer->Add(chb_match_single_line, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT | wxLEFT, m_em);

    for (wxWindow* chb : std::initializer_list<wxWindow*>{ chb_regexp, chb_case_insensitive, chb_whole_word, chb_match_single_line }) {
        chb->SetFont(wxGetApp().normal_font());
        chb->Bind(wxEVT_CHECKBOX, [this, substitution_id, chb_regexp, chb_case_insensitive, chb_whole_word, chb_match_single_line](wxCommandEvent e) {
            std::string value = std::string();
            if (CheckBox::GetValue(chb_regexp))
                value += "r";
            if (CheckBox::GetValue(chb_case_insensitive))
                value += "i";
            if (CheckBox::GetValue(chb_whole_word))
                value += "w";
            if (CheckBox::GetValue(chb_match_single_line))
                value += "s";

            chb_match_single_line->Show(CheckBox::GetValue(chb_regexp));
            m_grid_sizer->Layout();

            edit_substitution(substitution_id, 2, value);
        });
    }

    auto v_sizer = new wxBoxSizer(wxVERTICAL);
    v_sizer->Add(top_sizer, 1, wxEXPAND);
    v_sizer->Add(params_sizer, 1, wxEXPAND|wxTOP|wxBOTTOM, int(0.5* m_em));
    m_grid_sizer->Add(v_sizer, 1, wxEXPAND);

    if (call_after_layout) {
        m_parent->GetParent()->Layout();
        call_ui_update();
    }
}

void SubstitutionManager::update_from_config()
{
    std::vector<std::string>& subst = m_config->option<ConfigOptionStrings>("gcode_substitutions")->values;
    if (m_substitutions == subst && m_grid_sizer->IsShown(1)) {
        // just update visibility for chb_match_single_lines
        int subst_id = 0;
        assert(m_chb_match_single_lines.size() == size_t(subst.size()/4));
        for (size_t i = 0; i < subst.size(); i += 4) {
            const std::string& params = subst[i + 2];
            const bool         regexp = strchr(params.c_str(), 'r') != nullptr || strchr(params.c_str(), 'R') != nullptr;
            m_chb_match_single_lines[subst_id++]->Show(regexp);
        }

        // "gcode_substitutions" values didn't changed in config. There is no need to update/recreate controls
        return;
    }

    m_substitutions = subst;

    if (!m_grid_sizer->IsEmpty()) {
        m_grid_sizer->Clear(true);
        m_chb_match_single_lines.clear();
    }

    if (subst.empty())
        hide_delete_all_btn();
    else
        create_legend();

    validate_length();

    int subst_id = 0;
    for (size_t i = 0; i < subst.size(); i += 4)
        add_substitution(subst_id++, subst[i], subst[i + 1], subst[i + 2], subst[i + 3]);

    m_parent->GetParent()->Layout();
}

void SubstitutionManager::delete_all()
{
    m_substitutions.clear();
    m_config->option<ConfigOptionStrings>("gcode_substitutions")->values.clear();
    call_ui_update();

    if (!m_grid_sizer->IsEmpty()) {
        m_grid_sizer->Clear(true);
        m_chb_match_single_lines.clear();
    }

    m_parent->GetParent()->Layout();
}

void SubstitutionManager::edit_substitution(int substitution_id, int opt_pos, const std::string& value)
{
    validate_length();
    if(!is_compatible_with_ui() || !is_valid_id(substitution_id, "Invalid substitution_id to edit"))
        return;

    m_substitutions[substitution_id * 4 + opt_pos] = value;
    // save changes from m_substitutions to config 
    m_config->option<ConfigOptionStrings>("gcode_substitutions")->values = m_substitutions;

    call_ui_update();
}

bool SubstitutionManager::is_empty_substitutions()
{
    return m_config->option<ConfigOptionStrings>("gcode_substitutions")->values.empty();
}

// Return a callback to create a TabPrint widget to edit G-code substitutions
wxSizer* TabPrint::create_manage_substitution_widget(wxWindow* parent)
{
    auto create_btn = [parent](ScalableButton** btn, const wxString& label, const std::string& icon_name) {
        *btn = new ScalableButton(parent, wxID_ANY, icon_name, " " + label + " ", wxDefaultSize, wxDefaultPosition, wxBU_LEFT | wxBU_EXACTFIT);
        (*btn)->SetFont(wxGetApp().normal_font());
        (*btn)->SetSize((*btn)->GetBestSize());
    };

    ScalableButton* add_substitution_btn;
    create_btn(&add_substitution_btn, _L("Add"), "add_copies");
    add_substitution_btn->Bind(wxEVT_BUTTON, [this](wxCommandEvent e) {
        m_subst_manager.add_substitution();
        m_del_all_substitutions_btn->Show();
    });

    create_btn(&m_del_all_substitutions_btn, _L("Delete all"), "cross");
    m_del_all_substitutions_btn->Bind(wxEVT_BUTTON, [this, parent](wxCommandEvent e) {
        if (MessageDialog(parent, _L("Are you sure you want to delete all substitutions?"), SLIC3R_APP_NAME, wxYES_NO | wxCANCEL | wxICON_QUESTION).
            ShowModal() != wxID_YES)
            return;
        m_subst_manager.delete_all();
        m_del_all_substitutions_btn->Hide();
    });

    auto sizer = new wxBoxSizer(wxHORIZONTAL);
    sizer->Add(add_substitution_btn,        0, wxALIGN_CENTER_VERTICAL | wxRIGHT | wxLEFT, em_unit(parent));
    sizer->Add(m_del_all_substitutions_btn, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT | wxLEFT, em_unit(parent));

    parent->GetParent()->Layout();
    return sizer;
}

// Return a callback to create a TabPrint widget to edit G-code substitutions
wxSizer* TabPrint::create_substitutions_widget(wxWindow* parent)
{
    wxFlexGridSizer* grid_sizer = new wxFlexGridSizer(2, 5, wxGetApp().em_unit()); // delete_button,  edit column contains "Find", "Replace", "Notes"
    grid_sizer->SetFlexibleDirection(wxBOTH);
    grid_sizer->AddGrowableCol(1);

    m_subst_manager.init(m_config, parent, grid_sizer);
    m_subst_manager.set_cb_edited_substitution([this]() {
        update_dirty();
        Layout();
        wxGetApp().mainframe->on_config_changed(m_config); // invalidate print
    });
    m_subst_manager.set_cb_hide_delete_all_btn([this]() {
        m_del_all_substitutions_btn->Hide();
    });

    parent->GetParent()->Layout();
    return grid_sizer;
}

// Return a callback to create a TabPrinter widget to edit bed shape
wxSizer* TabPrinter::create_bed_shape_widget(wxWindow* parent)
{
    ScalableButton* btn = new ScalableButton(parent, wxID_ANY, "printer", " " + _(L("Set")) + " " + dots,
        wxDefaultSize, wxDefaultPosition, wxBU_LEFT | wxBU_EXACTFIT);
    btn->SetFont(wxGetApp().normal_font());
    btn->SetSize(btn->GetBestSize());

    auto sizer = new wxBoxSizer(wxHORIZONTAL);
    sizer->Add(btn, 0, wxALIGN_CENTER_VERTICAL);

    btn->Bind(wxEVT_BUTTON, ([this](wxCommandEvent e)
        {
            BedShapeDialog dlg(this);
            dlg.build_dialog(*m_config->option<ConfigOptionPoints>("bed_shape"),
                *m_config->option<ConfigOptionString>("bed_custom_texture"),
                *m_config->option<ConfigOptionString>("bed_custom_model"));
            if (dlg.ShowModal() == wxID_OK) {
                const std::vector<Vec2d>& shape = dlg.get_shape();
                const std::string& custom_texture = dlg.get_custom_texture();
                const std::string& custom_model = dlg.get_custom_model();
                if (!shape.empty())
                {
                    load_key_value("bed_shape", shape);
                    load_key_value("bed_custom_texture", custom_texture);
                    load_key_value("bed_custom_model", custom_model);
                    update_changed_ui();
                }
            }
        }));

    // may be it is not a best place, but 
    // add information about Category/Grope for "bed_custom_texture" and "bed_custom_model" as a copy from "bed_shape" option
    {
        Search::OptionsSearcher& searcher = wxGetApp().searcher();
        const Search::GroupAndCategory& gc = searcher.get_group_and_category("bed_shape");
        searcher.add_key("bed_custom_texture", m_type, gc.group, gc.category);
        searcher.add_key("bed_custom_model", m_type, gc.group, gc.category);
    }

    return sizer;
}

void TabPrinter::cache_extruder_cnt(const DynamicPrintConfig* config/* = nullptr*/)
{
    const DynamicPrintConfig& cached_config = config ? *config : m_presets->get_edited_preset().config;
    if (Preset::printer_technology(cached_config) == ptSLA)
        return;

    // get extruders count 
    auto* nozzle_diameter = dynamic_cast<const ConfigOptionFloats*>(cached_config.option("nozzle_diameter"));
    m_cache_extruder_count = nozzle_diameter->values.size(); //m_extruders_count;
}

bool TabPrinter::apply_extruder_cnt_from_cache()
{
    if (m_presets->get_edited_preset().printer_technology() == ptSLA)
        return false;

    if (m_cache_extruder_count > 0) {
        m_presets->get_edited_preset().set_num_extruders(m_cache_extruder_count);
//        extruders_count_changed(m_cache_extruder_count);
        m_cache_extruder_count = 0;
        return true;
    }
    return false;
}

void TabPrinter::update_sla_prusa_specific_visibility()
{
    if (m_active_page && m_active_page->title() == "General") {
        auto og_it = std::find_if(m_active_page->m_optgroups.begin(), m_active_page->m_optgroups.end(), [](const ConfigOptionsGroupShp og) { return og->title == "Tilt"; });
        if (og_it != m_active_page->m_optgroups.end()) {            
            og_it->get()->Show(m_mode == comExpert && !is_prusa_printer());
            Layout();
        }
    }
}

bool Tab::validate_custom_gcodes()
{
    if (m_type != Preset::TYPE_FILAMENT &&
        (m_type != Preset::TYPE_PRINTER || static_cast<TabPrinter*>(this)->m_printer_technology != ptFFF))
        return true;
    if (m_active_page->title() != L("Custom G-code"))
        return true;

    // When we switch Settings tab after editing of the custom g-code, then warning message could ba already shown after KillFocus event
    // and then it's no need to show it again
    if (validate_custom_gcodes_was_shown) {
        validate_custom_gcodes_was_shown = false;
        return true;
    }

    bool valid = true;
    for (auto opt_group : m_active_page->m_optgroups) {
        assert(opt_group->opt_map().size() == 1);
        if (!opt_group->is_activated())
            break;
        std::string key = opt_group->opt_map().begin()->first;
        if (key == "autoemit_temperature_commands")
            continue;
        valid &= validate_custom_gcode(opt_group->title, boost::any_cast<std::string>(opt_group->get_value(key)));
        if (!valid)
            break;
    }
    return valid;
}

void TabPrinter::update_machine_limits_description(const MachineLimitsUsage usage)
{
	wxString text;
	switch (usage) {
	case MachineLimitsUsage::EmitToGCode:
		text = _L("Machine limits will be emitted to G-code and used to estimate print time.");
		break;
	case MachineLimitsUsage::TimeEstimateOnly:
		text = _L("Machine limits will NOT be emitted to G-code, however they will be used to estimate print time, "
			      "which may therefore not be accurate as the printer may apply a different set of machine limits.");
		break;
	case MachineLimitsUsage::Ignore:
		text = _L("Machine limits are not set, therefore the print time estimate may not be accurate.");
		break;
	default: assert(false);
	}
    m_machine_limits_description_line->SetText(text);
}

void Tab::compatible_widget_reload(PresetDependencies &deps)
{
    Field* field = this->get_field(deps.key_condition);
    if (!field)
        return;

    bool has_any = ! m_config->option<ConfigOptionStrings>(deps.key_list)->values.empty();
    has_any ? deps.btn->Enable() : deps.btn->Disable();
    CheckBox::SetValue(deps.checkbox, !has_any);

    field->toggle(! has_any);
}

void Tab::fill_icon_descriptions()
{
    m_icon_descriptions.emplace_back(&m_bmp_value_lock, L("LOCKED LOCK"),
        // TRN Description for "LOCKED LOCK"
        L("indicates that the settings are the same as the system (or default) values for the current option group"));

    m_icon_descriptions.emplace_back(&m_bmp_value_unlock, L("UNLOCKED LOCK"),
        // TRN Description for "UNLOCKED LOCK"
        L("indicates that some settings were changed and are not equal to the system (or default) values for "
        "the current option group.\n"
        "Click the UNLOCKED LOCK icon to reset all settings for current option group to "
        "the system (or default) values."));

    m_icon_descriptions.emplace_back(&m_bmp_white_bullet, L("WHITE BULLET"),
        // TRN Description for "WHITE BULLET"
        L("for the left button: indicates a non-system (or non-default) preset,\n"
          "for the right button: indicates that the settings hasn't been modified."));

    m_icon_descriptions.emplace_back(&m_bmp_value_revert, L("BACK ARROW"),
        // TRN Description for "BACK ARROW"
        L("indicates that the settings were changed and are not equal to the last saved preset for "
        "the current option group.\n"
        "Click the BACK ARROW icon to reset all settings for the current option group to "
        "the last saved preset."));

    m_icon_descriptions.emplace_back(&m_bmp_edit_value, L("EDIT VALUE"),
        // TRN Description for "EDIT VALUE" in the Help dialog (the icon is currently used only to edit custom gcodes).
        L("clicking this icon opens a dialog allowing to edit this value."));
}

void Tab::set_tooltips_text()
{
    // --- Tooltip text for reset buttons (for whole options group)
    // Text to be shown on the "Revert to system" aka "Lock to system" button next to each input field.
    m_ttg_value_lock =		_(L("LOCKED LOCK icon indicates that the settings are the same as the system (or default) values "
                                "for the current option group"));
    m_ttg_value_unlock =	_(L("UNLOCKED LOCK icon indicates that some settings were changed and are not equal "
                                "to the system (or default) values for the current option group.\n"
                                "Click to reset all settings for current option group to the system (or default) values."));
    m_ttg_white_bullet_ns =	_(L("WHITE BULLET icon indicates a non system (or non default) preset."));
    m_ttg_non_system =		&m_ttg_white_bullet_ns;
    // Text to be shown on the "Undo user changes" button next to each input field.
    m_ttg_white_bullet =	_(L("WHITE BULLET icon indicates that the settings are the same as in the last saved "
                                "preset for the current option group."));
    m_ttg_value_revert =	_(L("BACK ARROW icon indicates that the settings were changed and are not equal to "
                                "the last saved preset for the current option group.\n"
                                "Click to reset all settings for the current option group to the last saved preset."));

    // --- Tooltip text for reset buttons (for each option in group)
    // Text to be shown on the "Revert to system" aka "Lock to system" button next to each input field.
    m_tt_value_lock =		_(L("LOCKED LOCK icon indicates that the value is the same as the system (or default) value."));
    m_tt_value_unlock =		_(L("UNLOCKED LOCK icon indicates that the value was changed and is not equal "
                                "to the system (or default) value.\n"
                                "Click to reset current value to the system (or default) value."));
    // 	m_tt_white_bullet_ns=	_(L("WHITE BULLET icon indicates a non system preset."));
    m_tt_non_system =		&m_ttg_white_bullet_ns;
    // Text to be shown on the "Undo user changes" button next to each input field.
    m_tt_white_bullet =		_(L("WHITE BULLET icon indicates that the value is the same as in the last saved preset."));
    m_tt_value_revert =		_(L("BACK ARROW icon indicates that the value was changed and is not equal to the last saved preset.\n"
                                "Click to reset current value to the last saved preset."));
}

bool Tab::select_preset_by_name(const std::string &name_w_suffix, bool force)
{
    return m_presets->select_preset_by_name(name_w_suffix, force);
}

bool Tab::save_current_preset(const std::string& new_name, bool detach)
{
    return m_presets->save_current_preset(new_name, detach);
}

bool Tab::delete_current_preset()
{
    return m_presets->delete_current_preset();
}

Page::Page(wxWindow* parent, const wxString& title, int iconID) :
        m_parent(parent),
        m_title(title),
        m_iconID(iconID)
{
    m_vsizer = (wxBoxSizer*)parent->GetSizer();
    m_item_color = &wxGetApp().get_label_clr_default();
}

void Page::reload_config()
{
    for (auto group : m_optgroups)
        group->reload_config();
}

void Page::update_visibility(ConfigOptionMode mode, bool update_contolls_visibility)
{
    bool ret_val = false;
    for (auto group : m_optgroups) {
        ret_val = (update_contolls_visibility     ? 
                   group->update_visibility(mode) :  // update visibility for all controlls in group
                   group->is_visible(mode)           // just detect visibility for the group
                   ) || ret_val;
    }

    m_show = ret_val;
}

void Page::activate(ConfigOptionMode mode, std::function<void()> throw_if_canceled)
{
    for (auto group : m_optgroups) {
        if (!group->activate(throw_if_canceled))
            continue;
        m_vsizer->Add(group->sizer, 0, wxEXPAND | (group->is_legend_line() ? (wxLEFT|wxTOP) : wxALL), 10);
        group->update_visibility(mode);
        group->reload_config();
        throw_if_canceled();
    }
}

void Page::clear()
{
    for (auto group : m_optgroups)
        group->clear();
}

void Page::msw_rescale()
{
    for (auto group : m_optgroups)
        group->msw_rescale();
}

void Page::sys_color_changed()
{
    for (auto group : m_optgroups)
        group->sys_color_changed();
}

void Page::refresh()
{
    for (auto group : m_optgroups)
        group->refresh();
}

Field* Page::get_field(const t_config_option_key& opt_key, int opt_index /*= -1*/) const
{
    Field* field = nullptr;
    for (auto opt : m_optgroups) {
        field = opt->get_fieldc(opt_key, opt_index);
        if (field != nullptr)
            return field;
    }
    return field;
}

Line* Page::get_line(const t_config_option_key& opt_key)
{
    for (auto opt : m_optgroups)
        if (Line* line = opt->get_line(opt_key))
            return line;
    return nullptr;
}

bool Page::set_value(const t_config_option_key& opt_key, const boost::any& value) {
    bool changed = false;
    for(auto optgroup: m_optgroups) {
        if (optgroup->set_value(opt_key, value))
            changed = true ;
    }
    return changed;
}

// package Slic3r::GUI::Tab::Page;
ConfigOptionsGroupShp Page::new_optgroup(const wxString& title, int noncommon_label_width /*= -1*/)
{
    //! config_ have to be "right"
    ConfigOptionsGroupShp optgroup = std::make_shared<ConfigOptionsGroup>(m_parent, title, m_config, true);
    if (noncommon_label_width >= 0)
        optgroup->label_width = noncommon_label_width;

#ifdef __WXOSX__
    Tab* tab = static_cast<Tab*>(parent()->GetParent()->GetParent());
#else
    Tab* tab = static_cast<Tab*>(parent()->GetParent());
#endif
    optgroup->set_config_category_and_type(m_title, static_cast<Tab*>(tab)->type());
    optgroup->on_change = [tab](t_config_option_key opt_key, boost::any value) {
        // This function will be called from OptionGroup.
        tab->update_dirty();
        tab->on_value_change(opt_key, value);
    };

    optgroup->get_initial_config = [tab]() {
        return tab->m_presets->get_selected_preset().config;
    };

    optgroup->get_sys_config = [tab]() {
        return tab->m_presets->get_selected_preset_parent()->config;
    };

    optgroup->have_sys_config = [tab]() {
        return tab->m_presets->get_selected_preset_parent() != nullptr;
    };

    optgroup->rescale_extra_column_item = [](wxWindow* win) {
        auto *ctrl = dynamic_cast<wxStaticBitmap*>(win);
        if (ctrl == nullptr)
            return;

        ctrl->SetBitmap(reinterpret_cast<ScalableBitmap*>(ctrl->GetClientData())->bmp());
    };

    m_optgroups.push_back(optgroup);

    return optgroup;
}

const ConfigOptionsGroupShp Page::get_optgroup(const wxString& title) const
{
    for (ConfigOptionsGroupShp optgroup : m_optgroups) {
        if (optgroup->title == title)
            return optgroup;
    }

    return nullptr;
}

void TabSLAMaterial::build()
{
    m_presets = &m_preset_bundle->sla_materials;
    load_initial_data();

    auto page = add_options_page(L("Material"), "resin");

    auto optgroup = page->new_optgroup(L("Material"));
    optgroup->append_single_option_line("material_colour");
    optgroup->append_single_option_line("bottle_cost");
    optgroup->append_single_option_line("bottle_volume");
    optgroup->append_single_option_line("bottle_weight");
    optgroup->append_single_option_line("material_density");

    optgroup->on_change = [this](t_config_option_key opt_key, boost::any value)
    {
        if (opt_key == "material_colour") {
            update_dirty();
            on_value_change(opt_key, value); 
            return;
        }

        DynamicPrintConfig new_conf = *m_config;

        if (opt_key == "bottle_volume") {
            double new_bottle_weight =  boost::any_cast<double>(value)*(new_conf.option("material_density")->getFloat() / 1000);
            new_conf.set_key_value("bottle_weight", new ConfigOptionFloat(new_bottle_weight));
        }
        if (opt_key == "bottle_weight") {
            double new_bottle_volume =  boost::any_cast<double>(value)/new_conf.option("material_density")->getFloat() * 1000;
            new_conf.set_key_value("bottle_volume", new ConfigOptionFloat(new_bottle_volume));
        }
        if (opt_key == "material_density") {
            double new_bottle_volume = new_conf.option("bottle_weight")->getFloat() / boost::any_cast<double>(value) * 1000;
            new_conf.set_key_value("bottle_volume", new ConfigOptionFloat(new_bottle_volume));
        }

        load_config(new_conf);

        update_dirty();

        // Change of any from those options influences for an update of "Sliced Info"
        wxGetApp().sidebar().update_sliced_info_sizer();
        wxGetApp().sidebar().Layout();
    };

    optgroup = page->new_optgroup(L("Layers"));
    optgroup->append_single_option_line("initial_layer_height");

    optgroup = page->new_optgroup(L("Exposure"));
    optgroup->append_single_option_line("exposure_time");
    optgroup->append_single_option_line("initial_exposure_time");

    optgroup = page->new_optgroup(L("Corrections"));
    auto line = Line{ m_config->def()->get("material_correction")->full_label, "" };
    for (auto& axis : { "X", "Y", "Z" }) {
        auto opt = optgroup->get_option(std::string("material_correction_") + char(std::tolower(axis[0])));
        opt.opt.label = axis;
        line.append_option(opt);
    }
    optgroup->append_line(line);

    optgroup->append_single_option_line("zcorrection_layers");

    line = Line{ "", "" };
    line.full_width = 1;
    // line.label_path = category_path + "recommended-thin-wall-thickness";
    line.widget = [this](wxWindow* parent) {
        return description_line_widget(parent, &m_z_correction_to_mm_description);
    };
    optgroup->append_line(line);

    add_material_overrides_page();

    page = add_options_page(L("Notes"), "note");
    optgroup = page->new_optgroup(L("Notes"), 0);
    optgroup->label_width = 0;
    Option option = optgroup->get_option("material_notes");
    option.opt.full_width = true;
    option.opt.height = 25;//250;
    optgroup->append_single_option_line(option);

    page = add_options_page(L("Dependencies"), "wrench");
    optgroup = page->new_optgroup(L("Profile dependencies"));

    create_line_with_widget(optgroup.get(), "compatible_printers", "", [this](wxWindow* parent) {
        return compatible_widget_create(parent, m_compatible_printers);
    });
    
    option = optgroup->get_option("compatible_printers_condition");
    option.opt.full_width = true;
    optgroup->append_single_option_line(option);

    create_line_with_widget(optgroup.get(), "compatible_prints", "", [this](wxWindow* parent) {
        return compatible_widget_create(parent, m_compatible_prints);
    });

    option = optgroup->get_option("compatible_prints_condition");
    option.opt.full_width = true;
    optgroup->append_single_option_line(option);

    build_preset_description_line(optgroup.get());

    page = add_options_page(L("Material printing profile"), "note");

#if 1
    optgroup = page->new_optgroup(L("Material printing profile"));
    optgroup->append_single_option_line("material_print_speed");

    optgroup = page->new_optgroup(L("Tilt"));
    optgroup->append_single_option_line("area_fill");

#else
    optgroup = page->new_optgroup(L("Material printing profile"));
    option = optgroup->get_option("material_print_speed");
    optgroup->append_single_option_line(option);

    optgroup->append_single_option_line("area_fill");
#endif

    build_tilt_group(page);
}

static void append_tilt_options_line(ConfigOptionsGroupShp optgroup, const std::string opt_key)
{
    auto option = optgroup->get_option(opt_key, 0);
    auto line = Line{ option.opt.full_label, "" };
    option.opt.width = Field::def_width/*_wider*/();
    line.append_option(option);

    option = optgroup->get_option(opt_key, 1);
    option.opt.width = Field::def_width/*_wider*/();
    line.append_option(option);

    optgroup->append_line(line);
}

void TabSLAMaterial::build_tilt_group(Slic3r::GUI::PageShp page)
{
    // Legend
    std::vector<std::pair<std::string, std::string>> legend_columns = {
        // TRN: This is a label of a column of parameters in settings to be used when the area is below certain threshold.
        {L("Below"),
        L("Values in this column are applied when layer area is smaller than area_fill.")},
        // TRN: This is a label of a column of parameters in settings to be used when the area is above certain threshold.
        {L("Above"),
        L("Values in this column are applied when layer area is larger than area_fill.")},
    };
    create_legend(page, legend_columns, comExpert/*, true*/);

    // TRN: 'Profile' in this context denotes a group of parameters used to configure
    //      layer separation procedure for SLA printers.
    auto optgroup = page->new_optgroup(L("Profile settings"));
    optgroup->on_change = [this, optgroup](const t_config_option_key& key, boost::any value)
    {
        if (key.find_first_of("use_tilt") == 0)
            toggle_tilt_options(key == "use_tilt#0");

        update_dirty();
        update();
    };

    for (const std::string& opt_key : tilt_options())
        append_tilt_options_line(optgroup, opt_key);
}

std::vector<std::string> disable_tilt_options = {
         "tilt_down_initial_speed"
        ,"tilt_down_offset_steps"
        ,"tilt_down_offset_delay"
        ,"tilt_down_finish_speed"
        ,"tilt_down_cycles"
        ,"tilt_down_delay"
        ,"tilt_up_initial_speed"
        ,"tilt_up_offset_steps"
        ,"tilt_up_offset_delay"
        ,"tilt_up_finish_speed"
        ,"tilt_up_cycles"
        ,"tilt_up_delay"
};

void TabSLAMaterial::toggle_tilt_options(bool is_above)
{
    if (m_active_page && m_active_page->title() == "Material printing profile")
    {
        int column_id = is_above ? 0 : 1;
        auto optgroup = m_active_page->get_optgroup("Profile settings");
        bool use_tilt = boost::any_cast<bool>(optgroup->get_config_value(*m_config, "use_tilt", column_id));

        for (const std::string& opt_key : disable_tilt_options) {
            auto field = optgroup->get_fieldc(opt_key, column_id);
            if (field != nullptr)
                field->toggle(use_tilt);
        }
    }
}

void TabSLAMaterial::toggle_options()
{
    if (m_active_page->title() == "Material Overrides")
        update_material_overrides_page();
}

void TabSLAMaterial::update()
{
    if (m_preset_bundle->printers.get_selected_preset().printer_technology() == ptFFF)
        return;

    toggle_options();

    update_description_lines();
    Layout();

// #ys_FIXME. Just a template for this function
//     m_update_cnt++;
//     ! something to update
//     m_update_cnt--;
//
//     if (m_update_cnt == 0)
    wxGetApp().mainframe->on_config_changed(m_config);
}

void TabSLAMaterial::update_description_lines()
{
    if (m_active_page && m_active_page->title() == "Material" &&  m_z_correction_to_mm_description) {
        double lh = m_preset_bundle->sla_prints.get_edited_preset().config.opt_float("layer_height");
        int zlayers = m_config->opt_int("zcorrection_layers");
        m_z_correction_to_mm_description->SetText(GUI::format_wxstr(_L("The current Z-axis height correction is: %1% mm"), zlayers * lh));
    }

    Tab::update_description_lines();
}

void TabSLAMaterial::update_sla_prusa_specific_visibility()
{
    if (m_active_page && m_active_page->title() == "Material printing profile") {
        for (auto& title : { "", "Profile settings" }) {
            auto og_it = std::find_if(m_active_page->m_optgroups.begin(), m_active_page->m_optgroups.end(), 
                         [title](const ConfigOptionsGroupShp og) { return og->title == title; });
            if (og_it != m_active_page->m_optgroups.end())
                og_it->get()->Show(m_mode >= comAdvanced && is_prusa_printer());
        }

        auto og_it = std::find_if(m_active_page->m_optgroups.begin(), m_active_page->m_optgroups.end(), 
                        [](const ConfigOptionsGroupShp og) { return og->title == "Material printing profile"; });
        if (og_it != m_active_page->m_optgroups.end())
            og_it->get()->Show(m_mode >= comAdvanced && !is_prusa_printer());

        Layout();
    }
}

void TabSLAMaterial::clear_pages()
{
    Tab::clear_pages();

    for (auto& over_opt : m_overrides_options)
        over_opt.second = nullptr;

    m_z_correction_to_mm_description = nullptr;
}

void TabSLAMaterial::msw_rescale()
{
    for (const auto& over_opt : m_overrides_options)
        if (wxWindow* win = over_opt.second)
            win->SetInitialSize(win->GetBestSize());
    Tab::msw_rescale();
}

void TabSLAMaterial::sys_color_changed()
{
    Tab::sys_color_changed();

    for (const auto& over_opt : m_overrides_options)
        if (wxWindow* check_box = over_opt.second) {
            wxGetApp().UpdateDarkUI(check_box);
            CheckBox::SysColorChanged(check_box);
        }
}

static void add_options_into_line(ConfigOptionsGroupShp &optgroup,
                                  const std::vector<SamePair<std::string>> &prefixes,
                                  const std::string &optkey,
                                  const std::string &preprefix = std::string())
{
    auto opt = optgroup->get_option(preprefix + prefixes.front().first + optkey);
    Line line{ opt.opt.label, "" };
    line.full_width = 1;
    for (auto &prefix : prefixes) {
        opt = optgroup->get_option(preprefix + prefix.first + optkey);
        opt.opt.label = prefix.second;
        opt.opt.width = 12; // TODO
        line.append_option(opt);
    }
    optgroup->append_line(line);
}

void TabSLAPrint::build_sla_support_params(const std::vector<SamePair<std::string>> &prefixes,
                                           const Slic3r::GUI::PageShp &page)
{

    auto optgroup = page->new_optgroup(L("Support head"));
    add_options_into_line(optgroup, prefixes, "support_head_front_diameter");
    add_options_into_line(optgroup, prefixes, "support_head_penetration");
    add_options_into_line(optgroup, prefixes, "support_head_width");

    optgroup = page->new_optgroup(L("Support pillar"));
    add_options_into_line(optgroup, prefixes, "support_pillar_diameter");
    add_options_into_line(optgroup, prefixes, "support_small_pillar_diameter_percent");
    add_options_into_line(optgroup, prefixes, "support_max_bridges_on_pillar");

    add_options_into_line(optgroup, prefixes, "support_pillar_connection_mode");
    add_options_into_line(optgroup, prefixes, "support_buildplate_only");
    add_options_into_line(optgroup, prefixes, "support_pillar_widening_factor");
    add_options_into_line(optgroup, prefixes, "support_max_weight_on_model");
    add_options_into_line(optgroup, prefixes, "support_base_diameter");
    add_options_into_line(optgroup, prefixes, "support_base_height");
    add_options_into_line(optgroup, prefixes, "support_base_safety_distance");

    // Mirrored parameter from Pad page for toggling elevation on the same page
    add_options_into_line(optgroup, prefixes, "support_object_elevation");

    Line line{ "", "" };
    line.full_width = 1;
    line.widget = [this](wxWindow* parent) {
        return description_line_widget(parent, &m_support_object_elevation_description_line);
    };
    optgroup->append_line(line);

    optgroup = page->new_optgroup(L("Connection of the support sticks and junctions"));
    add_options_into_line(optgroup, prefixes, "support_critical_angle");
    add_options_into_line(optgroup, prefixes, "support_max_bridge_length");
    add_options_into_line(optgroup, prefixes, "support_max_pillar_link_distance");
}

static std::vector<std::string> get_override_opt_kyes_for_line(const std::string& title, const std::string& key)
{
    const std::string preprefix = "material_ow_";

    std::vector<std::string> opt_keys;
    opt_keys.reserve(3);

    if (title == "Support head" || title == "Support pillar") {
        for (auto& prefix : { "", "branching" })
            opt_keys.push_back(preprefix + prefix + key);
    }
    else
        opt_keys.push_back(preprefix + key);

    return opt_keys;
}

void TabSLAMaterial::create_line_with_near_label_widget(ConfigOptionsGroupShp optgroup, const std::string& key)
{
    if (optgroup->title == "Support head" || optgroup->title == "Support pillar")
        add_options_into_line(optgroup, { {"", L("Default")}, {"branching", L("Branching")} }, key, "material_ow_");
    else {
        const std::string opt_key = std::string("material_ow_") + key;
        optgroup->append_single_option_line(opt_key);
    }

    Line* line = optgroup->get_last_line();
    if (!line)
        return;

    line->near_label_widget = [this, optgroup_wk = ConfigOptionsGroupWkp(optgroup), key](wxWindow* parent) {
        wxWindow* check_box = CheckBox::GetNewWin(parent);
        wxGetApp().UpdateDarkUI(check_box);

        check_box->Bind(wxEVT_CHECKBOX, [this, optgroup_wk, key](wxCommandEvent& evt) {
            const bool is_checked = evt.IsChecked();
            if (auto optgroup_sh = optgroup_wk.lock(); optgroup_sh) {
                auto opt_keys = get_override_opt_kyes_for_line(optgroup_sh->title.ToStdString(), key);
                for (const std::string& opt_key : opt_keys)
                    if (Field* field = optgroup_sh->get_fieldc(opt_key, 0); field != nullptr) {
                        field->toggle(is_checked);
                        if (is_checked)
                            field->set_last_meaningful_value();
                        else
                            field->set_na_value();
                    }
            }

            toggle_options();
        });

        m_overrides_options[key] = check_box;
        return check_box;
    };
}

std::vector<std::pair<std::string, std::vector<std::string>>> material_overrides_option_keys{
    {"Support head", {
        "support_head_front_diameter",
        "support_head_penetration",
        "support_head_width"
    }},
    {"Support pillar", {
        "support_pillar_diameter",
    }},
    {"Automatic generation", {
        "support_points_density_relative"
    }},
    {"Corrections", {
        "absolute_correction",
        "elefant_foot_compensation"
    }}
};

void TabSLAMaterial::add_material_overrides_page()
{
    // TRN: Page title in Material Settings in SLA mode.
    PageShp page = add_options_page(L("Material Overrides"), "wrench");

    for (const auto& [title, keys] : material_overrides_option_keys) {
        ConfigOptionsGroupShp optgroup = page->new_optgroup(L(title));
        for (const std::string& opt_key : keys) {
            create_line_with_near_label_widget(optgroup, opt_key);
        }
    }
}

void TabSLAMaterial::update_line_with_near_label_widget(ConfigOptionsGroupShp optgroup, const std::string& key, bool is_checked/* = true*/)
{
    if (!m_overrides_options[key])
        return;

    const std::string preprefix = "material_ow_";

    std::vector<std::string> opt_keys;
    opt_keys.reserve(3);

    if (optgroup->title == "Support head" || optgroup->title == "Support pillar") {
        for (auto& prefix : { "", "branching" }) {
            std::string opt_key = preprefix + prefix + key;
            is_checked = !m_config->option(opt_key)->is_nil();
            opt_keys.push_back(opt_key);
        }
    }
    else if (key == "relative_correction") {
        for (auto& axis : { "x", "y", "z" }) {
            std::string opt_key = preprefix + key + "_" + char(axis[0]);
            is_checked = !m_config->option(opt_key)->is_nil();
            opt_keys.push_back(opt_key);
        }
    }
    else {
        std::string opt_key = preprefix + key;
        is_checked = !m_config->option(opt_key)->is_nil();
        opt_keys.push_back(opt_key);
    }

    CheckBox::SetValue(m_overrides_options[key], is_checked);

    for (const std::string& opt_key : opt_keys) {
        Field* field = optgroup->get_field(opt_key);
        if (field != nullptr)
            field->toggle(is_checked);
    }
}

void TabSLAMaterial::update_material_overrides_page()
{
    if (!m_active_page || m_active_page->title() != "Material Overrides")
        return;
    Page* page = m_active_page;

    for (const auto& [title, keys] : material_overrides_option_keys) {
        std::optional<ConfigOptionsGroupShp> optgroup{ get_option_group(page, title) };
        if (!optgroup) {
            continue;
        }

        for (const std::string& key : keys) {
            update_line_with_near_label_widget(*optgroup, key);
        }
    }
}

void TabSLAPrint::build()
{
    m_presets = &m_preset_bundle->sla_prints;
    load_initial_data();

    auto page = add_options_page(L("Layers and perimeters"), "layers");

    auto optgroup = page->new_optgroup(L("Layers"));
    optgroup->append_single_option_line("layer_height");
    optgroup->append_single_option_line("faded_layers");

    page = add_options_page(L("Supports"), "support"/*"sla_supports"*/);

    optgroup = page->new_optgroup(L("Supports"));
    optgroup->append_single_option_line("supports_enable");
    optgroup->append_single_option_line("support_tree_type");
    optgroup->append_single_option_line("support_enforcers_only");
    
    build_sla_support_params({{"", L("Default")}, {"branching", L("Branching")}}, page);

    optgroup = page->new_optgroup(L("Automatic generation"));
    optgroup->append_single_option_line("support_points_density_relative");
    optgroup->append_single_option_line("support_points_minimal_distance");

    page = add_options_page(L("Pad"), "pad");
    optgroup = page->new_optgroup(L("Pad"));
    optgroup->append_single_option_line("pad_enable");
    optgroup->append_single_option_line("pad_wall_thickness");
    optgroup->append_single_option_line("pad_wall_height");
    optgroup->append_single_option_line("pad_brim_size");
    optgroup->append_single_option_line("pad_max_merge_distance");
    // TODO: Disabling this parameter for the beta release
//    optgroup->append_single_option_line("pad_edge_radius");
    optgroup->append_single_option_line("pad_wall_slope");

    optgroup->append_single_option_line("pad_around_object");
    optgroup->append_single_option_line("pad_around_object_everywhere");
    optgroup->append_single_option_line("pad_object_gap");
    optgroup->append_single_option_line("pad_object_connector_stride");
    optgroup->append_single_option_line("pad_object_connector_width");
    optgroup->append_single_option_line("pad_object_connector_penetration");
    
    page = add_options_page(L("Hollowing"), "hollowing");
    optgroup = page->new_optgroup(L("Hollowing"));
    optgroup->append_single_option_line("hollowing_enable");
    optgroup->append_single_option_line("hollowing_min_thickness");
    optgroup->append_single_option_line("hollowing_quality");
    optgroup->append_single_option_line("hollowing_closing_distance");

    page = add_options_page(L("Advanced"), "wrench");
    optgroup = page->new_optgroup(L("Slicing"));
    optgroup->append_single_option_line("slice_closing_radius");
    optgroup->append_single_option_line("slicing_mode");

    page = add_options_page(L("Output options"), "output+page_white");
    optgroup = page->new_optgroup(L("Output file"));
    Option option = optgroup->get_option("output_filename_format");
    option.opt.full_width = true;
    optgroup->append_single_option_line(option);

    page = add_options_page(L("Dependencies"), "wrench");
    optgroup = page->new_optgroup(L("Profile dependencies"));

    create_line_with_widget(optgroup.get(), "compatible_printers", "", [this](wxWindow* parent) {
        return compatible_widget_create(parent, m_compatible_printers);
    });

    option = optgroup->get_option("compatible_printers_condition");
    option.opt.full_width = true;
    optgroup->append_single_option_line(option);

    build_preset_description_line(optgroup.get());
}

void TabSLAPrint::update_description_lines()
{
    Tab::update_description_lines();

    if (m_active_page && m_active_page->title() == "Supports")
    {
        bool is_visible = m_config->def()->get("support_object_elevation")->mode <= m_mode;
        if (m_support_object_elevation_description_line)
        {
            m_support_object_elevation_description_line->Show(is_visible);
            if (is_visible)
            {
                bool elev = !m_config->opt_bool("pad_enable") || !m_config->opt_bool("pad_around_object");
                m_support_object_elevation_description_line->SetText(elev ? "" :
                    format_wxstr(_L("\"%1%\" is disabled because \"%2%\" is on in \"%3%\" category.\n"
                        "To enable \"%1%\", please switch off \"%2%\"")
                        , _L("Object elevation"), _L("Pad around object"), _L("Pad")));
            }
        }
    }
}

void TabSLAPrint::toggle_options()
{
    if (m_active_page)
        m_config_manipulation.toggle_print_sla_options(m_config);
}

void TabSLAPrint::update()
{
    if (m_preset_bundle->printers.get_selected_preset().printer_technology() == ptFFF)
        return;

    m_update_cnt++;

    update_description_lines();
    Layout();

    m_update_cnt--;

    if (m_update_cnt == 0) {
        toggle_options();

        // update() could be called during undo/redo execution
        // Update of objectList can cause a crash in this case (because m_objects doesn't match ObjectList) 
        if (!wxGetApp().plater()->inside_snapshot_capture())
            wxGetApp().obj_list()->update_and_show_object_settings_item();

        wxGetApp().mainframe->on_config_changed(m_config);
    }
}

void TabSLAPrint::clear_pages()
{
    Tab::clear_pages();

    m_support_object_elevation_description_line = nullptr;
}

ConfigManipulation Tab::get_config_manipulation()
{
    auto load_config = [this]()
    {
        update_dirty();
        // Initialize UI components with the config values.
        reload_config();
        update();
    };

    auto cb_toggle_field = [this](const t_config_option_key& opt_key, bool toggle, int opt_index) {
        return toggle_option(opt_key, toggle, opt_index);
    };

    auto cb_value_change = [this](const std::string& opt_key, const boost::any& value) {
        return on_value_change(opt_key, value);
    };

    return ConfigManipulation(load_config, cb_toggle_field, cb_value_change, nullptr, this);
}


} // GUI
} // Slic3r
