///|/ Copyright (c) Prusa Research 2017 - 2023 Oleksandra Iushchenko @YuSanka, Tomáš Mészáros @tamasmeszaros, Vojtěch Bubník @bubnikv, Lukáš Matěna @lukasmatena, Enrico Turri @enricoturri1966, Vojtěch Král @vojtechkral
///|/ Copyright (c) 2019 John Drake @foxox
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
#ifndef slic3r_Tab_hpp_
#define slic3r_Tab_hpp_

//	 The "Expert" tab at the right of the main tabbed window.
//	
//	 This file implements following packages:
//	   Slic3r::GUI::Tab;
//	       Slic3r::GUI::Tab::Print;
//	       Slic3r::GUI::Tab::Filament;
//	       Slic3r::GUI::Tab::Printer;
//	   Slic3r::GUI::Tab::Page
//	       - Option page: For example, the Slic3r::GUI::Tab::Print has option pages "Layers and perimeters", "Infill", "Skirt and brim" ...
//	   Slic3r::GUI::SavePresetWindow
//	       - Dialog to select a new preset name to store the configuration.
//	   Slic3r::GUI::Tab::Preset;
//	       - Single preset item: name, file is default or external.

#include <wx/panel.h>
#include <wx/notebook.h>
#include <wx/listbook.h>
#include <wx/scrolwin.h>
#include <wx/sizer.h>
#include <wx/bmpcbox.h>
#include <wx/bmpbuttn.h>
#include <wx/treectrl.h>
#include <wx/imaglist.h>

#include <map>
#include <vector>
#include <memory>

#include "BedShapeDialog.hpp"
#include "ButtonsDescription.hpp"
#include "Event.hpp"
#include "wxExtensions.hpp"
#include "ConfigManipulation.hpp"
#include "OptionsGroup.hpp"
#include "libslic3r/Preset.hpp"

class CheckBox;

namespace Slic3r {
namespace GUI {

class TabPresetComboBox;
class OG_CustomCtrl;

// G-code substitutions

// Substitution Manager - helper for manipuation of the substitutions
class SubstitutionManager
{
	DynamicPrintConfig* m_config{ nullptr };
	wxWindow*			m_parent{ nullptr };
	wxFlexGridSizer*	m_grid_sizer{ nullptr };

	int                 m_em{10};
	std::function<void()> m_cb_edited_substitution{ nullptr };
	std::function<void()> m_cb_hide_delete_all_btn{ nullptr };

	std::vector<std::string>	m_substitutions;
	std::vector<wxWindow*>		m_chb_match_single_lines;

	void validate_length();
	bool is_compatible_with_ui();
	bool is_valid_id(int substitution_id, const wxString& message);

public:
	SubstitutionManager() = default;
	~SubstitutionManager() = default;

	void init(DynamicPrintConfig* config, wxWindow* parent, wxFlexGridSizer* grid_sizer);
	void create_legend();
	void delete_substitution(int substitution_id);
	void add_substitution(	int substitution_id = -1,
							const std::string& plain_pattern = std::string(),
							const std::string& format = std::string(),
							const std::string& params = std::string(),
							const std::string& notes  = std::string());
	void update_from_config();
	void delete_all();
	void edit_substitution(int substitution_id, 
						   int opt_pos, // option position insubstitution [0, 2]
						   const std::string& value);
	void set_cb_edited_substitution(std::function<void()> cb_edited_substitution) {
		m_cb_edited_substitution = cb_edited_substitution;
	}
	void call_ui_update() {
		if (m_cb_edited_substitution)
			m_cb_edited_substitution();
	}
	void set_cb_hide_delete_all_btn(std::function<void()> cb_hide_delete_all_btn) {
		m_cb_hide_delete_all_btn = cb_hide_delete_all_btn;
	}
	void hide_delete_all_btn() {
		if (m_cb_hide_delete_all_btn)
			m_cb_hide_delete_all_btn();
	}
	bool is_empty_substitutions();
};

// Single Tab page containing a{ vsizer } of{ optgroups }
// package Slic3r::GUI::Tab::Page;
using ConfigOptionsGroupShp = std::shared_ptr<ConfigOptionsGroup>;
class Page// : public wxScrolledWindow
{
	wxWindow*		m_parent;
	wxString		m_title;
	size_t			m_iconID;
	wxBoxSizer*		m_vsizer;
    bool            m_show = true;
public:
    Page(wxWindow* parent, const wxString& title, int iconID);
	~Page() {}

