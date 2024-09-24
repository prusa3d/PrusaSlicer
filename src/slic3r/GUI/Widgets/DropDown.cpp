#include "DropDown.hpp"
#include "ComboBox.hpp"
#include "../GUI_App.hpp"
#include "../OptionsGroup.hpp"

#include <wx/dcgraph.h>
#include <wx/dcbuffer.h>
#include <wx/dcclient.h>
#include <wx/dcscreen.h>
#include <wx/dcmemory.h>
#include <wx/bitmap.h>

#include <wx/display.h>

#ifdef __WXGTK__
#include <gtk/gtk.h>
#endif

wxDEFINE_EVENT(EVT_DISMISS, wxCommandEvent);

BEGIN_EVENT_TABLE(DropDown, wxPopupTransientWindow)

EVT_LEFT_DOWN(DropDown::mouseDown)
EVT_LEFT_UP(DropDown::mouseReleased)
EVT_MOUSE_CAPTURE_LOST(DropDown::mouseCaptureLost)
EVT_MOTION(DropDown::mouseMove)
EVT_MOUSEWHEEL(DropDown::mouseWheelMoved)

// catch paint events
EVT_PAINT(DropDown::paintEvent)

END_EVENT_TABLE()

/*
 * Called by the system of by wxWidgets when the panel needs
 * to be redrawn. You can also trigger this call by
 * calling Refresh()/Update().
 */

DropDown::DropDown(std::vector<wxString> &texts,
                   std::vector<wxBitmapBundle> &icons)
    : texts(texts)
    , icons(icons)
    , radius(Slic3r::GUI::wxGetApp().suppress_round_corners() ? 0 : 5)
    , state_handler(this)
    , text_color(0x363636)
    , border_color(0xDBDBDB)
    , selector_border_color(std::make_pair(0x00AE42, (int) StateColor::Hovered),
        std::make_pair(*wxWHITE, (int) StateColor::Normal))
    , selector_background_color(std::make_pair(0xEDFAF2, (int) StateColor::Checked),
        std::make_pair(*wxWHITE, (int) StateColor::Normal))
{
}

DropDown::DropDown(wxWindow *             parent,
                   std::vector<wxString> &texts,
                   std::vector<wxBitmapBundle> &icons,
                   long           style)
    : DropDown(texts, icons)
{
    Create(parent, style);
}

#ifdef __WXGTK__
static gint gtk_popup_key_press (GtkWidget *widget, GdkEvent *gdk_event, wxPopupWindow* win )
{
    // Ignore events sent out before we connected to the signal
    if (win->m_time >= ((GdkEventKey*)gdk_event)->time)
        return FALSE;

    GtkWidget *child = gtk_get_event_widget (gdk_event);

    /*  We don't ask for button press events on the grab widget, so
     *  if an event is reported directly to the grab widget, it must
     *  be on a window outside the application (and thus we remove
     *  the popup window). Otherwise, we check if the widget is a child
     *  of the grab widget, and only remove the popup window if it
     *  is not. */
    if (child != widget) {
        while (child) {
            if (child == widget)
                return FALSE;
            child = gtk_widget_get_parent(child);
        }
    }

    gchar* keyval = gdk_keyval_name(((GdkEventKey*)gdk_event)->keyval);
    const long keyCode = strcmp(keyval, "Up") == 0     ? WXK_UP     :
                         strcmp(keyval, "Down") == 0   ? WXK_DOWN   :
                         strcmp(keyval, "Left") == 0   ? WXK_LEFT   :
                         strcmp(keyval, "Right") == 0  ? WXK_RIGHT  :
                         strcmp(keyval, "Return") == 0 ? WXK_RETURN : WXK_NONE;

    if (keyCode != WXK_NONE) {
        wxKeyEvent event( wxEVT_KEY_DOWN, win->GetId());
        event.m_keyCode = keyCode;
        event.SetEventObject( win );
        (void)win->HandleWindowEvent( event );
    }

    return TRUE;
}
#endif

