///|/ Copyright (c) Prusa Research 2018 - 2023 Oleksandra Iushchenko @YuSanka, David Kocík @kocikdav, Vojtěch Bubník @bubnikv, Enrico Turri @enricoturri1966
///|/ Copyright (c) 2021 Jurriaan Pruis
///|/
///|/ ported from lib/Slic3r/GUI/Preferences.pm:
///|/ Copyright (c) Prusa Research 2016 - 2018 Vojtěch Bubník @bubnikv
///|/ Copyright (c) Slic3r 2013 - 2014 Alessandro Ranellucci @alranel
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef slic3r_Preferences_hpp_
#define slic3r_Preferences_hpp_

#include "GUI.hpp"
#include "GUI_Utils.hpp"
#include "wxExtensions.hpp"

#include <wx/dialog.h>
#include <wx/timer.h>
#include <vector>
#include <map>

class wxColourPickerCtrl;
class wxBookCtrlBase;
class wxSlider;
class wxRadioButton;

namespace Slic3r {

	enum  NotifyReleaseMode {
		NotifyReleaseAll,
		NotifyReleaseOnly,
		NotifyReleaseNone
	};

namespace GUI {

class ConfigOptionsGroup;
class OG_CustomCtrl;

namespace DownloaderUtils {
	class Worker;
}

class PreferencesDialog : public DPIDialog
{
	std::map<std::string, std::string>	m_values;
	std::shared_ptr<ConfigOptionsGroup>	m_optgroup_general;
	std::shared_ptr<ConfigOptionsGroup>	m_optgroup_camera;
	std::shared_ptr<ConfigOptionsGroup>	m_optgroup_gui;
	std::shared_ptr<ConfigOptionsGroup>	m_optgroup_other;
#ifdef _WIN32
	std::shared_ptr<ConfigOptionsGroup>	m_optgroup_dark_mode;
#endif //_WIN32
#if ENABLE_ENVIRONMENT_MAP
	std::shared_ptr<ConfigOptionsGroup>	m_optgroup_render;
#endif // ENABLE_ENVIRONMENT_MAP
	wxSizer*                            m_icon_size_sizer {nullptr};
	wxSlider*							m_icon_size_slider {nullptr};
	wxRadioButton*						m_rb_old_settings_layout_mode {nullptr};
	wxRadioButton*						m_rb_dlg_settings_layout_mode {nullptr};

	wxColourPickerCtrl*					m_sys_colour {nullptr};
	wxColourPickerCtrl*					m_mod_colour {nullptr};

	std::vector<wxColour>				m_mode_palette;
	wxColourPickerCtrl*					m_mode_simple    { nullptr };
	wxColourPickerCtrl*					m_mode_advanced  { nullptr };
	wxColourPickerCtrl*					m_mode_expert    { nullptr };

	DownloaderUtils::Worker*			downloader { nullptr };

	wxBookCtrlBase*						tabs {nullptr};

    bool                                isOSX {false};
	bool								m_settings_layout_changed {false};
	bool								m_seq_top_layer_only_changed{ false };
	bool								m_recreate_GUI{false};

	int									m_custom_toolbar_size{-1};
	bool								m_use_custom_toolbar_size{false};

public:
	explicit PreferencesDialog(wxWindow* paren);
	~PreferencesDialog() = default;

	bool settings_layout_changed() const { return m_settings_layout_changed; }
	bool seq_top_layer_only_changed() const { return m_seq_top_layer_only_changed; }
	bool recreate_GUI() const { return m_recreate_GUI; }
	void	build();
	void	update_ctrls_alignment();
	void	accept(wxEvent&);
	void    revert(wxEvent&);
	void	show(const std::string& highlight_option = std::string(), const std::string& tab_name = std::string());

protected:
	void msw_rescale();
	void on_dpi_changed(const wxRect& suggested_rect) override { msw_rescale(); }
	void on_sys_color_changed() override;
    void layout();
	void clear_cache();
	void refresh_og(std::shared_ptr<ConfigOptionsGroup> og);
	void refresh_og(ConfigOptionsGroup* og);
    void create_icon_size_slider();
    void create_settings_mode_widget();
    void create_settings_text_color_widget();
    void create_settings_mode_color_widget();
    void create_settings_font_widget();
    void create_downloader_path_sizer();
	void init_highlighter(const t_config_option_key& opt_key);
	std::vector<ConfigOptionsGroup*> optgroups();

	HighlighterForWx						m_highlighter;
	std::map<std::string, BlinkingBitmap*>	m_blinkers;
};

} // GUI
} // Slic3r


#endif /* slic3r_Preferences_hpp_ */
