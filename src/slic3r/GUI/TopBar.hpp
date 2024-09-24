#ifndef slic3r_TopBar_hpp_
#define slic3r_TopBar_hpp_

#include <wx/bookctrl.h>
#include <wx/panel.h>
#include "Widgets/TextInput.hpp"

class TopBarMenus;

// custom message the TopBarItemsCtrl sends to its parent (TopBar) to notify a selection change:
wxDECLARE_EVENT(wxCUSTOMEVT_TOPBAR_SEL_CHANGED, wxCommandEvent);

class TopBarItemsCtrl : public wxControl
{
    class Button : public wxPanel
    {
        bool        m_is_selected{ false };
        wxColour    m_background_color;
        wxColour    m_foreground_color;
        wxBitmapBundle  m_bmp_bundle = wxBitmapBundle();

    protected:
        wxString    m_label;
        std::string m_icon_name;
        int         m_px_cnt            { 16 };
        bool        m_has_down_arrow    { false };
        wxBitmapBundle  m_dd_bmp_bundle = wxBitmapBundle();

    public:
        Button() {};
        Button( wxWindow*           parent,
                const wxString&     label,
                const std::string&  icon_name = "",
                const int           px_cnt = 16,
                wxSize              size = wxDefaultSize);

        ~Button() {}

        void set_selected(bool selected);
        void set_hovered (bool hovered);
        void render();

        void sys_color_changed();
        void SetBitmapBundle(wxBitmapBundle bmp_bundle) { m_bmp_bundle = bmp_bundle; }
    };

    class ButtonWithPopup : public Button
    {
        int             m_fixed_width       { wxDefaultCoord };
    public:
        ButtonWithPopup() {};
        ButtonWithPopup(wxWindow*           parent,
                        const wxString&     label,
                        const std::string&  icon_name = "",
                        const int           px_cnt = 16,
                        wxSize              size = wxDefaultSize);
        ButtonWithPopup(wxWindow*           parent,
                        const std::string&  icon_name,
                        int                 icon_width = 20,
                        int                 icon_height = 20);

        ~ButtonWithPopup() {}

        void SetLabel(const wxString& label) override;
        wxPoint get_popup_pos();
    };

    TopBarMenus*    m_menus                 { nullptr };

    ::TextInput*    m_search                { nullptr };

    int             m_btns_width            { 0 };
    bool            m_collapsed_btns        { false };

    std::function<void()> m_cb_settings_btn { nullptr };

    void            update_btns_width();

public:
    TopBarItemsCtrl(wxWindow* parent,
                    TopBarMenus* menus = nullptr,
                    std::function<void()> cb_settings_btn = nullptr);
    ~TopBarItemsCtrl() {}

    void SetSelection(int sel, bool force = false);
    void UpdateMode();
    void ShowUserAccount(bool show);
    void Rescale();
    void OnColorsChanged();
    void UpdateModeMarkers();
    void UpdateSelection();
    bool InsertPage(size_t n, const wxString& text, bool bSelect = false, const std::string& bmp_name = "");
    void RemovePage(size_t n);
    void SetPageText(size_t n, const wxString& strText);
    wxString GetPageText(size_t n) const;

    void UpdateAccountButton(bool avatar = false);
    void UnselectPopupButtons();

    void CreateSearch();
    void ShowFull();
    void ShowJustMode();
    void SetSettingsButtonTooltip(const wxString& tooltip);
    void UpdateSearchSizeAndPosition();
    void UpdateSearch(const wxString& search);

    wxWindow* GetSearchCtrl() { return m_search->GetTextCtrl(); }

private:
    wxFlexGridSizer*                m_buttons_sizer;
    wxFlexGridSizer*                m_sizer;
    ButtonWithPopup*                m_menu_btn      {nullptr};
    ButtonWithPopup*                m_workspace_btn {nullptr};
    ButtonWithPopup*                m_account_btn   {nullptr};
    Button*                         m_settings_btn  {nullptr};
    std::vector<Button*>            m_pageButtons;
    int                             m_selection {-1};
    int                             m_btn_margin;

    void    update_margins();
};