void DropDown::Create(wxWindow *     parent,
         long           style)
{
    wxPopupTransientWindow::Create(parent);
#ifdef __WXGTK__
    g_signal_connect (m_widget, "key_press_event", G_CALLBACK (gtk_popup_key_press), this);

    Bind(wxEVT_KEY_DOWN, [parent](wxKeyEvent &e) {
        if (ComboBox* cb = dynamic_cast<ComboBox*>(parent))
            cb->OnKeyDown(e);
    });
#endif

    if (!wxOSX) SetBackgroundStyle(wxBG_STYLE_PAINT);
    state_handler.attach({&border_color, &text_color, &selector_border_color, &selector_background_color});
    state_handler.update_binds();
    if (!(style & DD_NO_CHECK_ICON))
        check_bitmap = ScalableBitmap(this, "checked", 16);
    text_off = style & DD_NO_TEXT;

    SetFont(parent->GetFont());
#ifdef __WXOSX__
    // wxPopupTransientWindow releases mouse on idle, which may cause various problems,
    //  such as losting mouse move, and dismissing soon on first LEFT_DOWN event.
    Bind(wxEVT_IDLE, [] (wxIdleEvent & evt) {});
#endif
}

void DropDown::Invalidate(bool clear)
{
    if (clear) {
        selection = hover_item = -1;
        offset = wxPoint();
    }
    assert(selection < (int) texts.size());
    need_sync = true;
}

void DropDown::SetSelection(int n)
{
    if (n >= (int) texts.size())
        n = -1;
    if (selection == n) return;
    selection = n;
    if (IsShown())
        autoPosition();
    paintNow();
}

wxString DropDown::GetValue() const
{
    return selection >= 0 ? texts[selection] : wxString();
}

void DropDown::SetValue(const wxString &value)
{
    auto i = std::find(texts.begin(), texts.end(), value);
    selection = i == texts.end() ? -1 : std::distance(texts.begin(), i);
}

void DropDown::SetCornerRadius(double radius_in)
{
    radius = radius_in;
    paintNow();
}

void DropDown::SetBorderColor(StateColor const &color)
{
    border_color = color;
    state_handler.update_binds();
    paintNow();
}

void DropDown::SetSelectorBorderColor(StateColor const &color)
{
    selector_border_color = color;
    state_handler.update_binds();
    paintNow();
}

void DropDown::SetTextColor(StateColor const &color)
{
    text_color = color;
    state_handler.update_binds();
    paintNow();
}

void DropDown::SetSelectorBackgroundColor(StateColor const &color)
{
    selector_background_color = color;
    state_handler.update_binds();
    paintNow();
}

void DropDown::SetUseContentWidth(bool use)
{
    if (use_content_width == use)
        return;
    use_content_width = use;
    need_sync = true;
    messureSize();
}

void DropDown::SetAlignIcon(bool align) { align_icon = align; }

void DropDown::Rescale()
{
    need_sync = true;
}

bool DropDown::HasDismissLongTime()
{
    auto now = boost::posix_time::microsec_clock::universal_time();
    return !IsShown() &&
        (now - dismissTime).total_milliseconds() >= 200;
}

void DropDown::paintEvent(wxPaintEvent& evt)
{
    // depending on your system you may need to look at double-buffered dcs
    wxBufferedPaintDC dc(this);
    render(dc);
}

/*
 * Alternatively, you can use a clientDC to paint on the panel
 * at any time. Using this generally does not free you from
 * catching paint events, since it is possible that e.g. the window
 * manager throws away your drawing when the window comes to the
 * background, and expects you will redraw it when the window comes
 * back (by sending a paint event).
 */
void DropDown::paintNow()
{
    // depending on your system you may need to look at double-buffered dcs
    //wxClientDC dc(this);
    //render(dc);
    Refresh();
}

void DropDown::SetTransparentBG(wxDC& dc, wxWindow* win)
{
    const wxSize  size       = win->GetSize();
    const wxPoint screen_pos = win->GetScreenPosition();
    wxScreenDC    screen_dc;

#ifdef __WXMSW__
    // Draw screen_dc to dc for transparent background
    dc.Blit(0, 0, size.x, size.y, &screen_dc, screen_pos.x, screen_pos.y);
#else
    // See https://forums.wxwidgets.org/viewtopic.php?f=1&t=49318
    wxClientDC client_dc(win);
    client_dc.Blit(0, 0, size.x, size.y, &screen_dc, screen_pos.x, screen_pos.y);

    wxBitmap bmp(size.x, size.y);
    wxMemoryDC mem_dc(bmp);
    mem_dc.Blit(0, 0, size.x, size.y, &client_dc, 0, 0);
    mem_dc.SelectObject(wxNullBitmap);
    dc.DrawBitmap(bmp, 0, 0);
#endif //__WXMSW__
}

