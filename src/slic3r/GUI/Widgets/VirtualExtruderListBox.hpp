#pragma once

#include <functional>
#include <vector>

#include <wx/vlbox.h>
#include <wx/colour.h>

#include "../wxExtensions.hpp"

namespace Slic3r::GUI {

class VirtualExtruderListBox : public wxVListBox
{
public:
    static constexpr int ITEM_HEIGHT = 52;

    struct Row
    {
        wxColour swatch;
        wxString title;
        wxString subtitle;
        bool is_gradient = false;
        std::vector<std::pair<wxColour, double>> gradient_stops;

        struct BlendComponent
        {
            wxColour color;
            unsigned int id;
            int percent;
        };

        std::vector<BlendComponent> blend_components;
    };

    std::function<void(int)> on_remove_row;

    VirtualExtruderListBox(wxWindow* parent, const wxSize& size);

    int ItemHeight() const
    {
        return OnMeasureItem(0);
    }

    wxRect TrashIconRect(size_t n) const;

    void SetRows(std::vector<Row> rows);

    const std::vector<Row>& rows() const
    {
        return m_rows;
    }

    int GetMySelection() const
    {
        return m_selected;
    }

    void SetMySelection(int n);

protected:
    void OnDrawItem(wxDC& dc, const wxRect& rect, size_t n) const override;
    wxCoord OnMeasureItem(size_t) const override;
    void OnDrawBackground(wxDC& dc, const wxRect& rect, size_t n) const override;

private:
    static constexpr int ICON_SIZE   = 16;
    static constexpr int ICON_MARGIN = 10;

    std::vector<Row> m_rows;
    int m_selected    = wxNOT_FOUND;
    int m_hovered_row = wxNOT_FOUND;
    ScalableBitmap m_icon_remove;
};

} // namespace Slic3r::GUI