class TopBar : public wxBookCtrlBase
{
public:
    TopBar(wxWindow * parent,
                 wxWindowID winid = wxID_ANY,
                 const wxPoint & pos = wxDefaultPosition,
                 const wxSize & size = wxDefaultSize,
                 long style = 0)
    {
        Init();
        Create(parent, winid, pos, size, style);
    }

    TopBar( wxWindow * parent,
            TopBarMenus* menus,
            std::function<void()> cb_settings_btn = nullptr)
    {
        Init();
        // wxNB_NOPAGETHEME: Disable Windows Vista theme for the Notebook background. The theme performance is terrible on Windows 10
        // with multiple high resolution displays connected.
        Create(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxNB_TOP | wxTAB_TRAVERSAL | wxNB_NOPAGETHEME, menus, cb_settings_btn);
    }

    bool Create(wxWindow * parent,
                wxWindowID winid = wxID_ANY,
                const wxPoint & pos = wxDefaultPosition,
                const wxSize & size = wxDefaultSize,
                long style = 0,
                TopBarMenus* menus = nullptr,
                std::function<void()> cb_settings_btn = nullptr)
    {
        if (!wxBookCtrlBase::Create(parent, winid, pos, size, style | wxBK_TOP))
            return false;

        m_bookctrl = new TopBarItemsCtrl(this, menus, cb_settings_btn);

        wxSizer* mainSizer = new wxBoxSizer(IsVertical() ? wxVERTICAL : wxHORIZONTAL);

        if (style & wxBK_RIGHT || style & wxBK_BOTTOM)
            mainSizer->Add(0, 0, 1, wxEXPAND, 0);

        m_controlSizer = new wxBoxSizer(IsVertical() ? wxHORIZONTAL : wxVERTICAL);
        m_controlSizer->Add(m_bookctrl, wxSizerFlags(1).Expand());
        wxSizerFlags flags;
        if (IsVertical())
            flags.Expand();
        else
            flags.CentreVertical();
        mainSizer->Add(m_controlSizer, flags.Border(wxALL, m_controlMargin));
        SetSizer(mainSizer);

        this->Bind(wxCUSTOMEVT_TOPBAR_SEL_CHANGED, [this](wxCommandEvent& evt)
        {                    
            if (int page_idx = evt.GetId(); page_idx >= 0)
                SetSelection(page_idx);
        });

        this->Bind(wxEVT_NAVIGATION_KEY, &TopBar::OnNavigationKey, this);

        return true;
    }


    // Methods specific to this class.

    // A method allowing to add a new page without any label (which is unused
    // by this control) and show it immediately.
    bool ShowNewPage(wxWindow * page)
    {
        return AddNewPage(page, wxString(), ""/*true *//* select it */);
    }


    // Set effect to use for showing/hiding pages.
    void SetEffects(wxShowEffect showEffect, wxShowEffect hideEffect)
    {
        m_showEffect = showEffect;
        m_hideEffect = hideEffect;
    }

    // Or the same effect for both of them.
    void SetEffect(wxShowEffect effect)
    {
        SetEffects(effect, effect);
    }

    // And the same for time outs.
    void SetEffectsTimeouts(unsigned showTimeout, unsigned hideTimeout)
    {
        m_showTimeout = showTimeout;
        m_hideTimeout = hideTimeout;
    }

    void SetEffectTimeout(unsigned timeout)
    {
        SetEffectsTimeouts(timeout, timeout);
    }


    // Implement base class pure virtual methods.

    // adds a new page to the control
    bool AddNewPage(wxWindow* page,
                 const wxString& text,
                 const std::string& bmp_name,
                 bool bSelect = false)
    {
        DoInvalidateBestSize();
        return InsertNewPage(GetPageCount(), page, text, bmp_name, bSelect);
    }

    bool InsertNewPage(size_t n,
                    wxWindow * page,
                    const wxString & text,
                    const std::string& bmp_name = "",
                    bool bSelect = false)
    {
        if (!wxBookCtrlBase::InsertPage(n, page, text, bSelect))
            return false;

        GetTopBarItemsCtrl()->InsertPage(n, text, bSelect, bmp_name);

        if (bSelect)
            SetSelection(n);
        else
            page->Hide();

        return true;
    }