constexpr int slider_width  = 12;
#ifdef __WXOSX__
constexpr int slider_step   = 1;
#else
constexpr int slider_step   = 5;
#endif
constexpr int items_padding = 2;

/*
 * Here we do the actual rendering. I put it in a separate
 * method so that it can work no matter what type of DC
 * (e.g. wxPaintDC or wxClientDC) is used.
 */
void DropDown::render(wxDC &dc)
{
    if (texts.size() == 0) return;
    int states = state_handler.states();

    const wxSize size = GetSize(); 
    if (radius > 0. && !wxOSX)
        SetTransparentBG(dc, this);

    dc.SetPen(wxPen(border_color.colorForStates(states)));
    dc.SetBrush(wxBrush(GetBackgroundColour()));
    // if (GetWindowStyle() & wxBORDER_NONE)
    //    dc.SetPen(wxNullPen);

    const bool is_retina = wxOSX && dc.GetContentScaleFactor() > 1.0;

    wxRect rc(0, 0, size.x, size.y);
    // On Retina displays all controls are cut on 1px
    if (is_retina)
        rc.x = rc.y = 1;

    // draw background
    if (radius == 0.0 || wxOSX)
        dc.DrawRectangle(rc);
    else
        dc.DrawRoundedRectangle(rc, radius);

    // draw hover rectangle
    wxRect rcContent = {{0, offset.y}, rowSize};
    const int text_size = int(texts.size());

    const bool has_bar = rowSize.y * text_size > size.y;
    if (has_bar)
        rcContent.width -= slider_width;

    if (hover_item >= 0 && (states & StateColor::Hovered)) {
        rcContent.y += rowSize.y * hover_item;
        if (rcContent.GetBottom() > 0 && rcContent.y < size.y) {
            if (selection == hover_item)
                dc.SetBrush(wxBrush(selector_background_color.colorForStates(StateColor::Disabled)));
            dc.SetPen(wxPen(selector_border_color.colorForStates(states)));
            rcContent.Deflate(4, 1);
            dc.DrawRectangle(rcContent);
            rcContent.Inflate(4, 1);
        }
        rcContent.y = offset.y;
    }
    // draw checked rectangle
    if (selection >= 0 && (selection != hover_item || (states & StateColor::Hovered) == 0)) {
        rcContent.y += rowSize.y * selection;
        if (rcContent.GetBottom() > 0 && rcContent.y < size.y) {
            dc.SetBrush(wxBrush(selector_background_color.colorForStates(StateColor::Disabled)));
            dc.SetPen(wxPen(selector_background_color.colorForStates(states)));
            rcContent.Deflate(4, 1);
            if (is_retina)
                rc.y += 1;
            dc.DrawRectangle(rcContent);
            rcContent.Inflate(4, 1);
            if (is_retina)
                rc.y -= 1;
        }
        rcContent.y = offset.y;
    }
    dc.SetBrush(*wxTRANSPARENT_BRUSH);
    {
        wxSize offset = (rowSize - textSize) / 2;
        rcContent.Deflate(0, offset.y);
    }

    // draw position bar
    if (has_bar) {
        int    height = rowSize.y * text_size;
        wxRect rect = {size.x - slider_width - 2, -offset.y * size.y / height + 2, slider_width,
                       size.y * size.y / height - 3};
        dc.SetPen(wxPen(border_color.defaultColor()));
        dc.SetBrush(wxBrush(selector_background_color.colorForStates(states | StateColor::Checked)));
        dc.DrawRoundedRectangle(rect, 2);
    }

    // draw check icon
    rcContent.x += 5;
    rcContent.width -= 5;
    if (check_bitmap.bmp().IsOk()) {
        auto szBmp = check_bitmap.GetSize();
        if (selection >= 0) {
            wxPoint pt = rcContent.GetLeftTop();
            pt.y += (rcContent.height - szBmp.y) / 2;
            pt.y += rowSize.y * selection;
            if (pt.y + szBmp.y > 0 && pt.y < size.y)
                dc.DrawBitmap(check_bitmap.get_bitmap(), pt);
        }
        rcContent.x += szBmp.x + 5;
        rcContent.width -= szBmp.x + 5;
    }
    // draw texts & icons
    dc.SetTextForeground(text_color.colorForStates(states));
    for (size_t i = 0; i < texts.size(); ++i) {
        if (rcContent.GetBottom() < 0) {
            rcContent.y += rowSize.y;
            continue;
        }
        if (rcContent.y > size.y) break;
        wxPoint pt   = rcContent.GetLeftTop();
        auto &  icon = icons[i];
        const wxSize pref_icon_sz = get_preferred_size(icon, m_parent);
        if (iconSize.x > 0) {
            if (icon.IsOk()) {
                pt.y += (rcContent.height - pref_icon_sz.y) / 2;
#ifdef __WXGTK3__
                dc.DrawBitmap(icon.GetBitmap(pref_icon_sz), pt);
#else
                dc.DrawBitmap(icon.GetBitmapFor(m_parent), pt);
#endif
            }
            pt.x += iconSize.x + 5;
            pt.y = rcContent.y;
        } else if (icon.IsOk()) {
            pt.y += (rcContent.height - pref_icon_sz.y) / 2;
#ifdef __WXGTK3__
            dc.DrawBitmap(icon.GetBitmap(pref_icon_sz), pt);
#else
            dc.DrawBitmap(icon.GetBitmapFor(m_parent), pt);
#endif
            pt.x += pref_icon_sz.GetWidth() + 5;
            pt.y = rcContent.y;
        }
        auto text = texts[i];
        if (!text_off && !text.IsEmpty()) {
            wxSize tSize = dc.GetMultiLineTextExtent(text);
            if (pt.x + tSize.x > rcContent.GetRight()) {
                text = wxControl::Ellipsize(text, dc, wxELLIPSIZE_END,
                                            rcContent.GetRight() - pt.x);
            }
            pt.y += (rcContent.height - textSize.y) / 2;
            dc.SetFont(GetFont());
            dc.DrawText(text, pt);
        }
        rcContent.y += rowSize.y;
    }
}

