#include "Slic3r/App/WX/WidgetsConfig.hpp"

#include "Slic3r/Domain/Color.hpp"

#include "Slic3r/Biz/Algorithms/Color.hpp"

#include "Slic3r/App/WX/StringConversions.hpp"
#include "Slic3r/App/Theme.hpp"
#include "Slic3r/App/AppServices.hpp"

#include <wx/window.h>
#include <wx/toplevel.h>
#include <wx/scrolwin.h>
#include <wx/tooltip.h>
#include <wx/treectrl.h>
#include <wx/stattext.h>
#include <wx/dataview.h>
#include <wx/dialog.h>
#include <wx/button.h>
#include <wx/listbox.h>
#include <wx/checklst.h>
#include <wx/ownerdrw.h>
//#include <wx/.h>



using Slic3r::Domain::ColorRGB;
using Slic3r::Domain::ColorRGBA;

using Slic3r::Biz::Algorithms::Color::encode_color;

namespace Slic3r::App::WX {

WidgetsConfig* WidgetsConfig::m_wc_instancePtr{ nullptr };

WidgetsConfig* w_config()
{
    return WidgetsConfig::instance();
}

WidgetsConfig::WidgetsConfig(bool is_dark, bool is_sys_menu) : m_is_dark(is_dark)
{
    // initialize label colors and fonts
    init_ui_colours();
    init_fonts();
}

void WidgetsConfig::init_fonts()
{
    m_small_font    = wxSystemSettings::GetFont(wxSYS_DEFAULT_GUI_FONT);
    m_bold_font     = wxSystemSettings::GetFont(wxSYS_DEFAULT_GUI_FONT).Bold();
    m_normal_font   = wxSystemSettings::GetFont(wxSYS_DEFAULT_GUI_FONT);

#ifdef __WXMAC__
    m_small_font.SetPointSize(11);
    m_bold_font.SetPointSize(13);
#endif /*__WXMAC__*/

    // wxSYS_OEM_FIXED_FONT and wxSYS_ANSI_FIXED_FONT use the same as
    // DEFAULT in wxGtk. Use the TELETYPE family as a work-around
    m_code_font = wxFont(wxFontInfo().Family(wxFONTFAMILY_TELETYPE));
    m_code_font.SetPointSize(m_normal_font.GetPointSize());
}

void WidgetsConfig::update_fonts(const wxFont& normal_font, const int em)
{
    /* Only normal and bold fonts are used for an application rescale,
     * because of under MSW small and normal fonts are the same.
     * To avoid same rescaling twice, just fill this values
     * from rescaled MainFrame
     */
    m_normal_font = normal_font;
    m_small_font  = normal_font;
    m_bold_font   = normal_font.Bold();
    m_link_font   = m_bold_font.Underlined();

    m_code_font.SetPointSize(m_normal_font.GetPointSize());

    m_em_unit = em;
}

static void set_new_font(wxWindow* win, bool apply_for_children)
{
    wxWindowList& children = win->GetChildren();
    if (apply_for_children) {
        for (auto child : children)
            set_new_font(child, apply_for_children);
    }

    if (win->GetFont().GetWeight() == wxFONTWEIGHT_BOLD)
        win->SetFont(w_config()->bold_font());
    else
        win->SetFont(w_config()->normal_font());

    if (apply_for_children && !children.IsEmpty()) {
        win->Layout();
        win->Refresh();
    }
}

void WidgetsConfig::force_fonts_update(wxWindow* win, bool apply_for_children/* = false*/)
{
    set_new_font(win, apply_for_children);
}

void WidgetsConfig::init_ui_colours()
{
    auto to_wx = [](const Domain::ColorRGBA& color) -> wxColour {
        return {color.r_uchar(), color.g_uchar(), color.b_uchar()};
    };

    const Theme& theme = AppServices::instance().theme();

    m_color_label_modified = get_label_default_clr_modified();
    m_color_label_sys      = get_label_default_clr_system();
    m_mode_palette         = get_mode_default_palette();

    m_color_label_default           = m_is_dark ? wxColour(250, 250, 250) : wxColour(12, 12, 12);
    m_color_highlight_label_default = m_is_dark ? wxColour(230, 230, 230) : wxColour(12, 12, 12);
    m_color_highlight_default       = m_is_dark ? wxColour(78, 78, 78) : wxColour(190, 200, 215);
    m_color_hovered_btn_label       = m_is_dark ? wxColour(253, 111, 40) : wxColour(252, 77, 1);
    m_color_default_btn_label       = m_is_dark ? wxColour(255, 181, 100) : wxColour(203, 61, 0);
    m_color_hovered_btn_bg =
        to_wx(theme.color(Platform::Color::Button, Platform::ColorGroup::Hovered));
    m_color_selected_btn_bg =
        to_wx(theme.color(Platform::Color::Button, Platform::ColorGroup::Active));

    m_color_window_default = m_is_dark ? wxColour(43, 43, 43) : wxColour(234, 234, 234);
}

void WidgetsConfig::force_colors_update(const bool is_dark, const std::vector<wxWindow*>& wins )
{
    m_is_dark = is_dark;
    
    init_ui_colours();
}


unsigned WidgetsConfig::get_colour_approx_luma(const wxColour& colour)
{
    double r = colour.Red();
    double g = colour.Green();
    double b = colour.Blue();

    return std::round(std::sqrt(
        r * r * .241 +
        g * g * .691 +
        b * b * .068
    ));
}

const wxColour WidgetsConfig::get_label_default_clr_system()
{
    return m_is_dark ? wxColour(115, 220, 103) : wxColour(26, 132, 57);
}

const wxColour WidgetsConfig::get_label_default_clr_modified()
{
    return m_is_dark ? wxColour(253, 111, 40) : wxColour(252, 77, 1);
}

const std::vector<std::string> WidgetsConfig::get_mode_default_palette()
{
    return { "#7DF028", "#FFDC00", "#E70000" };
}

void WidgetsConfig::set_label_clr_modified(const wxColour& clr)
{
    if (m_color_label_modified == clr)
        return;
    m_color_label_modified = clr;
}

void WidgetsConfig::set_label_clr_sys(const wxColour& clr)
{
    if (m_color_label_sys == clr)
        return;
    m_color_label_sys = clr;
}

const std::string WidgetsConfig::get_html_bg_color(wxWindow* html_parent)
{
    wxColour    bgr_clr = html_parent->GetBackgroundColour();
#ifdef __APPLE__
    // On macOS 10.13 and older the background color returned by wxWidgets
    // is wrong, which leads to https://github.com/prusa3d/PrusaSlicer/issues/7603
    // and https://github.com/prusa3d/PrusaSlicer/issues/3775. wxSYS_COLOUR_WINDOW
    // may not match the window background exactly, but it seems to never end up
    // as black on black.

    if (wxPlatformInfo::Get().GetOSMajorVersion() == 10
        && wxPlatformInfo::Get().GetOSMinorVersion() < 14)
        bgr_clr = wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW);
#endif