    // override AddPage with using of AddNewPage
    bool AddPage(   wxWindow* page,
                    const wxString& text,
                    bool bSelect = false,
                    int imageId = NO_IMAGE) override
    {
        return AddNewPage(page, text, "", bSelect);
    }

    // Page management
    bool InsertPage(size_t n,
                    wxWindow * page,
                    const wxString & text,
                    bool bSelect = false,
                    int imageId = NO_IMAGE) override
    {
        return InsertNewPage(n, page, text, "", bSelect);
    }

    virtual int SetSelection(size_t n) override
    {
        GetTopBarItemsCtrl()->SetSelection(n, true);
        int ret = DoSetSelection(n, SetSelection_SendEvent);

        // check that only the selected page is visible and others are hidden:
        for (size_t page = 0; page < m_pages.size(); page++)
            if (page != n)
                m_pages[page]->Hide();

        if (!m_pages[n]->IsShown())
            m_pages[n]->Show();

        return ret;
    }

    virtual int ChangeSelection(size_t n) override
    {
        GetTopBarItemsCtrl()->SetSelection(n);
        return DoSetSelection(n);
    }

    // Neither labels nor images are supported but we still store the labels
    // just in case the user code attaches some importance to them.
    virtual bool SetPageText(size_t n, const wxString & strText) override
    {
        wxCHECK_MSG(n < GetPageCount(), false, wxS("Invalid page"));

        GetTopBarItemsCtrl()->SetPageText(n, strText);

        return true;
    }

    virtual wxString GetPageText(size_t n) const override
    {
        wxCHECK_MSG(n < GetPageCount(), wxString(), wxS("Invalid page"));
        return GetTopBarItemsCtrl()->GetPageText(n);
    }

    virtual bool SetPageImage(size_t WXUNUSED(n), int WXUNUSED(imageId)) override
    {
        return false;
    }

    virtual int GetPageImage(size_t WXUNUSED(n)) const override
    {
        return NO_IMAGE;
    }

    // Override some wxWindow methods too.
    virtual void SetFocus() override
    {
        wxWindow* const page = GetCurrentPage();
        if (page)
            page->SetFocus();
    }

    TopBarItemsCtrl* GetTopBarItemsCtrl() const { return static_cast<TopBarItemsCtrl*>(m_bookctrl); }

    void UpdateMode()
    {
        GetTopBarItemsCtrl()->UpdateMode();
    }

    void ShowUserAccount(bool show)
    {
        GetTopBarItemsCtrl()->ShowUserAccount(show);
    }

    void Rescale()
    {
        GetTopBarItemsCtrl()->Rescale();
    }

    void OnColorsChanged()
    {
        GetTopBarItemsCtrl()->OnColorsChanged();
    }

    void UpdateModeMarkers()
    {
        GetTopBarItemsCtrl()->UpdateModeMarkers();
    }