void DropDown::messureSize()
{
    if (!need_sync) return;
    textSize = wxSize();
    iconSize = wxSize();
    wxClientDC dc(GetParent() ? GetParent() : this);
    for (size_t i = 0; i < texts.size(); ++i) {
        wxSize size1 = text_off ? wxSize() : dc.GetMultiLineTextExtent(texts[i]);
        if (icons[i].IsOk()) {
            wxSize size2 = get_preferred_size(icons[i], m_parent);
            if (size2.x > iconSize.x) iconSize = size2;
            if (!align_icon) {
                size1.x += size2.x + (text_off ? 0 : 5);
            }
        }
        if (size1.x > textSize.x) textSize = size1;
    }
    if (!align_icon) iconSize.x = 0;
    wxSize szContent = textSize;
    szContent.x += 10;
    if (check_bitmap.bmp().IsOk()) {
        auto szBmp = check_bitmap.GetSize();
        szContent.x += szBmp.x + 5;
    }
    if (iconSize.x > 0) szContent.x += iconSize.x + (text_off ? 0 : 5);
    if (iconSize.y > szContent.y) szContent.y = iconSize.y;
    szContent.y += items_padding;
    if (texts.size() > 15) szContent.x += 6;
    if (GetParent()) {
        auto x = GetParent()->GetSize().x;
        if (!use_content_width || x > szContent.x)
            szContent.x = x;
    }
    rowSize = szContent;
    szContent.y *= std::min((size_t)15, texts.size());
    szContent.y += texts.size() > 15 ? rowSize.y / 2 : 0;
    wxWindow::SetSize(szContent);
#ifdef __WXGTK__
    // Gtk has a wrapper window for popup widget
    gtk_window_resize(GTK_WINDOW(m_widget), szContent.x, szContent.y);
#endif
    need_sync = false;
}

void DropDown::autoPosition()
{
    messureSize();
    wxPoint pos = GetParent()->ClientToScreen(wxPoint(0, -6));
    wxPoint old = GetPosition();
    wxSize size = GetSize();
    Position(pos, {0, GetParent()->GetSize().y + 12});
    if (old != GetPosition()) {
        size = rowSize;
        size.y *= std::min((size_t)15, texts.size());
        size.y += texts.size() > 15 ? rowSize.y / 2 : 0;
        if (size != GetSize()) {
            wxWindow::SetSize(size);
            offset = wxPoint();
            Position(pos, {0, GetParent()->GetSize().y + 12});
        }
    }
    if (GetPosition().y > pos.y) {
        // may exceed
        auto drect = wxDisplay(GetParent()).GetGeometry();
        if (GetPosition().y + size.y + 10 > drect.GetBottom()) {
            if (use_content_width && texts.size() <= 15) size.x += 6;
            size.y = drect.GetBottom() - GetPosition().y - 10;
            wxWindow::SetSize(size);
        }
    }
    if (selection >= 0) {
        if (offset.y + rowSize.y * (selection + 1) > size.y)
            offset.y = size.y - rowSize.y * (selection + 3);
        else if (offset.y + rowSize.y * selection < 0)
            offset.y = -rowSize.y * selection;
    }
}

