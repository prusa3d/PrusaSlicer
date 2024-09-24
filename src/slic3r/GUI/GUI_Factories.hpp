///|/ Copyright (c) Prusa Research 2021 - 2022 Oleksandra Iushchenko @YuSanka, Tomáš Mészáros @tamasmeszaros, Lukáš Matěna @lukasmatena, Pavel Mikuš @Godrak, Filip Sykala @Jony01, Vojtěch Bubník @bubnikv
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef slic3r_GUI_Factories_hpp_
#define slic3r_GUI_Factories_hpp_

#include <map>
#include <vector>
#include <array>

#include <wx/bitmap.h>

#include "libslic3r/PrintConfig.hpp"
#include "wxExtensions.hpp"

class wxMenu;
class wxMenuItem;

// ----------------------------------------------------------------------------
// MenuWithSeparators
// ----------------------------------------------------------------------------

class MenuWithSeparators : public wxMenu
{
public:
    MenuWithSeparators(const wxString& title, long style = 0)
        : wxMenu(title, style) {}

    MenuWithSeparators(long style = 0)
        : wxMenu(style) {}

    ~MenuWithSeparators() {}

    void DestroySeparators();
    void SetFirstSeparator();
    void SetSecondSeparator();

private:
    wxMenuItem* m_separator_frst{ nullptr };    // use like separator before settings item
    wxMenuItem* m_separator_scnd{ nullptr };   // use like separator between settings items
};

namespace Slic3r {

enum class ModelVolumeType : int;

namespace GUI {

struct SettingsFactory
{
//				     category ->       vector ( option )
    typedef std::map<std::string, std::vector<std::string>> Bundle;
    static std::map<std::string, std::string>               CATEGORY_ICON;

    static wxBitmapBundle*                      get_category_bitmap(const std::string& category_name);
    static Bundle                               get_bundle(const DynamicPrintConfig* config, bool is_object_settings);
    static std::vector<std::string>             get_options(bool is_part);
};

class MenuFactory
{
public:
	static std::vector<wxBitmapBundle*> get_volume_bitmaps();
	static std::vector<wxBitmapBundle*> get_text_volume_bitmaps();
	static std::vector<wxBitmapBundle*> get_svg_volume_bitmaps();

    static wxString                     get_repaire_result_message(const std::vector<std::string>& succes_models,
                                                                   const std::vector<std::pair<std::string, std::string>>& failed_models);

    MenuFactory();
    ~MenuFactory() = default;

    void    init(wxWindow* parent);
    void    update();
    void    update_objects_menu();
    void    update_default_menu();
    void    sys_color_changed();

    static void sys_color_changed(wxMenu* menu);
    static void sys_color_changed(wxMenuBar* menu_bar);

    wxMenu* default_menu();
    wxMenu* object_menu();
    wxMenu* sla_object_menu();
    wxMenu* part_menu();
    wxMenu* text_part_menu();
    wxMenu* svg_part_menu();
    wxMenu* instance_menu();
    wxMenu* layer_menu();
    wxMenu* multi_selection_menu();

private:
    enum MenuType {
        mtObjectFFF = 0,
        mtObjectSLA,
        mtCount
    };

    wxWindow* m_parent {nullptr};

    MenuWithSeparators m_object_menu;
    MenuWithSeparators m_part_menu;
    MenuWithSeparators m_text_part_menu;
    MenuWithSeparators m_svg_part_menu;
    MenuWithSeparators m_sla_object_menu;
    MenuWithSeparators m_default_menu;
    MenuWithSeparators m_instance_menu;

    // Removed/Prepended Items according to the view mode
    std::array<wxMenuItem*, mtCount> items_increase;
    std::array<wxMenuItem*, mtCount> items_decrease;
    std::array<wxMenuItem*, mtCount> items_set_number_of_copies;

    void        create_default_menu();
    void        create_common_object_menu(wxMenu *menu);
    void        append_immutable_part_menu_items(wxMenu* menu);
    void        append_mutable_part_menu_items(wxMenu* menu);
    void        create_part_menu();
    void        create_text_part_menu();
    void        create_svg_part_menu();
    void        create_instance_menu();

    wxMenu*     append_submenu_add_generic(wxMenu* menu, ModelVolumeType type);
    void        append_menu_item_add_text(wxMenu* menu, ModelVolumeType type, bool is_submenu_item = true);
    void        append_menu_item_add_svg(wxMenu *menu, ModelVolumeType type, bool is_submenu_item = true);    
    void        append_menu_items_add_volume(MenuType type);
    wxMenuItem* append_menu_item_layers_editing(wxMenu* menu);
    wxMenuItem* append_menu_item_settings(wxMenu* menu);
    wxMenuItem* append_menu_item_change_type(wxMenu* menu);
    wxMenuItem* append_menu_item_instance_to_object(wxMenu* menu);
    wxMenuItem* append_menu_item_printable(wxMenu* menu);
    void        append_menu_item_invalidate_cut_info(wxMenu *menu);
    void        append_menu_items_osx(wxMenu* menu);
    wxMenuItem* append_menu_item_fix_through_winsdk(wxMenu* menu);
    wxMenuItem* append_menu_item_simplify(wxMenu* menu);
    void        append_menu_item_export_stl(wxMenu* menu);
    void        append_menu_item_reload_from_disk(wxMenu* menu);
    void        append_menu_item_replace_with_stl(wxMenu* menu);
    void        append_menu_item_change_extruder(wxMenu* menu);
    void        append_menu_item_delete(wxMenu* menu);
    void        append_menu_item_scale_selection_to_fit_print_volume(wxMenu* menu);
    void        append_menu_items_convert_unit(wxMenu* menu, int insert_pos = 1); // Add "Conver/Revert..." menu items (from/to inches/meters) after "Reload From Disk"
    void        append_menu_item_merge_to_multipart_object(wxMenu *menu);
//    void        append_menu_item_merge_to_single_object(wxMenu *menu);
    void        append_menu_items_mirror(wxMenu *menu);
    void        append_menu_item_edit_text(wxMenu *menu);
    void        append_menu_item_edit_svg(wxMenu *menu);
    void        append_menu_items_instance_manipulation(wxMenu *menu);
    void        update_menu_items_instance_manipulation(MenuType type);
    void        append_menu_items_split(wxMenu *menu);
};

}}

#endif //slic3r_GUI_Factories_hpp_
