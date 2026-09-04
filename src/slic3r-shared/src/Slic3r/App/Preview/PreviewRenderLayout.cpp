#include "Slic3r/App/Preview/PreviewRenderLayout.hpp"

#include "Slic3r/App/Preview/GCodeWindow.hpp"
#include "Slic3r/App/Preview/LegendWindow.hpp"
#include "Slic3r/App/Preview/DoubleSliderForLayers.hpp"
#include "Slic3r/App/Preview/DoubleSliderForGCode.hpp"
#include "Slic3r/App/Preview/SidebarAutoReslice.hpp"
#include "Slic3r/App/Preview/SidebarPreviewActionButtons.hpp"
#include "Slic3r/App/InvalidDataDialog.hpp"
#include "Slic3r/App/SidebarStackLayout.hpp"

using namespace Slic3r::App::Yoga;

namespace Slic3r::App::Preview {

PreviewRenderLayout::PreviewRenderLayout(
    Navigator& navigator,
    std::unique_ptr<TopBar> top_bar,
    std::unique_ptr<PreferencesDialog> preferences_dialog,
    std::unique_ptr<ObjectListWindow> object_list,
    std::unique_ptr<CubeView> cube_view,
    std::unique_ptr<PopNotification::PopNotificationListView> pop_notification_list_view,
    std::unique_ptr<SidebarBed> sidebar_bed,
    std::unique_ptr<SidebarPrint> sidebar_print,
    std::unique_ptr<SidebarObject> sidebar_object,
    std::unique_ptr<SidebarPreviewActionButtons> sidebar_action_buttons,
    std::unique_ptr<GCodeWindow> m_gcode_window,
    std::unique_ptr<LegendWindow> legend,
    std::unique_ptr<DoubleSliderForLayers> double_slider_layers,
    std::unique_ptr<DoubleSliderForLayers> sla_double_slider_layers,
    std::unique_ptr<DoubleSliderForGcode> double_slider_gcode,
    std::unique_ptr<SidebarAutoReslice> sidebar_auto_reslice,
    std::unique_ptr<NumberEntryDialog> numbers_entry_dialog,
    std::unique_ptr<InvalidDataDialog> invalid_data_dialog,
    std::unique_ptr<CrashedProjectsDialog> crashed_projects_dialog,
    std::unique_ptr<PresetUpdaterDialog> preset_updater_dialog
) :
    AbstractRenderLayout(
        navigator,
        std::move(top_bar),
        std::move(preferences_dialog),
        std::move(object_list),
        std::move(cube_view),
        std::move(pop_notification_list_view),
        std::move(sidebar_bed),
        std::move(sidebar_print),
        std::move(sidebar_object),
        std::move(numbers_entry_dialog),
        std::move(crashed_projects_dialog),
        std::move(preset_updater_dialog)
    ),
    m_gcode_window(std::move(m_gcode_window)),
    m_legend(std::move(legend)),
    m_double_slider_layers(std::move(double_slider_layers)),
    m_sla_double_slider_layers(std::move(sla_double_slider_layers)),
    m_double_slider_gcode(std::move(double_slider_gcode)),
    m_sidebar_auto_reslice(std::move(sidebar_auto_reslice)),
    m_sidebar_action_buttons(std::move(sidebar_action_buttons)),
    m_invalid_data_dialog(std::move(invalid_data_dialog))
{}

PreviewRenderLayout::~PreviewRenderLayout() = default;

void PreviewRenderLayout::init()
{
    AbstractRenderLayout::init();

    m_layout_main.append(m_invalid_data_dialog.release());
    m_invalid_data_dialog->attach_to_center();
}

void PreviewRenderLayout::init_left_column()
{
    AbstractRenderLayout::init_left_column();

    m_layout_left_column->append(m_legend.release());
    m_legend->set_visible(false);
    m_legend->collapsible_window_callbacks().collapsed_changed = [this](bool collapsed)
    { update_left_separator_enable(); };
}

void PreviewRenderLayout::init_middle_column()
{
    AbstractRenderLayout::init_middle_column();

    // Workaround: Preview have double slider for layers which takes some size
    // SplitLayout unfortunately doesnt take in account all the min sizes
    // of their children.
    m_layout_center_row->set_min_width(465);

    m_layout_middle_column->append(m_double_slider_gcode.release());

    m_layout_center_row->append(m_double_slider_layers.release());
    m_layout_center_row->append(m_sla_double_slider_layers.release());
}

void PreviewRenderLayout::init_right_column()
{
    AbstractRenderLayout::init_right_column();

    m_layout_sidebar_stack_layout->insert_item(
        SidebarStackLayout::ItemType::GCode,
        m_gcode_window.release()
    );
    m_gcode_window->set_visible(false);
    m_gcode_window->set_flex_grow(1);

    m_layout_right_column->append(m_sidebar_auto_reslice.release());

    m_layout_right_column->append(m_sidebar_action_buttons.release());
}

} // namespace Slic3r::App::Preview
