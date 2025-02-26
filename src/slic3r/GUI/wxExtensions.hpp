///|/ Copyright (c) Prusa Research 2018 - 2023 Oleksandra Iushchenko @YuSanka, Lukáš Hejl @hejllukas, Enrico Turri @enricoturri1966, David Kocík @kocikdav, Vojtěch Bubník @bubnikv, Tomáš Mészáros @tamasmeszaros, Lukáš Matěna @lukasmatena, Vojtěch Král @vojtechkral
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef slic3r_GUI_wxExtensions_hpp_
#define slic3r_GUI_wxExtensions_hpp_

#include <wx/checklst.h>
#include <wx/combo.h>
#include <wx/dataview.h>
#include <wx/button.h>
#include <wx/sizer.h>
#include <wx/menu.h>
#include <wx/bmpcbox.h>
#include <wx/bmpbndl.h>
#include <wx/statbmp.h>
#include <wx/timer.h>

#include <vector>
#include <functional>
#include <boost/filesystem.hpp>


#ifndef __linux__
void                sys_color_changed_menu(wxMenu* menu);
#else 
inline void         sys_color_changed_menu(wxMenu* /* menu */) {}
#endif // no __linux__

#ifndef __APPLE__
// Caching wxAcceleratorEntries to use them during mainframe creation
std::vector<wxAcceleratorEntry*>& accelerator_entries_cache();
#endif // no __APPLE__

wxMenuItem* append_menu_item(wxMenu* menu, int id, const wxString& string, const wxString& description,
    std::function<void(wxCommandEvent& event)> cb, wxBitmapBundle* icon, wxEvtHandler* event_handler = nullptr,
    std::function<bool()> const cb_condition = []() { return true;}, wxWindow* parent = nullptr, int insert_pos = wxNOT_FOUND);
wxMenuItem* append_menu_item(wxMenu* menu, int id, const wxString& string, const wxString& description,
    std::function<void(wxCommandEvent& event)> cb, const std::string& icon = "", wxEvtHandler* event_handler = nullptr,
    std::function<bool()> const cb_condition = []() { return true; }, wxWindow* parent = nullptr, int insert_pos = wxNOT_FOUND);

wxMenuItem* append_submenu(wxMenu* menu, wxMenu* sub_menu, int id, const wxString& string, const wxString& description,
    const std::string& icon = "",
    std::function<bool()> const cb_condition = []() { return true; }, wxWindow* parent = nullptr);

wxMenuItem* append_menu_radio_item(wxMenu* menu, int id, const wxString& string, const wxString& description,
    std::function<void(wxCommandEvent& event)> cb, wxEvtHandler* event_handler);

wxMenuItem* append_menu_check_item(wxMenu* menu, int id, const wxString& string, const wxString& description,
    std::function<void(wxCommandEvent & event)> cb, wxEvtHandler* event_handler,
    std::function<bool()> const enable_condition = []() { return true; }, 
    std::function<bool()> const check_condition = []() { return true; }, wxWindow* parent = nullptr);

void enable_menu_item(wxUpdateUIEvent& evt, std::function<bool()> const cb_condition, wxMenuItem* item, wxWindow* win);

void set_menu_item_bitmap(wxMenuItem* item, const std::string& icon_name);

class wxDialog;

void    msw_buttons_rescale(wxDialog* dlg, const int em_unit, const std::vector<int>& btn_ids, double height_koef = 1.);
int     em_unit(wxWindow* win);
int     mode_icon_px_size();

wxBitmapBundle* get_bmp_bundle(const std::string& bmp_name, int width = 16, int height = -1, const std::string& new_color_rgb = std::string());
wxBitmapBundle* get_empty_bmp_bundle(int width, int height);
wxBitmapBundle* get_solid_bmp_bundle(int width, int height, const std::string& color);

std::vector<wxBitmapBundle*> get_extruder_color_icons(bool thin_icon = false);

