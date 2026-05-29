#pragma once

#include <vector>

#include <wx/panel.h>
#include <wx/colour.h>

namespace Slic3r::GUI {

class LayerSequenceBar : public wxPanel
{
public:
    static constexpr int MIN_CELL_COUNT = 24;

    LayerSequenceBar(wxWindow* parent);

    void SetCycle(std::vector<wxColour> cycle);

private:
    std::vector<wxColour> m_cycle;

    void on_paint(wxPaintEvent&);
};

} // namespace Slic3r::GUI