	bool				m_is_modified_values{ false };
	bool				m_is_nonsys_values{ true };

public:
	std::vector <ConfigOptionsGroupShp> m_optgroups;
	DynamicPrintConfig* m_config;

	wxBoxSizer*	vsizer() const { return m_vsizer; }
	wxWindow*	parent() const { return m_parent; }
	const wxString&	title()	 const { return m_title; }
	size_t		iconID() const { return m_iconID; }
	void		set_config(DynamicPrintConfig* config_in) { m_config = config_in; }
	void		reload_config();
    void        update_visibility(ConfigOptionMode mode, bool update_contolls_visibility);
    void        activate(ConfigOptionMode mode, std::function<void()> throw_if_canceled);
    void        clear();
    void        msw_rescale();
    void        sys_color_changed();
    void        refresh();
	Field*		get_field(const t_config_option_key& opt_key, int opt_index = -1) const;
	Line*		get_line(const t_config_option_key& opt_key);
	bool		set_value(const t_config_option_key& opt_key, const boost::any& value);
	ConfigOptionsGroupShp	new_optgroup(const wxString& title, int noncommon_label_width = -1);
	const ConfigOptionsGroupShp	get_optgroup(const wxString& title) const;

	bool		set_item_colour(const wxColour *clr) {
		if (m_item_color != clr) {
			m_item_color = clr;
			return true;
		}
		return false;
	}

	const wxColour	get_item_colour() {
			return *m_item_color;
	}
    bool get_show() const { return m_show; }

protected:
	// Color of TreeCtrlItem. The wxColour will be updated only if the new wxColour pointer differs from the currently rendered one.
	const wxColour*		m_item_color;
};


using PageShp = std::shared_ptr<Page>;
class Tab: public wxPanel
{
	wxBookCtrlBase*			m_parent;
#ifdef __WXOSX__
	wxPanel*			m_tmp_panel;
	int					m_size_move = -1;
#endif // __WXOSX__
protected:
    Preset::Type        m_type;
	std::string			m_name;
	const wxString		m_title;
	TabPresetComboBox*	m_presets_choice;
	ScalableButton*		m_btn_compare_preset;
	ScalableButton*		m_btn_save_preset;
	ScalableButton*		m_btn_rename_preset;
	ScalableButton*		m_btn_delete_preset;
	ScalableButton*		m_btn_edit_ph_printer {nullptr};
	ScalableButton*		m_btn_hide_incompatible_presets;
	wxBoxSizer*			m_top_hsizer;
	wxBoxSizer*			m_hsizer;
	wxBoxSizer*			m_h_buttons_sizer;
	wxBoxSizer*			m_left_sizer;
	wxTreeCtrl*			m_treectrl;

	wxScrolledWindow*	m_page_view {nullptr};
	wxBoxSizer*			m_page_sizer {nullptr};

   	struct PresetDependencies {
		Preset::Type type	  = Preset::TYPE_INVALID;
		wxWindow 	*checkbox = nullptr;
		ScalableButton 	*btn  = nullptr;
		std::string  key_list; // "compatible_printers"
		std::string  key_condition;
		wxString     dialog_title;
		wxString     dialog_label;
	};
	PresetDependencies 	m_compatible_printers;
	PresetDependencies 	m_compatible_prints;

    /* Indicates, that default preset or preset inherited from default is selected
     * This value is used for a options color updating 
     * (use green color only for options, which values are equal to system values)
     */
    bool                    m_is_default_preset {false};

	ScalableButton*			m_undo_btn;
	ScalableButton*			m_undo_to_sys_btn;
	ScalableButton*			m_question_btn;

	// Bitmaps to be shown on the "Revert to system" aka "Lock to system" button next to each input field.
	ScalableBitmap 			m_bmp_value_lock;
	ScalableBitmap 			m_bmp_value_unlock;
	ScalableBitmap 			m_bmp_white_bullet;
	// The following bitmap points to either m_bmp_value_unlock or m_bmp_white_bullet, depending on whether the current preset has a parent preset.
	ScalableBitmap 		   *m_bmp_non_system;
	// Bitmaps to be shown on the "Undo user changes" button next to each input field.
	ScalableBitmap 			m_bmp_value_revert;
	// Bitmaps to be shown on the "Undo user changes" button next to each input field.
	ScalableBitmap 			m_bmp_edit_value;
    