namespace Slic3r {
namespace GUI {
class BitmapComboBox;
}
}
void apply_extruder_selector(Slic3r::GUI::BitmapComboBox** ctrl,
                             wxWindow* parent,
                             const std::string& first_item = "",
                             wxPoint pos = wxDefaultPosition,
                             wxSize size = wxDefaultSize,
                             bool use_thin_icon = false);

inline wxSize get_preferred_size(const wxBitmapBundle& bmp, wxWindow* parent)
{
    if (!bmp.IsOk())
        return wxSize(0,0);
#ifdef __WIN32__
    return bmp.GetPreferredBitmapSizeFor(parent);
#else
    return bmp.GetDefaultSize();
#endif
}


// ----------------------------------------------------------------------------
// ScalableBitmap
// ----------------------------------------------------------------------------

class ScalableBitmap
{
public:
    ScalableBitmap() {};
    ScalableBitmap( wxWindow *parent,
                    const std::string& icon_name,
                    const int  width = 16,
                    const int  height = -1 ,
                    const bool grayscale = false);

    ScalableBitmap( wxWindow *parent,
                    const std::string& icon_name,
                    const  wxSize icon_size,
                    const bool grayscale = false);

    ScalableBitmap( wxWindow                    *parent,
                    boost::filesystem::path&    icon_path,
                    const  wxSize               icon_size);

    ~ScalableBitmap() {}

    void                sys_color_changed();

    const wxBitmapBundle& bmp()   const { return m_bmp; }
    wxBitmap            get_bitmap()    { return m_bmp.GetBitmapFor(m_parent); }
    wxWindow*           parent()  const { return m_parent;}
    const std::string&  name()    const { return m_icon_name; }
    wxSize              px_size()  const { return wxSize(m_bmp_width, m_bmp_height);}

    void                SetBitmap(const wxBitmapBundle& bmp) { m_bmp = bmp; }
    wxSize              GetSize()   const { return get_preferred_size(m_bmp, m_parent); }
    int                 GetWidth()  const { return GetSize().GetWidth(); }
    int                 GetHeight() const { return GetSize().GetHeight(); }
    bool                IsOk()      const { return m_bmp.IsOk(); }

private:
    wxWindow*       m_parent{ nullptr };
    wxBitmapBundle  m_bmp = wxBitmapBundle();
    wxBitmap        m_bitmap = wxBitmap();
    std::string     m_icon_name = "";
    int             m_bmp_width{ 16 };
    int             m_bmp_height{ -1 };
};


// ----------------------------------------------------------------------------
// LockButton
// ----------------------------------------------------------------------------

class LockButton : public wxButton
{
public:
    LockButton(
        wxWindow *parent,
        wxWindowID id,
        const wxPoint& pos = wxDefaultPosition,
        const wxSize& size = wxDefaultSize);
    ~LockButton() {}

    void    OnButton(wxCommandEvent& event);

    bool    IsLocked() const                { return m_is_pushed; }
    void    SetLock(bool lock);

    // create its own Enable/Disable functions to not really disabled button because of tooltip enabling
    void    enable()                        { m_disabled = false; }
    void    disable()                       { m_disabled = true;  }

    void    sys_color_changed();

protected:
    void    update_button_bitmaps();

private:
    bool        m_is_pushed = false;
    bool        m_disabled = false;

    ScalableBitmap    m_bmp_lock_closed;
    ScalableBitmap    m_bmp_lock_closed_f;
    ScalableBitmap    m_bmp_lock_open;
    ScalableBitmap    m_bmp_lock_open_f;
};


// ----------------------------------------------------------------------------
// ScalableButton
// ----------------------------------------------------------------------------

class ScalableButton : public wxButton
{
public:
    ScalableButton(){}
    ScalableButton(
        wxWindow *          parent,
        wxWindowID          id,
        const std::string&  icon_name = "",
        const wxString&     label = wxEmptyString,
        const wxSize&       size = wxDefaultSize,
        const wxPoint&      pos = wxDefaultPosition,
        long                style = wxBU_EXACTFIT | wxNO_BORDER,
        int                 width = 16, 
        int                 height = -1);

