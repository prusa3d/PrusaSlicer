#include "VirtualExtruderListBox.hpp"

#include <algorithm>
#include <cmath>

#include <wx/dcbuffer.h>
#include <wx/dcclient.h>
#include <wx/settings.h>

#include "../ColorUtils.hpp"
#include "../GUI_App.hpp"

namespace Slic3r::GUI {

static wxColour swatch_border_color()
{
    return GUI_App::dark_mode() ? wxColour(0xA0, 0xA0, 0xA0) : wxColour(0x60, 0x60, 0x60);
}

VirtualExtruderListBox::VirtualExtruderListBox(wxWindow* parent, const wxSize& size) :
    wxVListBox(parent, wxID_ANY, wxDefaultPosition, size, wxBORDER_SIMPLE | wxVSCROLL)
{
    this->SetFont(wxGetApp().normal_font());
    m_icon_remove = ScalableBitmap(this, "trash", ICON_SIZE);

    this->Bind(
        wxEVT_MOTION,
        [this](wxMouseEvent& event)
        {
            const wxPoint pos   = this->ScreenToClient(wxGetMousePosition());
            const int first_vis = int(this->GetVisibleBegin());
            const int item_h    = ItemHeight();
            const int row = (pos.y >= 0 && pos.x >= 0 && pos.x < this->GetClientSize().GetWidth()) ?
                (first_vis + pos.y / item_h) :
                wxNOT_FOUND;
            const int new_hover = (row >= 0 && row < int(m_rows.size())) ? row : wxNOT_FOUND;
            if (new_hover != m_hovered_row) {
                m_hovered_row = new_hover;
                this->Refresh();
            }
            const bool over_trash =
                (new_hover != wxNOT_FOUND) && TrashIconRect(size_t(new_hover)).Contains(pos);
            this->SetCursor(over_trash ? wxCursor(wxCURSOR_HAND) : wxNullCursor);
            event.Skip();
        }
    );
    this->Bind(
        wxEVT_LEAVE_WINDOW,
        [this](wxMouseEvent& event)
        {
            if (m_hovered_row != wxNOT_FOUND) {
                m_hovered_row = wxNOT_FOUND;
                this->Refresh();
            }
            event.Skip();
        }
    );
}

wxRect VirtualExtruderListBox::TrashIconRect(size_t n) const
{
    const int item_h    = ItemHeight();
    const int first_vis = int(GetVisibleBegin());
    const int row_y     = (int(n) - first_vis) * item_h;
    return wxRect(
        GetClientSize().GetWidth() - ICON_SIZE - ICON_MARGIN,
        row_y + (item_h - ICON_SIZE) / 2,
        ICON_SIZE,
        ICON_SIZE
    );
}

void VirtualExtruderListBox::SetRows(std::vector<Row> rows)
{
    m_rows = std::move(rows);
    this->SetItemCount(m_rows.size());
    if (m_selected >= int(m_rows.size())) {
        m_selected = wxNOT_FOUND;
    }

    this->RefreshAll();
}

void VirtualExtruderListBox::SetMySelection(int n)
{
    m_selected = n;
    this->Refresh();
    this->Update();
}

void VirtualExtruderListBox::OnDrawItem(wxDC& dc, const wxRect& rect, size_t n) const
{
    if (n >= m_rows.size()) {
        return;
    }

    const Row& row = m_rows[n];

    const bool selected = (int(n) == m_selected);

    const int swatch_size = rect.GetHeight() - 20;
    const int swatch_x    = rect.GetLeft() + 10;
    const int swatch_y    = rect.GetTop() + (rect.GetHeight() - swatch_size) / 2;
    if (row.is_gradient && row.gradient_stops.size() >= 2) {
        auto color_at_t = [&row](double t) -> wxColour
        {
            if (t <= row.gradient_stops.front().second) {
                return row.gradient_stops.front().first;
            }

            if (t >= row.gradient_stops.back().second) {
                return row.gradient_stops.back().first;
            }

            size_t upper = 1;
            while (upper < row.gradient_stops.size() - 1 && row.gradient_stops[upper].second < t) {
                ++upper;
            }

            const auto& lo       = row.gradient_stops[upper - 1];
            const auto& hi       = row.gradient_stops[upper];
            const double segment = hi.second - lo.second;
            const double weight  = (segment > 0.0) ? (t - lo.second) / segment : 0.0;
            return wxColour(
                static_cast<unsigned char>(
                    std::round(lo.first.Red() * (1.0 - weight) + hi.first.Red() * weight)
                ),
                static_cast<unsigned char>(
                    std::round(lo.first.Green() * (1.0 - weight) + hi.first.Green() * weight)
                ),
                static_cast<unsigned char>(
                    std::round(lo.first.Blue() * (1.0 - weight) + hi.first.Blue() * weight)
                )
            );
        };

        for (int dx = 0; dx < swatch_size; ++dx) {
            const double t = (swatch_size > 1) ? double(dx) / double(swatch_size - 1) : 0.0;
            const wxColour stripe_color = color_at_t(t);
            dc.SetPen(wxPen(stripe_color, 1));
            dc.DrawLine(swatch_x + dx, swatch_y, swatch_x + dx, swatch_y + swatch_size);
        }

        dc.SetBrush(*wxTRANSPARENT_BRUSH);
        dc.SetPen(wxPen(swatch_border_color(), 1));
        dc.DrawRoundedRectangle(swatch_x, swatch_y, swatch_size, swatch_size, 3);
    } else {
        dc.SetBrush(wxBrush(row.swatch));
        dc.SetPen(wxPen(swatch_border_color(), 1));
        dc.DrawRoundedRectangle(swatch_x, swatch_y, swatch_size, swatch_size, 3);
    }

    const int text_x = swatch_x + swatch_size + 10;
    dc.SetFont(wxGetApp().bold_font());
    wxCoord title_h = 0;
    dc.GetTextExtent("Mg", nullptr, &title_h);
    dc.SetFont(wxGetApp().small_font());
    wxCoord sub_line_h = 0;
    dc.GetTextExtent("Mg", nullptr, &sub_line_h);
    const int total_text_h = title_h + sub_line_h;
    const int title_y      = rect.GetTop() + (rect.GetHeight() - total_text_h) / 2;
    const int subtitle_y   = title_y + title_h;

    dc.SetTextForeground(
        selected ? wxSystemSettings::GetColour(wxSYS_COLOUR_HIGHLIGHTTEXT) :
                   this->GetForegroundColour()
    );
    dc.SetFont(wxGetApp().bold_font());
    dc.DrawText(row.title, text_x, title_y);

    const wxColour subtitle_fg = [this, selected]()
    {
        if (selected) {
            return wxSystemSettings::GetColour(wxSYS_COLOUR_HIGHLIGHTTEXT);
        }

        const wxColour fg = this->GetForegroundColour();
        const wxColour bg = this->GetBackgroundColour();
        return wxColour(
            (fg.Red() * 6 + bg.Red() * 4) / 10,
            (fg.Green() * 6 + bg.Green() * 4) / 10,
            (fg.Blue() * 6 + bg.Blue() * 4) / 10
        );
    }();
    dc.SetFont(wxGetApp().small_font());

    if (row.blend_components.empty()) {
        dc.SetTextForeground(subtitle_fg);
        dc.DrawText(row.subtitle, text_x, subtitle_y);
    } else {
        wxCoord line_h = 0;
        dc.GetTextExtent("Mg", nullptr, &line_h);
        const int chip_h = line_h;

        wxFont chip_font = wxGetApp().small_font();
        chip_font.MakeBold();

        int x = text_x;

        if (row.is_gradient && !row.subtitle.IsEmpty()) {
            dc.SetFont(wxGetApp().small_font());
            dc.SetTextForeground(subtitle_fg);
            const wxString prefix = row.subtitle + " - ";
            wxCoord pw = 0, ph = 0;
            dc.GetTextExtent(prefix, &pw, &ph);
            dc.DrawText(prefix, x, subtitle_y);
            x += pw;
        }

        for (const Row::BlendComponent& c : row.blend_components) {
            dc.SetFont(chip_font);
            const wxString digit = wxString::Format("%u", c.id);
            wxCoord dw = 0, dh = 0;
            dc.GetTextExtent(digit, &dw, &dh);
            const int chip_w = std::max(chip_h + 6, int(dw + 6));

            dc.SetBrush(wxBrush(c.color));
            dc.SetPen(wxPen(swatch_border_color(), 1));
            dc.DrawRoundedRectangle(x, subtitle_y, chip_w, chip_h, 2);

            dc.SetTextForeground(wcag_contrast_text(c.color));
            dc.DrawText(digit, x + (chip_w - dw) / 2, subtitle_y + (chip_h - dh) / 2);
            x += chip_w + 4;

            if (c.percent >= 0) {
                dc.SetFont(wxGetApp().small_font());
                dc.SetTextForeground(subtitle_fg);
                const wxString label = wxString::Format(" - %d%%", c.percent);
                wxCoord lw = 0, lh = 0;
                dc.GetTextExtent(label, &lw, &lh);
                dc.DrawText(label, x, subtitle_y);
                x += lw + 8;
            }
        }
    }

    if (m_icon_remove.IsOk()) {
        const int icon_x   = rect.GetRight() - ICON_SIZE - ICON_MARGIN;
        const int icon_y   = rect.GetTop() + (rect.GetHeight() - ICON_SIZE) / 2;
        const wxBitmap bmp = m_icon_remove.bmp().GetBitmapFor(this);
        if (bmp.IsOk()) {
            dc.DrawBitmap(bmp, icon_x, icon_y, /*useMask=*/true);
        }
    }
}

wxCoord VirtualExtruderListBox::OnMeasureItem(size_t) const
{
    wxClientDC dc(const_cast<VirtualExtruderListBox*>(this));
    dc.SetFont(wxGetApp().bold_font());
    wxCoord title_h = 0;
    dc.GetTextExtent("Mg", nullptr, &title_h);
    dc.SetFont(wxGetApp().small_font());
    wxCoord sub_h = 0;
    dc.GetTextExtent("Mg", nullptr, &sub_h);
    const int pad = std::max(4, int(sub_h / 3));
    return std::max(ITEM_HEIGHT, int(title_h + sub_h + 3 * pad));
}

void VirtualExtruderListBox::OnDrawBackground(wxDC& dc, const wxRect& rect, size_t n) const
{
    const bool selected = (int(n) == m_selected);
    dc.SetPen(*wxTRANSPARENT_PEN);
    dc.SetBrush(
        selected ? wxBrush(wxSystemSettings::GetColour(wxSYS_COLOUR_HIGHLIGHT)) :
                   wxBrush(this->GetBackgroundColour())
    );
    dc.DrawRectangle(rect);

    if (!selected && n + 1 < m_rows.size()) {
        const wxColour fg = this->GetForegroundColour();
        const wxColour bg = this->GetBackgroundColour();
        const wxColour sep(
            (fg.Red() + bg.Red() * 3) / 4,
            (fg.Green() + bg.Green() * 3) / 4,
            (fg.Blue() + bg.Blue() * 3) / 4
        );
        dc.SetPen(wxPen(sep, 1));
        dc.DrawLine(rect.GetLeft() + 10, rect.GetBottom(), rect.GetRight() - 10, rect.GetBottom());
    }
}

} // namespace Slic3r::GUI