    std::vector<ScalableButton*>	m_scaled_buttons = {};    
    std::vector<ScalableBitmap*>	m_scaled_bitmaps = {};    
    std::vector<ScalableBitmap>     m_scaled_icons_list = {};

	// Colors for ui "decoration"
	wxColour			m_sys_label_clr;
	wxColour			m_modified_label_clr;
	wxColour			m_default_text_clr;

	// Tooltip text for reset buttons (for whole options group)
	wxString			m_ttg_value_lock;
	wxString			m_ttg_value_unlock;
	wxString			m_ttg_white_bullet_ns;
	// The following text points to either m_ttg_value_unlock or m_ttg_white_bullet_ns, depending on whether the current preset has a parent preset.
	wxString			*m_ttg_non_system;
	// Tooltip text to be shown on the "Undo user changes" button next to each input field.
	wxString			m_ttg_white_bullet;
	wxString			m_ttg_value_revert;

	// Tooltip text for reset buttons (for each option in group)
	wxString			m_tt_value_lock;
	wxString			m_tt_value_unlock;
	// The following text points to either m_tt_value_unlock or m_ttg_white_bullet_ns, depending on whether the current preset has a parent preset.
	wxString			*m_tt_non_system;
	// Tooltip text to be shown on the "Undo user changes" button next to each input field.
	wxString			m_tt_white_bullet;
	wxString			m_tt_value_revert;

	int					m_icon_count;
	std::map<std::string, size_t>	m_icon_index;		// Map from an icon file name to its index
	std::map<wxString, std::string>	m_category_icon;	// Map from a category name to an icon file name
	std::vector<PageShp>			m_pages;
	Page*				m_active_page {nullptr};
	bool				m_disable_tree_sel_changed_event {false};
	bool				m_show_incompatible_presets;

    std::vector<Preset::Type>	m_dependent_tabs;
	enum OptStatus { osSystemValue = 1, osInitValue = 2 };
	std::map<std::string, int>	m_options_list;
	int							m_opt_status_value = 0;

	std::vector<GUI_Descriptions::ButtonEntry>	m_icon_descriptions = {};

	bool				m_is_modified_values{ false };
	bool				m_is_nonsys_values{ true };
	bool				m_postpone_update_ui {false};

    void                set_type();

    int                 m_em_unit;
    // To avoid actions with no-completed Tab
    bool                m_completed { false };
    ConfigOptionMode    m_mode = comExpert; // to correct first Tab update_visibility() set mode to Expert

	HighlighterForWx	m_highlighter;

	DynamicPrintConfig 	m_cache_config;


	bool				m_page_switch_running = false;
	bool				m_page_switch_planned = false;

public:
	PresetBundle*		m_preset_bundle;
	bool				m_show_btn_incompatible_presets = false;
	PresetCollection*	m_presets;
	DynamicPrintConfig*	m_config;
	ogStaticText*		m_parent_preset_description_line = nullptr;
	ScalableButton*		m_detach_preset_btn	= nullptr;

    // Counter for the updating (because of an update() function can have a recursive behavior):
    // 1. increase value from the very beginning of an update() function
    // 2. decrease value at the end of an update() function
    // 3. propagate changed configuration to the Plater when (m_update_cnt == 0) only
    int                 m_update_cnt = 0;

public:
    Tab(wxBookCtrlBase* parent, const wxString& title, Preset::Type type);
    ~Tab() {}

	wxWindow*	parent() const { return m_parent; }
	wxString	title()	 const { return m_title; }
	std::string	name()	 const { return m_presets->name(); }
    Preset::Type type()  const { return m_type; }
    // The tab is already constructed.
    bool 		completed() const { return m_completed; }
	virtual bool supports_printer_technology(const PrinterTechnology tech) const = 0;

