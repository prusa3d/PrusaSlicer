#pragma once

#include "Scalable.hpp"

#include <wx/statbmp.h>
#include <wx/timer.h>

class wxPanel;

namespace Slic3r::App::WX {

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
// There are 2 possible cases to use HighlighterForWx:
// - using a BlinkingBitmap. Change state of this bitmap
    BlinkingBitmap* m_blinking_bitmap   { nullptr };
// - using OG_CustomCtrl where arrow will be rendered and flag indicated "show/hide" state of this arrow
    wxPanel*        m_custom_ctrl       { nullptr };
    bool*           m_show_blink_ptr    { nullptr };

public:
    HighlighterForWx() {}
    ~HighlighterForWx() {}

    void bind_timer(wxWindow* owner) override;
    void init(BlinkingBitmap* blinking_bitmap);
    void init(std::pair<wxPanel*, bool*>);
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

}