    return encode_color(ColorRGB(bgr_clr.Red(), bgr_clr.Green(), bgr_clr.Blue()));
}

const std::string& WidgetsConfig::get_mode_btn_color(int mode_id)
{
    assert(0 <= mode_id && size_t(mode_id) < m_mode_palette.size());
    return m_mode_palette[mode_id];
}

std::vector<wxColour> WidgetsConfig::get_mode_palette()
{
    return {
        wxColor(from_u8(m_mode_palette[0])),
        wxColor(from_u8(m_mode_palette[1])),
        wxColor(from_u8(m_mode_palette[2]))
    };
}

bool WidgetsConfig::set_mode_palette(const std::vector<wxColour>& palette)
{
    bool to_save = false;

    for (size_t mode = 0; mode < palette.size(); ++mode) {
        const wxColour& clr = palette[mode];
        std::string color_str = clr == wxTransparentColour ? std::string("") : encode_color(ColorRGB(clr.Red(), clr.Green(), clr.Blue()));
        if (m_mode_palette[mode] != color_str) {
            m_mode_palette[mode] = color_str;
            to_save = true;
        }
    }

    return to_save;
}

/* Function for getting of em_unit value from correct parent.
 * In most of cases it is m_em_unit value from WidgetsConfig,
 * but for Dialogs it's its own value.
 * This value will be used to correct rescale after moving between
 * Displays with different HDPI */
int WidgetsConfig::em_unit(wxWindow* win) const
{
    if (win)
    {
        wxTopLevelWindow* toplevel = w_config()->find_toplevel_parent(win);
        float sf = toplevel->GetDPIScaleFactor();

        // On Retina displays we need to respect content scale factor too
        float csf = toplevel->GetContentScaleFactor();

        return int(sf / csf * 10.);
    }
    return m_em_unit;
}

const std::string& WidgetsConfig::shortkey_ctrl_prefix()
{
    static const std::string str =
#ifdef __APPLE__
        "⌘"
#else
        "Ctrl+"
#endif
        ;
    return str;
}

const std::string& WidgetsConfig::shortkey_alt_prefix()
{
    static const std::string str =
#ifdef __APPLE__
        "⌥"
#else
        "Alt+"
#endif
        ;
    return str;
}

[[maybe_unused]] static bool is_default(wxWindow* win)
{
    wxTopLevelWindow* tlw = WidgetsConfig::find_toplevel_parent(win);
    if (!tlw)
        return false;

    return win == tlw->GetDefaultItem();
}

// recursive function for scaling fonts for all controls in Window
void WidgetsConfig::update_dark_children_ui(wxWindow* window, bool just_buttons_update/* = false*/)
{
    auto children = window->GetChildren();
    for (auto child : children) {
        update_dark_children_ui(child);
    }
}

// Note: Don't use this function for Dialog contains ScalableButtons
void WidgetsConfig::UpdateDlgDarkUI(wxDialog* dlg, bool just_buttons_update/* = false*/)
{
    update_dark_children_ui(dlg, just_buttons_update);
}

void WidgetsConfig::SetWindowVariantForButton(wxButton* btn)
{
#ifdef __APPLE__
    // This is a limit imposed by OSX. The way the native button widget is drawn only allows it to be stretched horizontally,
    // and the vertical size is fixed. (see https://stackoverflow.com/questions/29083891/wxpython-button-size-being-ignored-on-osx)
    // But standard height is possible to change using SetWindowVariant method (see https://docs.wxwidgets.org/3.0/window_8h.html#a879bccd2c987fedf06030a8abcbba8ac)
    if (m_normal_font.GetPointSize() > 15) {
        btn->SetWindowVariant(wxWINDOW_VARIANT_LARGE);
        btn->SetFont(m_normal_font);
    }
#endif
}

wxTopLevelWindow* WidgetsConfig::find_toplevel_parent(wxWindow* window)
{
    for (; window != nullptr; window = window->GetParent()) {
        if (window->IsTopLevel()) {
            return dynamic_cast<wxTopLevelWindow*>(window);
        }
    }

    return nullptr;
}



} //namespace Slic3r::App::WX