	void		create_preset_tab();
    void        add_scaled_button(wxWindow* parent, ScalableButton** btn, const std::string& icon_name, 
                                  const wxString& label = wxEmptyString, 
                                  long style = wxBU_EXACTFIT | wxNO_BORDER);
    void        add_scaled_bitmap(wxWindow* parent, ScalableBitmap& btn, const std::string& icon_name);
	void		update_ui_items_related_on_parent_preset(const Preset* selected_preset_parent);
    virtual void load_current_preset();
	void        rebuild_page_tree();
    void		update_btns_enabling();
    void		update_preset_choice();
    // Select a new preset, possibly delete the current one.
    // return false, if action was canceled
    bool        select_preset(std::string preset_name = "", bool delete_current = false, const std::string& last_selected_ph_printer_name = "");
	bool		may_discard_current_dirty_preset(PresetCollection* presets = nullptr, const std::string& new_printer_name = "");

    virtual void    clear_pages();
    virtual void    update_description_lines();
    virtual void    activate_selected_page(std::function<void()> throw_if_canceled);

	void		OnTreeSelChange(wxTreeEvent& event);
	void		OnKeyDown(wxKeyEvent& event);

	void		compare_preset();
	void		transfer_options(const std::string&name_from, const std::string&name_to, std::vector<std::string> options);
	void		save_preset(std::string name = std::string(), bool detach = false);
	void		rename_preset();
	void		delete_preset();
	void		toggle_show_hide_incompatible();
	void		update_compatibility_ui();
	void		update_ui_from_settings();
	void		update_label_colours();
	void		decorate();
	void		update_changed_ui();
	void		get_sys_and_mod_flags(const std::string& opt_key, bool& sys_page, bool& modified_page);
	void		update_changed_tree_ui();
	void		update_undo_buttons();

	void		on_roll_back_value(const bool to_sys = false);

    PageShp		add_options_page(const wxString& title, const std::string& icon, bool is_extruder_pages = false);
	static wxString translate_category(const wxString& title, Preset::Type preset_type);

	virtual void	OnActivate();
	virtual void	on_preset_loaded() {}
	virtual void	build() = 0;
	virtual void	update() = 0;
	virtual void	toggle_options() = 0;
	virtual void	init_options_list();
	void			emplace_option(const std::string &opt_key, bool respect_vec_values = false);
	void			load_initial_data();
	void			update_dirty();
	void			update_tab_ui();
	void			load_config(const DynamicPrintConfig& config);
	virtual void	reload_config();
    void            update_mode();
    void            update_mode_markers();
    void            update_visibility();
    virtual void    update_sla_prusa_specific_visibility() {}
    virtual void    msw_rescale();
    virtual void	sys_color_changed();
	Field*			get_field(const t_config_option_key& opt_key, int opt_index = -1) const;
	Line*			get_line(const t_config_option_key& opt_key);
	std::pair<OG_CustomCtrl*, bool*> get_custom_ctrl_with_blinking_ptr(const t_config_option_key& opt_key, int opt_index = -1);

    Field*          get_field(const t_config_option_key &opt_key, Page** selected_page, int opt_index = -1);
	void			toggle_option(const std::string& opt_key, bool toggle, int opt_index = -1);
	wxSizer*		description_line_widget(wxWindow* parent, ogStaticText** StaticText, wxString text = wxEmptyString);
	bool			current_preset_is_dirty() const;
	bool			saved_preset_is_dirty() const;
	void            update_saved_preset_from_current_preset();

	DynamicPrintConfig*	get_config() { return m_config; }
	PresetCollection*	get_presets() { return m_presets; }
	const PresetCollection* get_presets() const { return m_presets; }

	void			on_value_change(const std::string& opt_key, const boost::any& value);

    void            update_wiping_button_visibility();
	void			activate_option(const std::string& opt_key, const wxString& category);
	void			cache_config_diff(const std::vector<std::string>& selected_options, const DynamicPrintConfig* config = nullptr);
	void			apply_config_from_cache();

	const std::map<wxString, std::string>& get_category_icon_map() { return m_category_icon; }

	static bool validate_custom_gcode(const wxString& title, const std::string& gcode);
	bool        validate_custom_gcodes();
    bool        validate_custom_gcodes_was_shown{ false };
    bool        is_prusa_printer() const;

