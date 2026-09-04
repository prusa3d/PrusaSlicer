#pragma once

#include "TabsBarCtrl.hpp"

#include <wx/bookctrl.h>
#include <wx/panel.h>
#include <wx/sizer.h>

#include "Slic3r/App/WX/WidgetsConfig.hpp"

namespace Slic3r::App::Desktop {

class TabsBarMenus;

class TabsBar : public wxBookCtrlBase
{
public:
    TabsBar(wxWindow * parent,
            wxWindowID winid = wxID_ANY,
            const wxPoint & pos = wxDefaultPosition,
            const wxSize & size = wxDefaultSize,
            long style = 0)
    {
        Init();
        // wxNB_NOPAGETHEME: Disable Windows Vista theme for the Notebook background. The theme performance is terrible on Windows 10
        // with multiple high resolution displays connected.
        Create(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, style | wxTAB_TRAVERSAL | wxNB_NOPAGETHEME);
    }

    bool Create(wxWindow * parent,
                wxWindowID winid = wxID_ANY,
                const wxPoint & pos = wxDefaultPosition,
                const wxSize & size = wxDefaultSize,
                long style = 0)
    {
        if (!wxBookCtrlBase::Create(parent, winid, pos, size, style))
            return false;

        wxSizer* mainSizer = new wxBoxSizer(IsVertical() ? wxVERTICAL : wxHORIZONTAL);

        if (style & wxBK_RIGHT || style & wxBK_BOTTOM)
            mainSizer->Add(0, 0, 1, wxEXPAND, 0);

        const int orient = IsVertical() ? wxHORIZONTAL : wxVERTICAL;
        m_controlSizer = new wxBoxSizer(orient);

        // Note, that in CreateBookCtrl() m_bookctrl will be created and added into m_controlSizer

        wxSizerFlags flags;
        flags.Expand();
        mainSizer->Add(m_controlSizer, flags.Border(wxALL, m_controlMargin));
        SetSizer(mainSizer);

        this->Bind(wxCUSTOMEVT_TABS_BAR_SEL_CHANGED, [this](wxCommandEvent& evt)
        {                    
            if (int page_idx = evt.GetId(); page_idx >= 0)
                SetSelection(page_idx);
        });

        this->Bind(
            wxCUSTOMEVT_TABS_BAR_FORCE_FULL_LAYOUT,
            [this](wxCommandEvent&)
            {
                if (wxWindow* current_page = GetCurrentPage()) {
                    current_page->InvalidateBestSize();
                    Layout();
                    current_page->SetSize(GetPageRect());
                } else {
                    InvalidateBestSize();
                    Layout();
                }
            }
        );

        this->Bind(wxEVT_NAVIGATION_KEY, &TabsBar::OnNavigationKey, this);

        return true;
    }

    virtual void CreateBookCtrl(TabsBarMenus* menus) = 0;

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

        GetTabsBarCtrl()->InsertPage(n, text, bSelect, bmp_name);

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

    int SetSelection(size_t n) override
    {
        GetTabsBarCtrl()->SetSelection(n, true);
        int ret = DoSetSelection(n, SetSelection_SendEvent);

        // check that only the selected page is visible and others are hidden:
        for (size_t page = 0; page < m_pages.size(); page++)
            if (page != n)
                m_pages[page]->Hide();

        if (!m_pages[n]->IsShown())
            m_pages[n]->Show();

        return ret;
    }

    int ChangeSelection(size_t n) override
    {
        GetTabsBarCtrl()->SetSelection(n);
        return DoSetSelection(n);
    }

    // Neither labels nor images are supported but we still store the labels
    // just in case the user code attaches some importance to them.
    bool SetPageText(size_t n, const wxString & strText) override
    {
        wxCHECK_MSG(n < GetPageCount(), false, wxS("Invalid page"));

        GetTabsBarCtrl()->SetPageText(n, strText);

        return true;
    }

    wxString GetPageText(size_t n) const override
    {
        wxCHECK_MSG(n < GetPageCount(), wxString(), wxS("Invalid page"));
        return GetTabsBarCtrl()->GetPageText(n);
    }

    bool SetPageImage(size_t WXUNUSED(n), int WXUNUSED(imageId)) override
    {
        return false;
    }

    int GetPageImage(size_t WXUNUSED(n)) const override
    {
        return NO_IMAGE;
    }

    // Override some wxWindow methods too.
    void SetFocus() override
    {
        wxWindow* const page = GetCurrentPage();
        if (page)
            page->SetFocus();
    }

    TabsBarCtrl* GetTabsBarCtrl() const { return static_cast<TabsBarCtrl*>(m_bookctrl); }

    void Rescale()
    {
        GetTabsBarCtrl()->Rescale();
    }

    void OnColorsChanged()
    {
        GetTabsBarCtrl()->OnColorsChanged();
    }

    virtual void OnNavigationKey(wxNavigationKeyEvent& event)
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

            if (isFromSelf && !isForward)
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

protected:
    void UpdateSelectedPage(size_t WXUNUSED(newsel)) override
    {
        // Nothing to do here, but must be overridden to avoid the assert in
        // the base class version.
    }

    wxBookCtrlEvent * CreatePageChangingEvent() const override
    {
        return new wxBookCtrlEvent(wxEVT_BOOKCTRL_PAGE_CHANGING,
                                   GetId());
    }

    void MakeChangedEvent(wxBookCtrlEvent & event) override
    {
        event.SetEventType(wxEVT_BOOKCTRL_PAGE_CHANGED);
    }

    wxWindow * DoRemovePage(size_t page) override
    {
        wxWindow* const win = wxBookCtrlBase::DoRemovePage(page);
        if (win)
        {
            GetTabsBarCtrl()->RemovePage(page);
            DoSetSelectionAfterRemoval(page);
        }

        return win;
    }

    void DoSize() override
    {
        wxWindow* const page = GetCurrentPage();
        if (page)
            page->SetSize(GetPageRect());
    }

    void DoShowPage(wxWindow * page, bool show) override
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

} // namespace Slic3r::App::Desktop