    void OnNavigationKey(wxNavigationKeyEvent& event)
    {
        if (event.IsWindowChange()) {
            // change pages
            AdvanceSelection(event.GetDirection());
        }
        else {
            // we get this event in 3 cases
            //
            // a) one of our pages might have generated it because the user TABbed
            // out from it in which case we should propagate the event upwards and
            // our parent will take care of setting the focus to prev/next sibling
            //
            // or
            //
            // b) the parent panel wants to give the focus to us so that we
            // forward it to our selected page. We can't deal with this in
            // OnSetFocus() because we don't know which direction the focus came
            // from in this case and so can't choose between setting the focus to
            // first or last panel child
            //
            // or
            //
            // c) we ourselves (see MSWTranslateMessage) generated the event
            //
            wxWindow* const parent = GetParent();

            // the wxObject* casts are required to avoid MinGW GCC 2.95.3 ICE
            const bool isFromParent = event.GetEventObject() == (wxObject*)parent;
            const bool isFromSelf = event.GetEventObject() == (wxObject*)this;
            const bool isForward = event.GetDirection();

            wxWindow* search_win = (dynamic_cast<TopBarItemsCtrl*>(m_bookctrl)->GetSearchCtrl());
            const bool isFromSearch = event.GetEventObject() == (wxObject*)search_win;
            if (isFromSearch)
            {
                // find the target window in the siblings list
                wxWindowList& siblings = m_bookctrl->GetChildren();
                wxWindowList::compatibility_iterator i = siblings.Find(search_win->GetParent());
                i->GetNext()->GetData()->SetFocus();
            }
            else if (isFromSelf && !isForward)
            {
                // focus is currently on notebook tab and should leave
                // it backwards (Shift-TAB)
                event.SetCurrentFocus(this);
                parent->HandleWindowEvent(event);
            }
            else if (isFromParent || isFromSelf)
            {
                // no, it doesn't come from child, case (b) or (c): forward to a
                // page but only if entering notebook page (i.e. direction is
                // backwards (Shift-TAB) comething from out-of-notebook, or
                // direction is forward (TAB) from ourselves),
                if (m_selection != wxNOT_FOUND &&
                    (!event.GetDirection() || isFromSelf))
                {
                    // so that the page knows that the event comes from it's parent
                    // and is being propagated downwards
                    event.SetEventObject(this);

                    wxWindow* page = m_pages[m_selection];
                    if (!page->HandleWindowEvent(event))
                    {
                        page->SetFocus();
                    }
                    //else: page manages focus inside it itself
                }
                else // otherwise set the focus to the notebook itself
                {
                    SetFocus();
                }
            }
            else
            {
                // it comes from our child, case (a), pass to the parent, but only
                // if the direction is forwards. Otherwise set the focus to the
                // notebook itself. The notebook is always the 'first' control of a
                // page.
                if (!isForward)
                {
                    SetFocus();
                }
                else if (parent)
                {
                    event.SetCurrentFocus(this);
                    parent->HandleWindowEvent(event);
                }
            }
        }
    }

    // Methods for extensions of this class

    void ShowFull() {
        Show();
        GetTopBarItemsCtrl()->ShowFull();
    }

    void ShowJustMode() {
        Show();
        GetTopBarItemsCtrl()->ShowJustMode();
    }

    void SetSettingsButtonTooltip(const wxString& tooltip) {
        GetTopBarItemsCtrl()->SetSettingsButtonTooltip(tooltip);
    }

    void UpdateSearchSizeAndPosition() {
        GetTopBarItemsCtrl()->UpdateSearchSizeAndPosition();
    }

    void UpdateSearch(const wxString& search) {
        GetTopBarItemsCtrl()->UpdateSearch(search);
    }

protected:
    virtual void UpdateSelectedPage(size_t WXUNUSED(newsel)) override
    {
        // Nothing to do here, but must be overridden to avoid the assert in
        // the base class version.
    }

    virtual wxBookCtrlEvent * CreatePageChangingEvent() const override
    {
        return new wxBookCtrlEvent(wxEVT_BOOKCTRL_PAGE_CHANGING,
                                   GetId());
    }

    virtual void MakeChangedEvent(wxBookCtrlEvent & event) override
    {
        event.SetEventType(wxEVT_BOOKCTRL_PAGE_CHANGED);
    }

    virtual wxWindow * DoRemovePage(size_t page) override
    {
        wxWindow* const win = wxBookCtrlBase::DoRemovePage(page);
        if (win)
        {
            GetTopBarItemsCtrl()->RemovePage(page);
            // Don't setect any page after deletion some of them
            // DoSetSelectionAfterRemoval(page);
        }

        return win;
    }

    virtual void DoSize() override
    {
        wxWindow* const page = GetCurrentPage();
        if (page)
            page->SetSize(GetPageRect());
    }

    virtual void DoShowPage(wxWindow * page, bool show) override
    {
        if (show)
            page->ShowWithEffect(m_showEffect, m_showTimeout);
        else
            page->HideWithEffect(m_hideEffect, m_hideTimeout);
    }

private:
    void Init()
    {
        // We don't need any border as we don't have anything to separate the
        // page contents from.
        SetInternalBorder(0);

        // No effects by default.
        m_showEffect =
        m_hideEffect = wxSHOW_EFFECT_NONE;

        m_showTimeout =
        m_hideTimeout = 0;
    }

    wxShowEffect m_showEffect,
                 m_hideEffect;

    unsigned m_showTimeout,
             m_hideTimeout;

};
//#endif // _WIN32
#endif // slic3r_TopBar_hpp_