    void						edit_custom_gcode(const t_config_option_key& opt_key);
    virtual const std::string&	get_custom_gcode(const t_config_option_key& opt_key);
    virtual void				set_custom_gcode(const t_config_option_key& opt_key, const std::string& value);
protected:
	void			create_line_with_widget(ConfigOptionsGroup* optgroup, const std::string& opt_key, const std::string& path, widget_t widget);
	wxSizer*		compatible_widget_create(wxWindow* parent, PresetDependencies &deps);
	void 			compatible_widget_reload(PresetDependencies &deps);
	void			load_key_value(const std::string& opt_key, const boost::any& value, bool saved_value = false);

	// return true if cancelled
	bool			tree_sel_change_delayed();
	void			on_presets_changed();
	void			build_preset_description_line(ConfigOptionsGroup* optgroup);
	void			update_preset_description_line();
	void			update_frequently_changed_parameters();
	void			fill_icon_descriptions();
	void			set_tooltips_text();

    virtual bool    select_preset_by_name(const std::string& name_w_suffix, bool force);
    virtual bool    save_current_preset(const std::string& new_name, bool detach);
    virtual bool    delete_current_preset();

    ConfigManipulation m_config_manipulation;
    ConfigManipulation get_config_manipulation();
};

class TabPrint : public Tab
{
public:
	TabPrint(wxBookCtrlBase* parent) :
        Tab(parent, _(L("Print Settings")), Slic3r::Preset::TYPE_PRINT) {}
	~TabPrint() {}

	void		build() override;
	void		update_description_lines() override;
	void		toggle_options() override;
	void		update() override;
	void		clear_pages() override;
	bool 		supports_printer_technology(const PrinterTechnology tech) const override { return tech == ptFFF; }
	wxSizer*	create_manage_substitution_widget(wxWindow* parent);
	wxSizer*	create_substitutions_widget(wxWindow* parent);

private:
	ogStaticText*	m_recommended_thin_wall_thickness_description_line = nullptr;
	ogStaticText*	m_top_bottom_shell_thickness_explanation = nullptr;
	ogStaticText*	m_post_process_explanation = nullptr;
	ScalableButton* m_del_all_substitutions_btn{nullptr};
	SubstitutionManager m_subst_manager;
};

class TabFilament : public Tab
{
    BitmapComboBox* m_extruders_cb {nullptr};
    int             m_active_extruder {0};
	ogStaticText*	m_volumetric_speed_description_line {nullptr};
	ogStaticText*	m_cooling_description_line {nullptr};

    void            create_line_with_near_label_widget(ConfigOptionsGroupShp optgroup, const std::string &opt_key, int opt_index = 0);
    void            update_line_with_near_label_widget(ConfigOptionsGroupShp optgroup, const std::string &opt_key, int opt_index = 0, bool is_checked = true);
    void            add_filament_overrides_page();
    void            update_filament_overrides_page();
    void            create_extruder_combobox();
	void 			update_volumetric_flow_preset_hints();

    std::map<std::string, wxWindow*> m_overrides_options;
public:
	TabFilament(wxBookCtrlBase* parent) :
		Tab(parent, _L("Filaments"), Slic3r::Preset::TYPE_FILAMENT) {}
	~TabFilament() {}

	void		build() override;
	void		update_description_lines() override;
	void		toggle_options() override;
	void		update() override;
	void		clear_pages() override;
	void        msw_rescale() override;
	void		sys_color_changed() override;
	bool 		supports_printer_technology(const PrinterTechnology tech) const override { return tech == ptFFF; }
    void        load_current_preset() override;

    // set actiev extruder and update preset combobox if needed
    // return false, if new preset wasn't selected
    bool        set_active_extruder(int new_selected_extruder);
    void        invalidate_active_extruder() { m_active_extruder = -1; }
    void        update_extruder_combobox();
    void        update_extruder_combobox_visibility();
    int         get_active_extruder() const { return m_active_extruder; }

	const std::string&	get_custom_gcode(const t_config_option_key& opt_key) override;
	void				set_custom_gcode(const t_config_option_key& opt_key, const std::string& value) override;

protected:
    bool        select_preset_by_name(const std::string& name_w_suffix, bool force) override;
    bool        save_current_preset(const std::string& new_name, bool detach) override;
    bool        delete_current_preset() override;
};

class TabPrinter : public Tab
{
private:
	bool		m_has_single_extruder_MM_page = false;
	bool		m_use_silent_mode = false;
    bool        m_supports_travel_acceleration = false;
	bool        m_supports_min_feedrates = false;
	void		append_option_line(ConfigOptionsGroupShp optgroup, const std::string opt_key);
	bool		m_rebuild_kinematics_page = false;
	ogStaticText* m_machine_limits_description_line {nullptr};
	void 		update_machine_limits_description(const MachineLimitsUsage usage);

