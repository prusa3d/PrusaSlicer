#include "Slic3r/App/WX/WindowMetrics.hpp"

#include "Slic3r/Biz/Algorithms/StringUtils.hpp"

#include <boost/lexical_cast.hpp>
#include <fmt/format.h>

namespace Slic3r::App::WX {

void on_window_geometry(wxTopLevelWindow* tlw, std::function<void()> callback)
{
#ifdef _WIN32
    // On windows, the wxEVT_SHOW is not received if the window is created maximized
    // cf. https://groups.google.com/forum/#!topic/wx-users/c7ntMt6piRI
    // OTOH the geometry is available very soon, so we can call the callback right away
    callback();
#elif defined __linux__
    tlw->Bind(
        wxEVT_SHOW,
        [=](wxShowEvent& evt)
        {
            // On Linux, the geometry is only available after wxEVT_SHOW + CallAfter
            // cf. https://groups.google.com/forum/?pli=1#!topic/wx-users/fERSXdpVwAI
            tlw->CallAfter([=]() { callback(); });
            evt.Skip();
        }
    );
#elif defined __APPLE__
    tlw->Bind(
        wxEVT_SHOW,
        [=](wxShowEvent& evt)
        {
            callback();
            evt.Skip();
        }
    );
#endif
}

WindowMetrics WindowMetrics::from_window(wxTopLevelWindow* window)
{
    WindowMetrics res;
    res.rect      = window->GetScreenRect();
    res.maximized = window->IsMaximized();
    return res;
}

boost::optional<WindowMetrics> WindowMetrics::deserialize(const std::string& str)
{
    std::vector<std::string> metrics_str;
    metrics_str.reserve(5);

    if (!Biz::Algorithms::unescape_strings_cstyle(str, metrics_str) || metrics_str.size() != 5) {
        return boost::none;
    }

    int metrics[5];
    try {
        for (size_t i = 0; i < 5; i++) {
            metrics[i] = boost::lexical_cast<int>(metrics_str[i]);
        }
    } catch (const boost::bad_lexical_cast&) {
        return boost::none;
    }

    WindowMetrics res;
    res.rect      = wxRect(metrics[0], metrics[1], metrics[2], metrics[3]);
    res.maximized = metrics[4] != 0;

    return res;
}

void WindowMetrics::sanitize_for_display(const wxRect& screen_rect)
{
    rect = rect.Intersect(screen_rect);

    // Prevent the window from going too far towards the right and/or bottom edge
    // It's hardcoded here that the threshold is 80% of the screen size
    rect.x = std::min(rect.x, screen_rect.x + 4 * screen_rect.width / 5);
    rect.y = std::min(rect.y, screen_rect.y + 4 * screen_rect.height / 5);
}

std::string WindowMetrics::serialize() const
{
    return fmt::format(
        "{}; {}; {}; {}; {}",
        rect.x,
        rect.y,
        rect.width,
        rect.height,
        static_cast<int>(maximized)
    );
}

} // namespace Slic3r::App::WX
