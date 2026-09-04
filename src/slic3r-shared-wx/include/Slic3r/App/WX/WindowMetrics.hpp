#pragma once

#include <boost/optional.hpp>
#include <wx/wx.h>

class wxTopLevelWindow;

namespace Slic3r::App::WX {

void on_window_geometry(wxTopLevelWindow* tlw, std::function<void()> callback);

class WindowMetrics
{
private:
    wxRect rect;
    bool maximized;

    WindowMetrics() : maximized(false) {}

public:
    static WindowMetrics from_window(wxTopLevelWindow* window);
    static boost::optional<WindowMetrics> deserialize(const std::string& str);

    const wxRect& get_rect() const
    {
        return rect;
    }

    bool get_maximized() const
    {
        return maximized;
    }

    void sanitize_for_display(const wxRect& screen_rect);
    std::string serialize() const;
};

} // namespace Slic3r::App::WX
