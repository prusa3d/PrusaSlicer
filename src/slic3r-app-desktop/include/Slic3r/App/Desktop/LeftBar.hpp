#pragma once

#include "TabsBar.hpp"
#include "LeftBarCtrl.hpp"

namespace Slic3r::App::Desktop {

class TabsBarMenus;

class LeftBar : public TabsBar
{
private:
    LeftBar(
        wxWindow* parent,
        wxWindowID winid = wxID_ANY,
        const wxPoint& pos = wxDefaultPosition,
        const wxSize& size = wxDefaultSize)
    : TabsBar(parent, winid, pos, size, wxBK_LEFT) {}

public:
    static LeftBar* Create(
        wxWindow* parent,
        wxWindowID winid = wxID_ANY,
        const wxPoint& pos = wxDefaultPosition,
        const wxSize& size = wxDefaultSize)
    {
        LeftBar* lb = new LeftBar(parent, winid, pos, size);
        lb->CreateBookCtrl();
        return lb;
    }

    static LeftBar* Create(
        wxWindow * parent,
        TabsBarMenus* menus)
    {
        LeftBar* lb = new LeftBar(parent);
        lb->CreateBookCtrl(menus);
        return lb;
    }

    void ShowUserAccount(bool show)
    {
        GetLeftBarCtrl()->ShowUserAccount(show);
    }

    void set_compact_mode(bool compact_mode)
    {
        GetLeftBarCtrl()->set_compact_mode(compact_mode);
    }

    LeftBarCtrl* GetLeftBarCtrl() const { return static_cast<LeftBarCtrl*>(m_bookctrl); }

private:
    void CreateBookCtrl(TabsBarMenus* menus = nullptr) override
    {
        m_bookctrl = new LeftBarCtrl(this, wxVERTICAL, menus);
        m_controlSizer->Add(m_bookctrl, wxSizerFlags(1).Expand());
    }
};


} // namespace Slic3r::App::Desktop