void DropDown::mouseDown(wxMouseEvent& event)
{
    // Receivce unexcepted LEFT_DOWN on Mac after OnDismiss
    if (!IsShown())
        return;
    // force calc hover item again
    mouseMove(event);

    const wxSize size = GetSize();
    const int height = rowSize.y * int(texts.size());
    const wxRect rect = { size.x - slider_width, -offset.y * size.y / height, slider_width - 2,
                      size.y * size.y / height };
    slider_grabbed = rect.Contains(event.GetPosition());

    pressedDown = true;
    CaptureMouse();
    dragStart   = event.GetPosition();
}

void DropDown::mouseReleased(wxMouseEvent& event)
{
    if (pressedDown) {
        dragStart = wxPoint();
        pressedDown = false;
        slider_grabbed = false;
        if (HasCapture())
            ReleaseMouse();
        if (hover_item >= 0) { // not moved
#ifndef _WIN32
            // To avoid cases, when some dialog appears after item selection, but DropDown is still shown
            Hide();
#endif
            sendDropDownEvent();
            DismissAndNotify();
        }
    }
}

void DropDown::mouseCaptureLost(wxMouseCaptureLostEvent &event)
{
    wxMouseEvent evt;
    mouseReleased(evt);
}

void DropDown::mouseMove(wxMouseEvent &event)
{
    wxPoint pt  = event.GetPosition();
    int text_size = int(texts.size());
    if (pressedDown) {
        const int height = rowSize.y * text_size;
        const int y_step = slider_grabbed ? -height / GetSize().y : 1;

        wxPoint pt2 = offset + (pt - dragStart)*y_step;
        dragStart = pt;
        if (pt2.y > 0)
            pt2.y = 0;
        else if (pt2.y + rowSize.y * text_size < GetSize().y)
            pt2.y = GetSize().y - rowSize.y * text_size;
        if (pt2.y != offset.y) {
            offset = pt2;
            hover_item = -1; // moved
        } else {
            return;
        }
    }
    if (!pressedDown || hover_item >= 0) {
        int hover = (pt.y - offset.y) / rowSize.y;
        if (hover >= text_size || slider_grabbed) hover = -1;
        if (hover == hover_item) return;
        hover_item = hover;
        if (hover >= 0)
            SetToolTip(texts[hover]);
    }
    paintNow();
}

void DropDown::mouseWheelMoved(wxMouseEvent &event)
{
    if (event.GetWheelRotation() == 0)
        return;
    auto delta = event.GetWheelRotation() > 0 ? rowSize.y : -rowSize.y;
    wxPoint pt2 = offset + wxPoint{0, slider_step * delta};
    int text_size = int(texts.size());
    if (pt2.y > 0)
        pt2.y = 0;
    else if (pt2.y + rowSize.y * text_size < GetSize().y)
        pt2.y = GetSize().y - rowSize.y * text_size;
    if (pt2.y != offset.y) {
        offset = pt2;
    } else {
        return;
    }
    int hover = (event.GetPosition().y - offset.y) / rowSize.y;
    if (hover >= text_size) hover = -1;
    if (hover != hover_item) {
        hover_item = hover;
        if (hover >= 0) SetToolTip(texts[hover]);
    }
    paintNow();
}

// currently unused events
void DropDown::sendDropDownEvent()
{
    selection = hover_item;
    wxCommandEvent event(wxEVT_COMBOBOX, GetId());
    event.SetEventObject(this);
    event.SetInt(selection);
    event.SetString(GetValue());
    GetEventHandler()->ProcessEvent(event);
}

void DropDown::OnDismiss()
{
    dismissTime = boost::posix_time::microsec_clock::universal_time();
    hover_item  = -1;
    wxCommandEvent e(EVT_DISMISS);
    GetEventHandler()->ProcessEvent(e);
}
