#include "Slic3r/App/Desktop/TabsBarCtrl.hpp"

#include <Slic3r/App/WX/WidgetsConfig.hpp>
#include <Slic3r/App/WX/BitmapGetters.hpp>
#include <Slic3r/App/WX/StringConversions.hpp>
#include <Slic3r/App/AppServices.hpp>
#include <Slic3r/App/Theme.hpp>

#include <Slic3r/Biz/Algorithms/Color.hpp>

#include "Slic3r/App/WX/I18N.hpp"

#include <wx/sizer.h>
#include <wx/dcclient.h>

wxDEFINE_EVENT(wxCUSTOMEVT_TABS_BAR_SEL_CHANGED, wxCommandEvent);
wxDEFINE_EVENT(wxCUSTOMEVT_TABS_BAR_FORCE_FULL_LAYOUT, wxCommandEvent);

namespace Slic3r::App::Desktop {

using namespace Slic3r::App::WX;

TabsBarCtrl::Button::Button(wxWindow* parent, const ButtonAppearance& appear, wxSize size_def) :
    wxPanel(parent, wxID_ANY, wxDefaultPosition, size_def, wxBORDER_NONE | wxTAB_TRAVERSAL),
    m_appearance(appear),
    m_parent(parent),
#ifdef _WIN32
    m_background_color(m_parent->GetBackgroundColour()),
#else
    m_background_color(wxTransparentColor),
#endif
    m_foreground_color(m_parent->GetForegroundColour())
{
    m_margin = w_config()->em_unit(this);
    init_bitmaps();
    messure_min_size();

    // button events
    Bind(
        wxEVT_SET_FOCUS,
        [this](wxFocusEvent& event)
        {
            set_hovered(true);
            event.Skip();
        }
    );
    Bind(
        wxEVT_KILL_FOCUS,
        [this](wxFocusEvent& event)
        {
            set_hovered(false);
            event.Skip();
        }
    );
    Bind(
        wxEVT_ENTER_WINDOW,
        [this](wxMouseEvent& event)
        {
            set_hovered(true);
            event.Skip();
        }
    );
    Bind(
        wxEVT_LEAVE_WINDOW,
        [this](wxMouseEvent& event)
        {
            set_hovered(false);
            event.Skip();
        }
    );

    Bind(wxEVT_PAINT, [this](wxPaintEvent&) { render(); });
#ifdef __linux__
    Bind(
        wxEVT_LEFT_UP,
        [this](wxMouseEvent& event)
        {
#else
    Bind(
        wxEVT_LEFT_DOWN,
        [this](wxMouseEvent& event)
        {
#endif
            wxCommandEvent evt(wxEVT_BUTTON, GetId());
            GetEventHandler()->AddPendingEvent(evt);
            event.Skip();
        }
    );
}

void TabsBarCtrl::Button::init_bitmaps()
{

    const std::string disabled_color = IsEnabled() ? std::string() :
                                                     Slic3r::Biz::Algorithms::Color::encode_color(
                                                         AppServices::instance().theme().color(
                                                             Platform::Color::Text,
                                                             Platform::ColorGroup::Disabled
                                                         )
                                                     );

    if (!m_appearance.icon_name.empty() && !m_custom_bitmap) {
        m_bmp_bundle =
            *get_bmp_bundle(m_appearance.icon_name, m_appearance.px_cnt, -1, disabled_color);
    }
    if (m_appearance.has_down_arrow) {
        m_dd_bmp_bundle = *get_bmp_bundle("drop_down", 16, -1);
    }
}

void TabsBarCtrl::Button::DoEnable(bool enable)
{
    wxPanel::DoEnable(enable);
    init_bitmaps();
    Refresh();
}


void TabsBarCtrl::Button::messure_min_size()
{
    if (m_compact_mode) {
        const double koef = 0.1 * w_config()->em_unit(this);
        this->SetMinSize(wxSize(static_cast<int>(50. * koef), static_cast<int>(52. * koef)));
        return;
    }

    int x, y;
    wxString localized_label = m_compact_mode ? wxString() : m_appearance.label;

    if (m_appearance.orient == wxHORIZONTAL) {
        GetTextExtent(
            localized_label.IsEmpty() ? wxString::FromUTF8("a") : localized_label,
            &x,
            &y
        );
        wxSize size(x + 4 * m_margin, y + int(1.5 * m_margin));
        if (m_appearance.icon_name.empty())
            this->SetMinSize(size);
        else if (localized_label.IsEmpty()) {
            const int btn_side = size.y;
            this->SetMinSize(wxSize(btn_side, btn_side));
        } else
#ifdef _WIN32
            this->SetMinSize(wxSize(-1, size.y));
#else
            this->SetMinSize(wxSize(size.x + m_appearance.px_cnt, size.y));
#endif
    } else {
        wxArrayString lines = wxSplit(localized_label, ' ');
        if (lines.size() == 1) {
            GetTextExtent(localized_label, &x, &y);
        } else {
            int max_width = 0;
            for (const wxString& line : lines) {
                int width         = 0;
                int unused_height = 0;
                GetTextExtent(line, &width, &unused_height);
                max_width = std::max(max_width, width);
            }
            const int line_height = GetCharHeight();
            y                     = static_cast<int>(lines.size()) * line_height;
            x                     = max_width;
        }

        wxSize szIcon = get_preferred_size(m_bmp_bundle, this);
        wxSize size(x + 2 * m_margin, y + 3 * m_margin + szIcon.y);
        if (localized_label.IsEmpty()) {
            this->SetMinSize(wxSize(size.y, size.y));
        } else {
            size.y += m_margin;
            this->SetMinSize(size);
        }
    }
}

void TabsBarCtrl::Button::set_selected(bool selected)
{
    if (m_is_selected != selected) {
        m_is_selected = selected;

        m_background_color = m_is_selected ? w_config()->get_color_selected_btn_bg() :
#ifdef _WIN32
                                             m_parent->GetBackgroundColour();
#else
                                             wxTransparentColor;
#endif
        Refresh();
    }
}

void TabsBarCtrl::Button::set_hovered(bool hovered)
{
#ifdef _WIN32
    this->GetParent()->Refresh(); // force redraw a background of the selected mode button
#endif /* no _WIN32 */

    m_background_color = m_is_selected ? w_config()->get_color_selected_btn_bg() :
        hovered                        ? w_config()->get_color_hovered_btn_bg() :

#ifdef _WIN32
                  m_parent->GetBackgroundColour();
#else
                  wxTransparentColor;
#endif

    this->Refresh();
    this->Update();
}

void TabsBarCtrl::Button::set_compact_mode(
    bool compact_mode,
    int icon_px_cnt,
    bool update_from_appearance
)
{
    if (m_appearance.px_cnt != icon_px_cnt) {
        m_appearance.px_cnt = icon_px_cnt;
        if (update_from_appearance) {
            init_bitmaps();
        }
    }
    m_compact_mode = compact_mode;
    messure_min_size();

    if (update_from_appearance) {
        SetToolTip(
            m_compact_mode && !m_appearance.label.IsEmpty() ? m_appearance.label : wxString()
        );
    }
}

bool TabsBarCtrl::Button::compact_mode() const
{
    return m_compact_mode;
}

void TabsBarCtrl::Button::set_margin(int margin)
{
    m_margin = margin;
    messure_min_size();
}

void TabsBarCtrl::Button::render()
{
    const wxRect rc(GetSize());
    wxPaintDC dc(this);

    int em = w_config()->em_unit(this);
    const double round_radius = m_compact_mode ? 0.0 : 0.5 * em;
    const int gap = int(0.5 * em);

    // Draw def rect with rounded corners

    dc.SetPen(m_background_color);
    dc.SetBrush(m_background_color);

    if (m_appearance.orient == wxHORIZONTAL && !m_appearance.label.empty() && m_is_selected) {
        // render Button as a Tab
        wxRect new_rc(rc.GetPosition(), rc.GetSize() + wxSize(0, em));
        dc.DrawRoundedRectangle(new_rc, round_radius);
    } else
        dc.DrawRoundedRectangle(rc, round_radius);

    wxPoint pt    = {0, 0};
    wxString text = m_compact_mode ? wxString() : m_appearance.label;

    if (m_appearance.orient == wxHORIZONTAL) {
        if (m_bmp_bundle.IsOk()) {
            wxSize szIcon = get_preferred_size(m_bmp_bundle, this);
            pt.x          = text.IsEmpty() ? ((rc.width - szIcon.x) / 2) : em;
            pt.y          = (rc.height - szIcon.y) / 2;
            dc.DrawBitmap(m_bmp_bundle.GetBitmapFor(this), pt, true);
            pt.x += szIcon.x + gap;
        }

        // Draw text

        if (!text.IsEmpty()) {
            wxSize labelSize = dc.GetTextExtent(text);
            if (labelSize.x > rc.width)
                text = wxControl::Ellipsize(text, dc, wxELLIPSIZE_END, rc.width);
            if (!m_bmp_bundle.IsOk())
                pt.x += (rc.width - pt.x - labelSize.x) / 2;
            pt.y = (rc.height - labelSize.y) / 2;

            dc.SetTextForeground(m_foreground_color);
            dc.SetFont(GetFont());
            dc.DrawText(text, pt);

            pt.x += labelSize.x + gap;

            // Draw down_arrow if needed

            if (m_dd_bmp_bundle.IsOk()) {
                wxSize szIcon = get_preferred_size(m_dd_bmp_bundle, this);
                pt.y          = (rc.height - szIcon.y) / 2;
                dc.DrawBitmap(m_dd_bmp_bundle.GetBitmapFor(this), pt, true);
            }
        }
    } else {
        if (m_bmp_bundle.IsOk()) {
            wxSize szIcon = get_preferred_size(m_bmp_bundle, this);
            pt.x          = (rc.width - szIcon.x) * 0.5;
            if (text.IsEmpty()) {
                // button has no text, so icon should be aligned in the center
                pt.y = (rc.height - szIcon.y) * 0.5;
            } else {
                pt.y = em * 1.5;
            }
            dc.DrawBitmap(m_bmp_bundle.GetBitmapFor(this), pt, true);
            pt.y += em + szIcon.y;
        }

        // Draw text

        if (!text.IsEmpty()) {
            dc.SetTextForeground(m_foreground_color);
            dc.SetFont(GetFont());

            wxArrayString lines = wxSplit(text, ' ');
            if (lines.size() == 1) {
                wxSize labelSize = dc.GetTextExtent(text);
                if (labelSize.x > rc.width)
                    text = wxControl::Ellipsize(text, dc, wxELLIPSIZE_END, rc.width);
                pt.x = (rc.width - labelSize.x) * 0.5;
                if (!m_bmp_bundle.IsOk())
                    pt.y = (rc.height - labelSize.y) * 0.5;
                dc.DrawText(text, pt);
            } else {
                int max_width         = 0;
                const int line_height = GetCharHeight();
                for (const wxString& line : lines) {
                    wxSize labelSize = dc.GetTextExtent(line);
                    pt.x             = (rc.width - labelSize.x) * 0.5;
                    if (!m_bmp_bundle.IsOk())
                        pt.y = (rc.height - labelSize.y) * 0.5;

                    dc.DrawText(line, pt);
                    pt.y += line_height;
                }
            }
        }

        // Draw down_arrow if needed

        if (m_dd_bmp_bundle.IsOk()) {
            wxSize szIcon = get_preferred_size(m_dd_bmp_bundle, this);
            pt.x          = rc.width - szIcon.x;
            pt.y          = rc.height - szIcon.y;
            dc.DrawBitmap(m_dd_bmp_bundle.GetBitmapFor(this), pt, true);
        }
    }
}

void TabsBarCtrl::Button::sys_color_changed()
{
    init_bitmaps();

    m_background_color = m_parent->GetBackgroundColour();
    m_foreground_color = m_parent->GetForegroundColour();
}

void TabsBarCtrl::Button::set_bitmap_bundle(const wxBitmapBundle& bmp_bundle)
{
    m_bmp_bundle = bmp_bundle;
    m_custom_bitmap = true;
}

bool TabsBarCtrl::Button::SetFont(const wxFont& font)
{
    bool ret = wxPanel::SetFont(font);
    messure_min_size();
    return ret;
}

TabsBarCtrl::ButtonWithPopup::ButtonWithPopup(
    wxWindow* parent,
    const wxString& label,
    const std::string& icon_name,
    const int px_cnt,
    const int orient,
    wxSize size
) :
    TabsBarCtrl::Button(parent, {label, icon_name, px_cnt, orient, true}, size)
{
    if (size != wxDefaultSize)
        m_fixed_width = size.x * 0.1;

    this->SetLabel(label);
}

TabsBarCtrl::ButtonWithPopup::ButtonWithPopup(
    wxWindow* parent,
    const std::string& icon_name,
    const int orient,
    int icon_width /* = 20*/,
    int icon_height /* = 20*/
) :
    TabsBarCtrl::Button(parent, {wxString::FromUTF8(""), icon_name, icon_width, orient, true})
{}

void TabsBarCtrl::ButtonWithPopup::SetLabel(const wxString& label)
{
    wxString text  = label;
    int btn_height = GetMinSize().GetHeight();

    if (label.IsEmpty()) {
        m_appearance.label = label;
        SetMinSize(wxSize(btn_height, btn_height));
        return;
    }

    const int em = w_config()->em_unit(this);

    const int label_width = GetTextExtent(text).GetWidth();
    int width_margins     = int(0.1 * em * (m_appearance.px_cnt + 16 + 25));

    this->SetMinSize(wxSize(label_width + width_margins, btn_height));

    if (m_fixed_width != wxDefaultCoord) {
        const int text_width = m_fixed_width * w_config()->em_unit(this) - width_margins;
        if (label_width > text_width) {
            wxWindowDC wdc(this);
            text = wxControl::Ellipsize(text, wdc, wxELLIPSIZE_END, text_width);

            SetMinSize(wxSize(m_fixed_width * w_config()->em_unit(this), btn_height));
            SetSize(wxSize(m_fixed_width * w_config()->em_unit(this), btn_height));
        }
    }

    m_appearance.label = text;
    Refresh();
    GetParent()->Layout();
}

void TabsBarCtrl::update_margins()
{
    int em       = w_config()->em_unit(this);
    m_btn_margin = std::lround(0.9 * em);

    for (auto* btn : m_pageButtons) {
        btn->set_margin(em);
    }
    Layout();
}

wxPoint TabsBarCtrl::ButtonWithPopup::get_popup_pos()
{
    wxPoint pos = this->GetPosition() + int(0.3 * w_config()->em_unit(this)) * wxSize(1, 1);
    if (m_appearance.orient == wxHORIZONTAL) {
        pos.y += this->GetSize().GetHeight();
    } else {
        pos.x += this->GetSize().GetWidth();
    }
    return pos;
}

TabsBarCtrl::TabsBarCtrl(wxWindow* parent, int orient, TabsBarMenus* menus /* = nullptr*/) :
    wxControl(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE | wxTAB_TRAVERSAL),
    m_orient(orient),
    m_menus(menus)
{
#ifdef _WIN32
    SetDoubleBuffered(true);
#endif //__WINDOWS__
    update_margins();

    wxAlignment align;
    int margin_flags{0};

    if (orient == wxHORIZONTAL) {
        m_sizer = new wxFlexGridSizer(2);
        m_sizer->AddGrowableCol(0);
        align        = wxALIGN_CENTER_VERTICAL;
        margin_flags = wxLEFT | wxRIGHT;
    } else {
        m_sizer = new wxFlexGridSizer(1);
        m_sizer->AddGrowableRow(0);
        align = wxALIGN_CENTER_HORIZONTAL;
    }
    m_sizer->SetFlexibleDirection(orient);
    this->SetSizer(m_sizer);

    m_first_sizer = new wxBoxSizer(orient);

    m_buttons_sizer = new wxFlexGridSizer(
        1,
        1,
        m_btn_margin,
        (orient == wxVERTICAL && m_compact_mode) ? 0 : m_btn_margin
    );
    m_buttons_sizer->SetFlexibleDirection(wxBOTH);
    m_first_sizer->Add(m_buttons_sizer, 0, align | margin_flags, m_btn_margin);

    m_sizer->Add(m_first_sizer, 1, wxEXPAND);

    m_second_sizer = new wxBoxSizer(orient);
    m_sizer->Add(m_second_sizer, 0, align);
}

void TabsBarCtrl::refresh_buttons()
{
    for (auto* button : m_pageButtons) {
        button->set_compact_mode(m_compact_mode,22);
    }

    if (!m_compact_mode && m_orient == wxVERTICAL) {
        int max_min_width = wxDefaultCoord;
        for (auto* button : m_pageButtons) {
            max_min_width = std::max(max_min_width, button->GetMinSize().GetWidth());
        }
        for (Button* button : m_pageButtons) {
            if (max_min_width > button->GetMinSize().GetWidth()) {
                wxSize new_min_size(max_min_width, button->GetMinSize().y);
                button->SetMinSize(new_min_size);
            }
        }
        wxCommandEvent evt = wxCommandEvent(wxCUSTOMEVT_TABS_BAR_FORCE_FULL_LAYOUT);
        wxPostEvent(this->GetParent(), evt);
    }

    m_first_sizer->Layout();
    Refresh();
}

void TabsBarCtrl::set_compact_mode(bool compact_mode)
{
    if (m_compact_mode == compact_mode)
        return;

    m_compact_mode = compact_mode;

    wxAlignment align{
        m_orient == wxHORIZONTAL ? wxALIGN_CENTER_VERTICAL : wxALIGN_CENTER_HORIZONTAL
    };
    int margin_flags{
        m_orient == wxHORIZONTAL ? wxLEFT | wxRIGHT : (compact_mode ? 0 : wxTOP | wxLEFT | wxRIGHT)
    };

    refresh_buttons();

    m_first_sizer->GetItem(size_t(0))->SetFlag(align | margin_flags);
    m_buttons_sizer->SetHGap((m_orient == wxVERTICAL && m_compact_mode) ? 0 : m_btn_margin);

    on_compact_mode_changed();
}

bool TabsBarCtrl::compact_mode() const
{
    return m_compact_mode;
}

void TabsBarCtrl::enable_buttons(bool enable)
{
    std::ranges::for_each(m_pageButtons, [enable](Button* button) { button->Enable(enable); });
}

void TabsBarCtrl::Rescale()
{
    update_margins();

    m_buttons_sizer->SetVGap(m_btn_margin);
    m_buttons_sizer->SetHGap((m_orient == wxVERTICAL && m_compact_mode) ? 0 : m_btn_margin);

    on_rescale();

    // call Layout before update buttons width to process recaling of the buttons
    m_sizer->Layout();
}

void TabsBarCtrl::OnColorsChanged()
{
    for (Button* button : m_pageButtons) {
        button->sys_color_changed();
    }

    UpdateSelection();
}

wxRect TabsBarCtrl::get_selected_tab_rect()
{
    for (Button* button : m_pageButtons) {
        if (button->is_selected())
            return wxRect(button->GetPosition(), button->GetSize());
    }
    return wxRect();
}

void TabsBarCtrl::UpdateSelection()
{
    for (Button* btn : m_pageButtons)
        btn->set_selected(false);

    if (m_selection >= 0)
        m_pageButtons[m_selection]->set_selected(true);

    Refresh();
    // m_sizer->Layout();
}

void TabsBarCtrl::SetSelection(int sel, bool force /*= false*/)
{
    if (m_selection == sel && !force)
        return;
    m_selection = sel;
    UpdateSelection();
}

bool TabsBarCtrl::InsertPage(
    size_t n,
    const wxString& text,
    bool bSelect /* = false*/,
    const std::string& bmp_name /* = ""*/
)
{
    Button* btn = new Button(this, {text, bmp_name, 22, m_orient});
    btn->set_compact_mode(m_compact_mode, 22);
    btn->Bind(
        wxEVT_BUTTON,
        [this, btn](wxCommandEvent& event)
        {
            if (auto it = std::find(m_pageButtons.begin(), m_pageButtons.end(), btn);
                it != m_pageButtons.end())
            {
                m_selection        = it - m_pageButtons.begin();
                wxCommandEvent evt = wxCommandEvent(wxCUSTOMEVT_TABS_BAR_SEL_CHANGED);
                evt.SetId(m_selection);
                wxPostEvent(this->GetParent(), evt);
                UpdateSelection();
            }
        }
    );

    m_pageButtons.insert(m_pageButtons.begin() + n, btn);
    m_buttons_sizer->Insert(
        n,
        new wxSizerItem(
            btn,
            0,
            m_orient == wxVERTICAL ? wxALIGN_CENTER_HORIZONTAL : wxALIGN_CENTER_VERTICAL
        )
    );
    if (m_orient == wxHORIZONTAL)
        m_buttons_sizer->SetCols(m_buttons_sizer->GetCols() + 1);
    else
        m_buttons_sizer->SetRows(m_buttons_sizer->GetRows() + 1);

    refresh_buttons();

    m_sizer->Layout();
    return true;
}

void TabsBarCtrl::RemovePage(size_t n)
{
    auto btn = m_pageButtons[n];
    m_pageButtons.erase(m_pageButtons.begin() + n);
    m_buttons_sizer->Remove(n);

    // Under OSX call of btn->Reparent(nullptr) causes a crash, so as a workaround use RemoveChild() instead
    this->RemoveChild(btn);
    btn->Destroy();

    refresh_buttons();

    m_sizer->Layout();
}

void TabsBarCtrl::SetPageText(size_t n, const wxString& strText)
{
    auto btn = m_pageButtons[n];
    btn->SetLabel(strText);
}

wxString TabsBarCtrl::GetPageText(size_t n) const
{
    auto btn = m_pageButtons[n];
    return btn->GetLabel();
}

} // namespace Slic3r::App::Desktop
