#pragma once

#include "Slic3r/App/Yoga/CollapsibleWindow.hpp"
#include "Slic3r/App/Preview/Legend.hpp"

namespace Slic3r::App::Yoga {
class Rectangle;
class ToggleButton;
class ComboBox;
}

namespace Slic3r::App::Preview {

class LegendWindow : public Yoga::CollapsibleWindow
{
public:
    LegendWindow(libvgcode::FdmViewer* viewer, FdmViewerWrapper* wrapper);
    LegendCallbacks& callbacks();
    void update_type_selector(const std::vector<std::string>& types, const std::vector<libvgcode::ViewType>& option_types, int selection);

private:
    void update_show_time_estimate(const libvgcode::FdmViewer& viewer);

private:
    Legend* m_legend{ nullptr };
    Yoga::ComboBox* m_type_selector{ nullptr };
    // Maps a combo box index to the ViewType it represents; the combo omits LayerTime* entries
    // when no layer times are available, so the index cannot be cast to ViewType directly.
    std::vector<libvgcode::ViewType> m_type_option_types;

    Yoga::ToggleButton* m_show_time_estimate{ nullptr };
    Yoga::ToggleButton* m_detail_view{ nullptr };

    Yoga::Rectangle* m_settings{ nullptr };
};

} // namespace Slic3r::App::Preview
