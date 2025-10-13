///|/ Copyright (c) Prusa Research 2018 - 2023 Tomáš Mészáros @tamasmeszaros, Oleksandra Iushchenko @YuSanka, Enrico Turri @enricoturri1966, David Kocík @kocikdav, Lukáš Hejl @hejllukas, Vojtěch Bubník @bubnikv, Lukáš Matěna @lukasmatena, Pavel Mikuš @Godrak, Filip Sykala @Jony01, Vojtěch Král @vojtechkral
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
#ifndef slic3r_Sidebar_hpp_
#define slic3r_Sidebar_hpp_

#include <vector>

#include <wx/panel.h>
#include <wx/string.h>
#include <wx/sizer.h>

#include "libslic3r/Preset.hpp"
#include "GUI.hpp"
#include "Event.hpp"

class wxButton;
class wxScrolledWindow;
class ScalableButton;
class ModeSizer;

namespace Slic3r {

namespace GUI {

class ConfigOptionsGroup;
class FreqChangedParams;
class ObjectInfo;
class SlicedInfo;
class ObjectManipulation;
class ObjectSettings;
class ObjectLayers;
class ObjectList;
class PlaterPresetComboBox;
class Plater;

enum class ActionButtonType : int {
    Reslice,
    Export,
    SendGCode,
    Connect
};

class Sidebar : public wxPanel
{
    ConfigOptionMode    m_mode{ConfigOptionMode::comSimple};

    Plater*             m_plater            { nullptr };

    wxScrolledWindow*   m_scrolled_panel    { nullptr };
    wxPanel*            m_presets_panel     { nullptr }; // Used for MSW better layouts

    wxFlexGridSizer*    m_presets_sizer     { nullptr };
    wxBoxSizer*         m_filaments_sizer   { nullptr };

    PlaterPresetComboBox*               m_combo_print       { nullptr };
    PlaterPresetComboBox*               m_combo_sla_print   { nullptr };
    PlaterPresetComboBox*               m_combo_sla_material{ nullptr };
    PlaterPresetComboBox*               m_combo_printer     { nullptr };
    std::vector<PlaterPresetComboBox*>  m_combos_filament;

    ObjectList*     m_object_list               { nullptr };
    ObjectInfo*     m_object_info               { nullptr };
    SlicedInfo*     m_sliced_info               { nullptr };
    wxBoxSizer*     m_btns_sizer                { nullptr };
    wxBoxSizer*     m_autoslicing_btns_sizer    { nullptr };


    wxButton*       m_btn_export_gcode          { nullptr };
    wxButton*       m_btn_reslice               { nullptr };
    wxButton*       m_btn_connect_gcode         { nullptr };
    ScalableButton* m_btn_send_gcode            { nullptr };
    ScalableButton* m_btn_export_gcode_removable{ nullptr }; //exports to removable drives (appears only if removable drive is connected)
                                                             //
    wxButton* m_btn_export_all_gcode                { nullptr };
    wxButton* m_btn_connect_gcode_all               { nullptr };
	ScalableButton* m_btn_export_all_gcode_removable{ nullptr };

    std::unique_ptr<FreqChangedParams>  m_frequently_changed_parameters;
    std::unique_ptr<ObjectManipulation> m_object_manipulation;
    std::unique_ptr<ObjectSettings>     m_object_settings;
    std::unique_ptr<ObjectLayers>       m_object_layers;

    bool m_autoslicing_mode{ false };
#ifdef _WIN32
    wxString m_reslice_btn_tooltip;
#endif

    void init_filament_combo(PlaterPresetComboBox **combo, int extr_idx);
    void remove_unused_filament_combos(const size_t current_extruder_count);
    void update_all_preset_comboboxes();
    void update_reslice_btn_tooltip();

    void show_preset_comboboxes();
    void on_select_preset(wxCommandEvent& evt);

public:
    Sidebar(Plater *parent);
    Sidebar(Sidebar &&) = delete;
    Sidebar(const Sidebar &) = delete;
    Sidebar &operator=(Sidebar &&) = delete;
    Sidebar &operator=(const Sidebar &) = delete;
    ~Sidebar();

    ObjectManipulation*     obj_manipul();
    ObjectList*             obj_list();
    ObjectSettings*         obj_settings();
    ObjectLayers*           obj_layers();

    ConfigOptionsGroup*     og_freq_chng_params(const bool is_fff);
    wxButton*               get_wiping_dialog_button();

    void show_info_sizer();
    void show_sliced_info_sizer(const bool show);
    void show_btns_sizer(const bool show);
    void show_bulk_btns_sizer(const bool show);

    void update_sliced_info_sizer();

    void enable_buttons(bool enable);
    void set_btn_label(const ActionButtonType btn_type, const wxString& label) const;
    bool show_reslice(bool show) const;
    bool show_export(bool show) const;
    bool show_send(bool show) const;
    bool show_export_removable(bool show) const;
    bool show_connect(bool show) const;

    void enable_bulk_buttons(bool enable);
    bool show_export_all(bool show) const;
    bool show_export_removable_all(bool show) const;
    bool show_connect_all(bool show) const;

    void switch_to_autoslicing_mode();
    void switch_from_autoslicing_mode();

    void collapse(bool collapse);
    void set_extruders_count(size_t extruders_count);

    void update_mode();
    void update_ui_from_settings();
    void update_objects_list_extruder_column(size_t extruders_count);
    void update_presets(Preset::Type preset_type);
    void update_printer_presets_combobox();
    void update_all_filament_comboboxes();

    void msw_rescale();
    void sys_color_changed();

    bool is_collapsed{ false };
};

} // namespace GUI
} // namespace Slic3r

#endif
