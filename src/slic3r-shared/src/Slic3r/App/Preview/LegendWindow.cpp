#include "Slic3r/App/Preview/LegendWindow.hpp"

#include "Slic3r/App/Yoga/Text.hpp"
#include "Slic3r/App/Yoga/ComboBox.hpp"
#include "Slic3r/App/Yoga/Validator.hpp"
#include "Slic3r/App/Yoga/Rectangle.hpp"
#include "Slic3r/App/Yoga/ToggleButton.hpp"
#include "Slic3r/App/Yoga/ScrollArea.hpp"
#include "Slic3r/App/Imgui/ImguiExtension.hpp"

#include <Slic3r/App/libvgcode/Types.hpp>
#include <Slic3r/App/libvgcode/FdmViewer.hpp>

#include "Slic3r/Biz/I18N/I18N.hpp"

namespace Slic3r::App::Preview {

using namespace Slic3r::App::Yoga;
using namespace Slic3r::Biz;

LegendWindow::LegendWindow(libvgcode::FdmViewer* viewer, FdmViewerWrapper* wrapper)
    : CollapsibleWindow(_u8L("Legend"), "LegendWindow")
{
    content()->set_padding(0.f);
    Item* scroll_area = content()->emplace_back<ScrollArea>();
    scroll_area->set_orientation(Orientation::Vertical);
    scroll_area->set_padding(Paddings(20.f, 10.f, 20.f, 0.f));
    m_legend = scroll_area->emplace_back<Legend>(viewer, wrapper);
    m_legend->set_flex_shrink(0.f);

    m_show_time_estimate = content()->emplace_back<ToggleButton>(_u8L("Used filament"));
    m_show_time_estimate->set_padding(Paddings(20.f, 5.f));
    m_show_time_estimate->set_visible(false);
    m_show_time_estimate->set_checked(true);
    m_show_time_estimate->callbacks().checked_changed = [this](bool checked) { 
        m_legend->set_show_time_estimate(checked);
        m_show_time_estimate->set_label(checked ? _u8L("Used filament") : _u8L("Time estimate"));
        m_show_time_estimate->set_tooltip(checked ? _u8L("Switch to show used filament").c_str() : _u8L("Switch to show time estimate"));
    };

    m_settings = content()->emplace_back<Rectangle>();
    m_settings->set_flex_shrink(0.f);
    m_settings->set_fill(
        Imgui::adjust_brightness(m_theme->color_imgui(Platform::Color::WindowBg), 1.25f)
    );
    m_settings->set_flags(ImDrawFlags_RoundCornersBottom);
    m_settings->set_padding(Paddings(20.f, 15.f));
    m_settings->set_gap(10.f);

    // change to combobox later
    m_type_selector = m_settings->emplace_back<ComboBox>("Types");
    m_type_selector->set_flags(ImGuiComboFlags_HeightLargest);
    m_type_selector->set_flex_grow(1.0);
    m_type_selector->callbacks().selection_changed = [this, viewer](int current_index) {
        if (current_index < 0 || current_index >= int(m_type_option_types.size()))
            return;
        viewer->set_view_type(m_type_option_types[current_index]);
        if (m_legend->callbacks().cb_view_type_changed)
            m_legend->callbacks().cb_view_type_changed();
        update_show_time_estimate(*viewer);
    };

    m_detail_view = m_settings->emplace_back<ToggleButton>(_u8L("Detail view"));
    m_detail_view->set_font_type(Render::ImguiFontType::Bold);
    m_detail_view->callbacks().checked_changed = [this, viewer](bool checked) {
        m_legend->set_detail_view(checked);
        update_show_time_estimate(*viewer);
    };
}

LegendCallbacks& LegendWindow::callbacks()
{
    return m_legend->callbacks();
}

void LegendWindow::update_type_selector(const std::vector<std::string>& types, const std::vector<libvgcode::ViewType>& option_types, int selection)
{
    m_type_option_types = option_types;
    m_type_selector->set_items(types);
    m_type_selector->set_current_index(selection);
}

void LegendWindow::update_show_time_estimate(const libvgcode::FdmViewer& viewer)
{
    libvgcode::ViewType type = viewer.view_type();
    bool is_visible = m_detail_view->checked() && (type == libvgcode::ViewType::FeatureType ||
        (type == libvgcode::ViewType::ColorPrint && viewer.has_gcode_events_to_show()));
    m_show_time_estimate->set_visible(is_visible);
}

} // namespace Slic3r::App::Preview
