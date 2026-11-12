#pragma once

#include <wx/control.h>
#include <wx/bmpbndl.h>
#include <wx/string.h>
#include <wx/panel.h>
#include <wx/display.h>

#include <vector>
#include <string>

class wxBoxSizer;
class wxFlexGridSizer;

// custom message the TabsBarCtrl sends to its parent (TabsBar) to notify a selection change:
wxDECLARE_EVENT(wxCUSTOMEVT_TABS_BAR_SEL_CHANGED, wxCommandEvent);
// custom message the TabsBarCtrl sends to its parent (TabsBar) to notify a required full layout refresh:
wxDECLARE_EVENT(wxCUSTOMEVT_TABS_BAR_FORCE_FULL_LAYOUT, wxCommandEvent);

namespace Slic3r::App::Desktop {

class TabsBarMenus;

class TabsBarCtrl : public wxControl
{
public:
    struct ButtonAppearance {
        wxString    label;
        std::string icon_name;
        int         px_cnt{ 16 };
        int         orient{ wxHORIZONTAL };
        bool        has_down_arrow{ false };
    };

    class Button : public wxPanel
    {
    public:
        Button() = default;
        Button(wxWindow* parent,
            const ButtonAppearance& appear,
            wxSize size = wxDefaultSize);

        void set_selected(bool selected);
        void set_hovered(bool hovered);
        void set_compact_mode(bool compact_mode, int icon_px_cnt = 16, bool update_from_appearance = true);
        bool compact_mode() const;
        void set_margin(int margin);
        void render();
        bool is_selected() const { return m_is_selected; }

        void sys_color_changed();
        void set_bitmap_bundle(const wxBitmapBundle& bmp_bundle);

        bool SetFont(const wxFont& font) override;

    private:
        void        init_bitmaps();
        void        messure_min_size();

    protected:
        void DoEnable(bool enable) override;

    protected:
        wxBitmapBundle  m_dd_bmp_bundle = wxBitmapBundle();
        ButtonAppearance m_appearance;

    private:
        wxWindow*   m_parent        { nullptr };
        bool        m_is_selected   { false };
        wxColour    m_background_color;
        wxColour    m_foreground_color;
        wxBitmapBundle  m_bmp_bundle = wxBitmapBundle();

        bool m_compact_mode{false};
        bool m_custom_bitmap{false};
        int m_margin{0};
    };

    class ButtonWithPopup : public Button
    {
        int             m_fixed_width{ wxDefaultCoord };
    public:
        ButtonWithPopup() {};
        ButtonWithPopup(wxWindow* parent,
            const wxString& label,
            const std::string& icon_name = "",
            const int           px_cnt = 16,
            const int           orient = wxHORIZONTAL,
            wxSize              size = wxDefaultSize);
        ButtonWithPopup(wxWindow* parent,
            const std::string& icon_name,
            const int           orient = wxHORIZONTAL,
            int                 icon_width = 20,
            int                 icon_height = 20);

        ~ButtonWithPopup() {}

        void SetLabel(const wxString& label) override;
        wxPoint get_popup_pos();
    };

    TabsBarCtrl(wxWindow* parent,
                int orient,
                TabsBarMenus* menus = nullptr);
    ~TabsBarCtrl() = default;

    void SetSelection(int sel, bool force = false);
    void Rescale();
    virtual void OnColorsChanged();
    void UpdateSelection();
    bool InsertPage(size_t n, const wxString& text, bool bSelect = false, const std::string& bmp_name = "");
    void RemovePage(size_t n);
    void SetPageText(size_t n, const wxString& strText);
    wxString GetPageText(size_t n) const;

    virtual void UnselectPopupButtons() = 0;

    void refresh_buttons();
    void set_compact_mode(bool compact_mode);
    bool compact_mode() const;
    void enable_buttons(bool enable);

protected:
    virtual void on_compact_mode_changed() = 0;
    virtual void on_rescale() = 0;
    wxRect get_selected_tab_rect();

private:
    void    update_margins();

protected:
    wxFlexGridSizer*        m_buttons_sizer { nullptr };
    wxFlexGridSizer*        m_sizer         { nullptr };
    wxBoxSizer*             m_first_sizer   { nullptr };
    wxBoxSizer*             m_second_sizer  { nullptr };
    std::vector<Button*>    m_pageButtons;
    int                     m_selection     { -1 };
    int                     m_btn_margin;
    int                     m_orient;

    TabsBarMenus*           m_menus         { nullptr };
    bool                    m_compact_mode{false};
};

} // namespace Slic3r::App::Desktop