    ScalableButton(
        wxWindow *          parent,
        wxWindowID          id,
        const ScalableBitmap&  bitmap,
        const wxString&     label = wxEmptyString,
        long                style = wxBU_EXACTFIT | wxNO_BORDER);

    ~ScalableButton() {}

    void SetBitmap_(const ScalableBitmap& bmp);
    bool SetBitmap_(const std::string& bmp_name);
    void SetBitmapDisabled_(const ScalableBitmap &bmp);
    int  GetBitmapHeight();
    wxSize  GetBitmapSize();

    virtual void    sys_color_changed();

private:
    wxWindow*       m_parent { nullptr };
    std::string     m_current_icon_name;
    std::string     m_disabled_icon_name;
    int             m_width {-1}; // should be multiplied to em_unit
    int             m_height{-1}; // should be multiplied to em_unit

protected:
    // bitmap dimensions 
    int             m_bmp_width{ 16 };
    int             m_bmp_height{ -1 };
    bool            m_has_border {false};
};


// ----------------------------------------------------------------------------
// BlinkingBitmap
// ----------------------------------------------------------------------------

class BlinkingBitmap : public wxStaticBitmap
{
public:
    BlinkingBitmap() {};
    BlinkingBitmap(wxWindow* parent, const std::string& icon_name = "search_blink");

    ~BlinkingBitmap() {}

    void    invalidate();
    void    activate();
    void    blink();

    const wxBitmapBundle& get_bmp() const { return bmp.bmp(); }

private:
    ScalableBitmap  bmp;
    bool            show {false};
};

// ----------------------------------------------------------------------------
// Highlighter
// ----------------------------------------------------------------------------

namespace Slic3r {
namespace GUI {

class OG_CustomCtrl;

// Highlighter is used as an instrument to put attention to some UI control

class Highlighter
{
    int             m_blink_counter     { 0 };
    wxTimer         m_timer;

public:
    Highlighter() {}
    ~Highlighter() {}

    void set_timer_owner(wxWindow* owner, int timerid = wxID_ANY);
    virtual void bind_timer(wxWindow* owner) = 0;

    bool init(bool input_failed);
    void blink();
    void invalidate();
};

class HighlighterForWx : public Highlighter
{
    struct BlinkingCustomCtrl
    {
        OG_CustomCtrl *custom_ctrl_ptr;
        bool          *show_blink_ptr;

        bool is_valid() const
        {
            return custom_ctrl_ptr != nullptr && show_blink_ptr != nullptr;
        }
    };

    using BlinkingCustomCtrls = std::vector<BlinkingCustomCtrl>;

// There are 2 possible cases to use HighlighterForWx:
// - using a BlinkingBitmap. Change state of this bitmap
    BlinkingBitmap     *m_blinking_bitmap { nullptr };
// - using OG_CustomCtrl where arrow will be rendered and flag indicated "show/hide" state of this arrow
    BlinkingCustomCtrls m_blinking_custom_ctrls;

public:
    HighlighterForWx() {}
    ~HighlighterForWx() {}

    void bind_timer(wxWindow* owner) override;
    void init(BlinkingBitmap* blinking_bitmap);
    void init(std::pair<OG_CustomCtrl*, bool*>);
    void init(const std::vector<std::pair<OG_CustomCtrl *, bool *>> &blinking_custom_ctrls_params);
    void blink();
    void invalidate();
};
/*
class HighlighterForImGUI : public Highlighter
{

public:
    HighlighterForImGUI() {}
    ~HighlighterForImGUI() {}

    void init();
    void blink();
    void invalidate();
};
*/
} // GUI
} // Slic3r

#endif // slic3r_GUI_wxExtensions_hpp_
