#include "LayerSequenceBar.hpp"

#include <algorithm>
#include <cmath>

#include <wx/dcbuffer.h>
#include <wx/dcclient.h>

#include "../GUI_App.hpp"

namespace Slic3r::GUI {

LayerSequenceBar::LayerSequenceBar(wxWindow* parent) :
    wxPanel(parent, wxID_ANY, wxDefaultPosition, wxSize(-1, 16))
{
    this->SetBackgroundStyle(wxBG_STYLE_PAINT);
    this->SetMinSize(wxSize(240, 16));
    this->Bind(wxEVT_PAINT, &LayerSequenceBar::on_paint, this);
    this->Bind(
        wxEVT_SIZE,
        [this](wxSizeEvent& e)
        {
            this->Refresh();
            e.Skip();
        }
    );
}

void LayerSequenceBar::SetCycle(std::vector<wxColour> cycle)
{
    m_cycle = std::move(cycle);
    this->Refresh();
}

void LayerSequenceBar::on_paint(wxPaintEvent&)
{
    wxAutoBufferedPaintDC dc(this);
    const wxSize sz = this->GetClientSize();
    const wxRect rect(0, 0, sz.GetWidth(), sz.GetHeight());
    if (rect.GetWidth() <= 0 || rect.GetHeight() <= 0) {
        return;
    }

    const bool dark = GUI_App::dark_mode();
    dc.SetBackground(wxBrush(this->GetParent()->GetBackgroundColour()));
    dc.Clear();

    dc.SetPen(*wxTRANSPARENT_PEN);
    if (m_cycle.empty()) {
        dc.SetBrush(wxBrush(dark ? wxColour(0x50, 0x50, 0x50) : wxColour(0xC0, 0xC0, 0xC0)));
        dc.DrawRectangle(rect);
    } else {
        const int cells = std::max<int>(MIN_CELL_COUNT, int(m_cycle.size()));
        for (int i = 0; i < cells; ++i) {
            const int x0 = int(std::round(double(i) * rect.GetWidth() / double(cells)));
            const int x1 = int(std::round(double(i + 1) * rect.GetWidth() / double(cells)));
            dc.SetBrush(wxBrush(m_cycle[size_t(i) % m_cycle.size()]));
            dc.DrawRectangle(x0, rect.GetTop(), x1 - x0, rect.GetHeight());
        }
    }

    dc.SetPen(wxPen(dark ? wxColour(0x70, 0x70, 0x70) : wxColour(0xAA, 0xAA, 0xAA), 1));
    dc.SetBrush(*wxTRANSPARENT_BRUSH);
    dc.DrawRectangle(rect);
}

} // namespace Slic3r::GUI
