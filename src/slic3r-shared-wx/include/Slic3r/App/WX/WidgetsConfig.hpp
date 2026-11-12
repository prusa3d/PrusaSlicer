#pragma once

// ! Singleton Class  

#include <wx/colour.h>
#include <wx/font.h>
#include <wx/settings.h>

#include <string>
#include <vector>

// Translate the ifdef 
#ifdef __WXOSX__
#define wxOSX true
#else
#define wxOSX false
#endif 
#ifdef __WXGTK3__
#define wxGTK3 true
#else
#define wxGTK3 false
#endif

class wxTopLevelWindow;
class wxWindow;
class wxDialog;
class wxDataViewCtrl;
class wxButton;

namespace Slic3r::App::WX {

class WidgetsConfig {

	wxColour        m_color_label_modified;
	wxColour        m_color_label_sys;
	wxColour        m_color_label_default;
	wxColour        m_color_window_default;

	wxColour        m_color_highlight_label_default;
	wxColour        m_color_hovered_btn_label;
	wxColour        m_color_default_btn_label;
	wxColour        m_color_highlight_default;
	wxColour        m_color_hovered_btn_bg;
	wxColour        m_color_selected_btn_bg;

	wxFont		    m_small_font;
	wxFont		    m_bold_font;
	wxFont			m_normal_font;
	wxFont			m_code_font;
	wxFont		    m_link_font;

    std::vector<std::string>     m_mode_palette;

    int             m_em_unit{ 10 };    // width of a "m"-symbol in pixels for current system font
	                                    // Note: for 100% Scale m_em_unit = 10 -> it's a good enough coefficient for a size setting of controls

    bool            m_is_dark{ true };


    WidgetsConfig(bool is_dark, bool is_sys_menu);

	static WidgetsConfig* m_wc_instancePtr;

    void update_dark_children_ui(wxWindow* window, bool just_buttons_update = false);

public:

	WidgetsConfig(const WidgetsConfig& obj) = delete;

    static WidgetsConfig* instance()
    {
        if (m_wc_instancePtr == NULL)
            m_wc_instancePtr = new WidgetsConfig(false, false);
        return m_wc_instancePtr;
    }

	static WidgetsConfig* instance(bool is_dark/* = false*/, bool is_sys_menu/* = false*/)
	{
		if (m_wc_instancePtr == NULL)
            m_wc_instancePtr = new WidgetsConfig(is_dark, is_sys_menu);
		return m_wc_instancePtr;
	}

    void            init_fonts();
    void            init_ui_colours();

	void            update_fonts(const wxFont& normal_font, const int em);
	void            force_fonts_update(wxWindow* win, bool apply_for_children = false);

    void            force_colors_update(const bool is_dark, const std::vector<wxWindow*>& wins);

    static unsigned get_colour_approx_luma(const wxColour &colour);

    static wxTopLevelWindow* find_toplevel_parent(wxWindow* window);

    const wxColour  get_label_default_clr_system();
    const wxColour  get_label_default_clr_modified();
    void            set_label_clr_sys(const wxColour& clr);
    void            set_label_clr_modified(const wxColour& clr);

    const std::vector<std::string> get_mode_default_palette();

    // update color mode for whole dialog including all children
    void            UpdateDlgDarkUI(wxDialog* dlg, bool just_buttons_update = false);
    void            SetWindowVariantForButton(wxButton* btn);

    const std::string       get_html_bg_color(wxWindow* html_parent);

    const std::string&      get_mode_btn_color(int mode_id);
    std::vector<wxColour>   get_mode_palette();
    // return true when palette was changed and those changes have to be saved in AppConfig
    bool                    set_mode_palette(const std::vector<wxColour> &palette);

    void                    set_dark_mode(bool is_dark) { m_is_dark = is_dark; }
    void                    switch_mode()               { m_is_dark = !m_is_dark; }

    const wxColour& get_label_clr_modified()        { return m_color_label_modified; }
    const wxColour& get_label_clr_sys()             { return m_color_label_sys; }
    const wxColour& get_label_clr_default()         { return m_color_label_default; }
    const wxColour& get_window_default_clr()        { return m_color_window_default; }

    const wxColour& get_label_highlight_clr()       { return m_color_highlight_label_default; }
    const wxColour& get_highlight_default_clr()     { return m_color_highlight_default; }
    const wxColour& get_color_hovered_btn_label()   { return m_color_hovered_btn_label; }
    const wxColour& get_color_selected_btn_bg()     { return m_color_selected_btn_bg; }
    const wxColour& get_color_hovered_btn_bg()      { return m_color_hovered_btn_bg; }

    const wxFont&   small_font()                    { return m_small_font; }
    const wxFont&   bold_font()                     { return m_bold_font; }
    const wxFont&   normal_font()                   { return m_normal_font; }
    const wxFont&   code_font()                     { return m_code_font; }
    const wxFont&   link_font()                     { return m_link_font; }

    int             em_unit(wxWindow* win) const;
    int             em_unit() const                 { return m_em_unit; }
    bool            dark_mode() const               { return m_is_dark; }

    // Platform specific Ctrl+/Alt+ (Windows, Linux) vs. ⌘/⌥ (OSX) prefixes 
    const std::string& shortkey_ctrl_prefix();
    const std::string& shortkey_alt_prefix();

    bool            suppress_round_corners() const { return true;/* app_config->get("suppress_round_corners") == "1";*/ }
};

WidgetsConfig* w_config();

} // Slic3r::App::WX