	ogStaticText*	m_fff_print_host_upload_description_line {nullptr};
	ogStaticText*	m_sla_print_host_upload_description_line {nullptr};

    std::vector<PageShp>			m_pages_fff;
    std::vector<PageShp>			m_pages_sla;

public:
	size_t		m_extruders_count;
	size_t		m_extruders_count_old = 0;
	size_t		m_initial_extruders_count;
	size_t		m_sys_extruders_count;
	size_t		m_cache_extruder_count = 0;

    PrinterTechnology               m_printer_technology = ptFFF;

    TabPrinter(wxBookCtrlBase* parent) :
        Tab(parent, _L("Printers"), Slic3r::Preset::TYPE_PRINTER) {}
	~TabPrinter() {}

	void		build() override;
	void		build_print_host_upload_group(Page* page);
    void		build_fff();
    void		build_sla();
	void		reload_config() override;
	void		activate_selected_page(std::function<void()> throw_if_canceled) override;
	void		clear_pages() override;
	void		toggle_options() override;
    void		update() override;
    void		update_fff();
    void		update_sla();
    void        update_pages(); // update m_pages according to printer technology
	void		extruders_count_changed(size_t extruders_count);
	PageShp		build_kinematics_page();
	void		build_extruder_pages(size_t n_before_extruders);
	void		build_unregular_pages(bool from_initial_build = false);
	void		on_preset_loaded() override;
	void		init_options_list() override;
	bool 		supports_printer_technology(const PrinterTechnology /* tech */) const override { return true; }

	wxSizer*	create_bed_shape_widget(wxWindow* parent);
	void		cache_extruder_cnt(const DynamicPrintConfig* config = nullptr);
	bool		apply_extruder_cnt_from_cache();
	void		update_sla_prusa_specific_visibility() override;
};

class TabSLAMaterial : public Tab
{
	void		create_line_with_near_label_widget(ConfigOptionsGroupShp optgroup, const std::string& opt_key);
	void		update_line_with_near_label_widget(ConfigOptionsGroupShp optgroup, const std::string& opt_key, bool is_checked = true);
	void		add_material_overrides_page();
	void		update_material_overrides_page();

	std::map<std::string, wxWindow*> m_overrides_options;
	ogStaticText*	m_z_correction_to_mm_description = nullptr;

public:
    TabSLAMaterial(wxBookCtrlBase* parent) :
		Tab(parent, _L("Materials"), Slic3r::Preset::TYPE_SLA_MATERIAL) {}
    ~TabSLAMaterial() {}

	void		build() override;
	void		build_tilt_group(Slic3r::GUI::PageShp page);
	void		toggle_tilt_options(bool is_above);
	void		toggle_options() override;
	void		update() override;
	void		clear_pages() override;
	void        msw_rescale() override;
	void		sys_color_changed() override;
	bool 		supports_printer_technology(const PrinterTechnology tech) const override { return tech == ptSLA; }
	void		update_sla_prusa_specific_visibility() override;
    void		update_description_lines() override;
};

class TabSLAPrint : public Tab
{
    // Methods are a vector of method prefix -> method label pairs
    // method prefix is the prefix whith which all the config values are prefixed
    // for a particular method. The label is the friendly name for the method
    void build_sla_support_params(const std::vector<SamePair<std::string>> &methods,
                                  const Slic3r::GUI::PageShp &page);

public:
    TabSLAPrint(wxBookCtrlBase* parent) :
        Tab(parent, _(L("Print Settings")), Slic3r::Preset::TYPE_SLA_PRINT) {}
    ~TabSLAPrint() {}

	ogStaticText* m_support_object_elevation_description_line = nullptr;

    void		build() override;
	void		update_description_lines() override;
	void		toggle_options() override;
    void		update() override;
	void		clear_pages() override;
	bool 		supports_printer_technology(const PrinterTechnology tech) const override { return tech == ptSLA; }
};

} // GUI
} // Slic3r

#endif /* slic3r_Tab_hpp_ */
