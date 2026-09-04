#include "Slic3r/App/AbstractRenderLayout.hpp"

#include <imgui_internal.h>

#include "Slic3r/App/Scene/IGizmo.hpp"
#include "Slic3r/App/ToolBar/ToolBar.hpp"
#include "Slic3r/App/ToolBar/ToolBarButton.hpp"
#include "Slic3r/App/Yoga/ImGuiUtils.hpp"
#include "Slic3r/App/Yoga/SplitLayout.hpp"
#include "Slic3r/App/SidebarStackLayout.hpp"
#include "Slic3r/App/AppServices.hpp"
#include "Slic3r/App/AppConfig.hpp"
#include "Slic3r/App/Navigator.hpp"

#include "Slic3r/Assert.hpp"

using namespace Slic3r::App::Yoga;

namespace Slic3r::App {

SidebarStackLayout* AbstractRenderLayout::sidebar_stack_layout() const
{
    return m_layout_sidebar_stack_layout;
}

void AbstractRenderLayout::save_column_sizes()
{
    AppServices::instance().app_config().set<double>(
        "layout_main_left_column_width",
        static_cast<double>(m_layout_left_column->width())
    );
    AppServices::instance().app_config().set<double>(
        "layout_main_right_column_width",
        static_cast<double>(m_layout_right_column->width())
    );
}

void AbstractRenderLayout::load_column_sizes()
{
    if (m_layout_left_column && m_layout_right_column) {
        m_layout_left_column->set_width(
            AppServices::instance().app_config().get<double>("layout_main_left_column_width")
        );
        m_layout_right_column->set_width(
            AppServices::instance().app_config().get<double>("layout_main_right_column_width")
        );
        m_layout_main_bottom->invalidate();
    }
}

void AbstractRenderLayout::on_app_config_changed(const std::string& key) {}

void AbstractRenderLayout::update_cube_view_position()
{
    // When sidebars are visible and object list is collapsed
    // move cube_view to left_column
    // otherwise put it in the scene_row
    enum class Location
    {
        LeftColumn,
        SceneRow
    };

    auto does_cube_fits = [this]() -> bool
    {
        const float item_size = m_cube_view->height() + m_cube_view->margin().result_vertical();
        float available_size =
            m_layout_left_column->height() - m_layout_left_column->padding().result_vertical();
        for (Item* child : m_layout_left_column->items()) {
            // ignore Objects and CubeViewWrapper (that one flexes)
            if (!child || m_cube_view_wrapper == child) {
                continue;
            }
            available_size -= (child->height() + child->margin().result_vertical());
            available_size -= m_layout_left_column->gap().value; // Always count gap
        }

        return item_size <= available_size;
    };

    const Location current_location =
        m_layout_scene_row->index_of(m_cube_view_wrapper).has_value() ? Location::SceneRow :
                                                                        Location::LeftColumn;
    const Location target_location =
        m_sidebars_visible && (m_object_list->collapsed() || does_cube_fits()) ?
        Location::LeftColumn :
        Location::SceneRow;

    if (current_location == target_location) {
        return;
    }

    if (target_location == Location::LeftColumn) {
        m_layout_left_column->append(m_layout_scene_row->remove(m_cube_view_wrapper));
        m_cube_view_wrapper->set_self_align(YGAlign::YGAlignFlexStart);
        m_cube_view_wrapper->set_flex_grow(1);
    } else {
        m_layout_scene_row->prepend(m_layout_left_column->remove(m_cube_view_wrapper));
        m_cube_view_wrapper->set_self_align(YGAlign::YGAlignFlexEnd);
        m_cube_view_wrapper->set_flex_grow(0);
    }
}

void AbstractRenderLayout::update_left_separator_enable()
{
    // everytime a collapse state change in any of the CollapsibleWindow
    // located in Left column is done we need to update
    // SplitLayout left column separator enable

    bool enabled = false;
    for (size_t child_index = 0; child_index < m_layout_left_column->object_count(); ++child_index)
    {
        CollapsibleWindow* window =
            dynamic_cast<CollapsibleWindow*>(m_layout_left_column->get_item(child_index));
        if (window && !window->collapsed()) {
            enabled = true;
            break;
        }
    }

    m_layout_main_bottom->set_separator_enable(0, enabled);
}

ToolBarButton*
AbstractRenderLayout::add_toolbar_item(ToolbarID id, Render::Icon icon, const std::string& tooltip)
{
    ToolBar* toolbar = find_toolbar(id);
    ASSERT(toolbar);

    toolbar->set_visible(true);

    ToolBarButton* button = toolbar->emplace_back<ToolBarButton>(icon, tooltip);

    return button;
}

ToolBarButton* AbstractRenderLayout::add_toolbar_item_checkable(
    ToolbarID id,
    Render::Icon icon,
    const std::string& tooltip,
    bool checked
)
{
    ToolBarButton* button = add_toolbar_item(id, icon, tooltip);
    ASSERT(button);

    button->set_checked(checked);
    button->set_checkable(true);

    return button;
}

ToolBarSwitchButton* AbstractRenderLayout::add_toolbar_item_switch(
    ToolbarID id,
    Render::Icon icon,
    const std::string& label,
    const std::string& tooltip,
    ToolBarSwitchButton::SwitchPosition switch_position
)
{
    ToolBar* toolbar = find_toolbar(id);
    ASSERT(toolbar);

    toolbar->set_visible(true);

    ToolBarSwitchButton* switch_button =
        toolbar->emplace_back<ToolBarSwitchButton>(switch_position, icon, label, tooltip);
    m_switch_buttons.push_back(switch_button);

    return switch_button;
}

ToolBar* AbstractRenderLayout::find_toolbar(ToolbarID id) const
{
    switch (id) {
    case ToolbarID::ToolLeft:
        return m_tool_left_toolbar;
    case ToolbarID::ToolRight:
        return m_tool_right_toolbar;
    case ToolbarID::Mode:
        return m_mode_toolbar;
    }
    return nullptr;
}

ToolBar* AbstractRenderLayout::mode_toolbar() const
{
    return m_mode_toolbar;
}

ToolBar* AbstractRenderLayout::tool_right_toolbar() const
{
    return m_tool_right_toolbar;
}

ToolBar* AbstractRenderLayout::tool_left_toolbar() const
{
    return m_tool_left_toolbar;
}

void AbstractRenderLayout::set_sidebars_visible(bool visible)
{
    if (m_sidebars_visible != visible) {
        m_sidebars_visible = visible;

        m_layout_main_bottom->set_visible_child(m_layout_right_column, m_sidebars_visible);
        m_layout_main_bottom->set_visible_child(m_layout_left_column, m_sidebars_visible);

        update_cube_view_position();
    }
}

void AbstractRenderLayout::init()
{
    m_layout_main.set_padding(0);
    m_layout_main.set_gap(0);
    m_layout_main.set_orientation(Orientation::Vertical);

    m_layout_main.append(m_top_bar.release());

    m_layout_main_bottom = m_layout_main.emplace_back<SplitLayout>();
    m_layout_main_bottom->set_orientation(Orientation::Horizontal);
    m_layout_main_bottom->set_padding(Paddings(5.f, 5.f));
    m_layout_main_bottom->set_flex_grow(1.0);

    init_left_column();

    init_middle_column();

    init_right_column();

    m_layout_main.append(m_preferences_dialog.release());
    m_preferences_dialog->attach_to_center();

    m_layout_main.append(m_numbers_entry_dialog.release());
    m_numbers_entry_dialog->attach_to_center();

    m_layout_main.append(m_crashed_projects_dialog.release());
    m_crashed_projects_dialog->attach_to_center();

    m_layout_main.append(m_preset_updater_dialog.release());
    m_preset_updater_dialog->attach_to_center();
}

void AbstractRenderLayout::init_left_column()
{
    m_layout_left_column = m_layout_main_bottom->emplace_back<Item>();
    m_layout_left_column->set_min_width(280.f);
    m_layout_left_column->set_justify_content(YGJustifyFlexStart);
    m_layout_left_column->set_width(
        AppServices::instance().app_config().get<double>("layout_main_left_column_width")
    );
    m_layout_left_column->set_orientation(Orientation::Vertical);
    m_layout_left_column->set_gap(5);

    m_object_list->collapsible_window_callbacks().collapsed_changed = [this](bool collapsed)
    {
        update_cube_view_position();
        update_left_separator_enable();
        m_navigator.set_object_list_collapsed(collapsed);
    };

    m_layout_left_column->item_callbacks().size_changed = [this]()
    {
        update_cube_view_position();
        update_left_separator_enable();
    };
    m_object_list->item_callbacks().size_changed = [this]()
    {
        update_cube_view_position();
        update_left_separator_enable();
    };
    m_layout_left_column->append(m_object_list.release());
}

void AbstractRenderLayout::init_middle_column()
{
    m_layout_center_row = m_layout_main_bottom->emplace_back<Item>();
    m_layout_center_row->set_min_width(345);
    m_layout_center_row->set_orientation(Orientation::Horizontal);
    m_layout_center_row->set_gap(5);
    m_layout_main_bottom->set_flex_child(m_layout_center_row, true);

    m_layout_middle_column = m_layout_center_row->emplace_back<Item>();
    m_layout_middle_column->set_orientation(Orientation::Vertical);
    m_layout_middle_column->set_gap(5);
    m_layout_middle_column->set_flex_grow(1);
    m_layout_middle_column->set_object_name("MiddleColumn");

    init_toolbar_row();

    // Row for CubeView (left) and Notification column (right)
    m_layout_scene_row = m_layout_middle_column->emplace_back<Yoga::Item>();
    m_layout_scene_row->set_orientation(Orientation::Horizontal);
    m_layout_scene_row->set_flex_grow(1);

    m_cube_view_wrapper = m_layout_scene_row->emplace_back<Item>();
    m_cube_view_wrapper->set_self_align(YGAlignFlexEnd);
    m_cube_view_wrapper->set_justify_content(YGJustifyFlexEnd);
    m_cube_view_wrapper->set_align_items(YGAlignFlexStart);
    m_cube_view_wrapper->set_orientation(Orientation::Vertical);
    m_cube_view_wrapper->append(m_cube_view.release());

    Item* spacer = m_layout_scene_row->emplace_back<Item>();
    spacer->set_flex_grow(1);

    m_layout_scene_row->append(m_pop_notification_list_view.release());
    m_pop_notification_list_view->set_orientation(Orientation::Vertical);
    m_pop_notification_list_view->set_max_width(400.f);
    m_pop_notification_list_view->set_max_height(200.f);
    m_pop_notification_list_view->set_flex_grow(1);
    m_pop_notification_list_view->set_margin(10.);
    m_pop_notification_list_view->set_self_align(YGAlignFlexEnd);
    m_pop_notification_list_view->set_justify_content(YGJustifyFlexEnd);
    m_pop_notification_list_view->set_flex_shrink(1);
    m_pop_notification_list_view->set_source_list(
        &AppServices::instance().pop_notification_center().source_list()
    );
}

void AbstractRenderLayout::init_right_column()
{
    m_layout_right_column = m_layout_main_bottom->emplace_back<Item>();
    m_layout_right_column->set_orientation(Orientation::Vertical);
    m_layout_right_column->set_gap(5);
    m_layout_right_column->set_width(
        AppServices::instance().app_config().get<double>("layout_main_right_column_width")
    );
    m_layout_right_column->set_min_width(240);

    m_layout_sidebar_stack_layout = m_layout_right_column->emplace_back<SidebarStackLayout>();
    m_layout_sidebar_stack_layout->set_orientation(Orientation::Vertical);
    m_layout_sidebar_stack_layout->set_flex_grow(1);

    ItemPtr sidebar_bed_print = std::make_unique<Item>();
    sidebar_bed_print->set_orientation(Orientation::Vertical);
    sidebar_bed_print->set_gap(5);
    sidebar_bed_print->set_flex_grow(1);
    sidebar_bed_print->append(m_sidebar_bed.release());
    sidebar_bed_print->append(m_sidebar_print.release());
    m_layout_sidebar_stack_layout->insert_item(
        SidebarStackLayout::ItemType::Bed,
        std::move(sidebar_bed_print)
    );

    m_layout_sidebar_stack_layout->insert_item(
        SidebarStackLayout::ItemType::Object,
        m_sidebar_object.release()
    );
}

void AbstractRenderLayout::init_toolbar_row()
{
    constexpr float button_size = 40.f;

    m_layout_middle_toolbar_row = m_layout_middle_column->emplace_back<Item>();
    m_layout_middle_toolbar_row->set_orientation(Orientation::Horizontal);
    m_layout_middle_toolbar_row->set_z(1); // Increaze Z so toolbars can be on top of double sliders

    m_mode_toolbar = m_layout_middle_toolbar_row->emplace_back<ToolBar>("BottomToolbar");
    m_mode_toolbar->set_button_width(button_size);
    m_mode_toolbar->set_button_height(button_size);
    m_mode_toolbar->set_orientation(Orientation::Horizontal);
    m_mode_toolbar->set_flex_shrink(0);
    m_mode_toolbar->set_visible(false);

    Item* tool_container = m_layout_middle_toolbar_row->emplace_back<Item>();
    tool_container->set_gap(5_fpx);
    tool_container->set_flex_grow(1);
    tool_container->set_justify_content(YGJustifyCenter);

    m_tool_left_toolbar = tool_container->emplace_back<ToolBar>("TopToolbar");
    m_tool_left_toolbar->set_button_width(button_size);
    m_tool_left_toolbar->set_button_height(button_size);
    m_tool_left_toolbar->set_orientation(Orientation::Horizontal);
    m_tool_left_toolbar->set_flex_shrink(0);
    m_tool_left_toolbar->set_visible(false);

    m_tool_right_toolbar = tool_container->emplace_back<ToolBar>("MiddleToolbar");
    m_tool_right_toolbar->set_button_width(button_size);
    m_tool_right_toolbar->set_button_height(button_size);
    m_tool_right_toolbar->set_orientation(Orientation::Horizontal);
    m_tool_right_toolbar->set_collapsible(true);
    m_tool_right_toolbar->set_visible(false);
    m_tool_right_toolbar->toolbar_callbacks().expand_margin = [this]()
    {
        float margin = 0.f;
        for (ToolBarSwitchButton* switch_button : std::as_const(m_switch_buttons)) {
            margin += switch_button->width_with_label() - switch_button->width();
        }
        return margin;
    };
    m_tool_right_toolbar->toolbar_callbacks().has_enough_space_changed =
        [this](bool has_enough_space)
    {
        for (ToolBarSwitchButton* switch_button : std::as_const(m_switch_buttons)) {
            switch_button->set_show_label(has_enough_space);
        }
    };
}

AbstractRenderLayout::AbstractRenderLayout(
    Navigator& navigator,
    std::unique_ptr<TopBar> top_bar,
    std::unique_ptr<PreferencesDialog> preferences_dialog,
    std::unique_ptr<ObjectListWindow> object_list,
    std::unique_ptr<CubeView> cube_view,
    std::unique_ptr<PopNotification::PopNotificationListView> pop_notification_list_view,
    std::unique_ptr<SidebarBed> sidebar_bed,
    std::unique_ptr<SidebarPrint> sidebar_print,
    std::unique_ptr<SidebarObject> sidebar_object,
    std::unique_ptr<NumberEntryDialog> numbers_entry_dialog,
    std::unique_ptr<CrashedProjectsDialog> crashed_projects_dialog,
    std::unique_ptr<PresetUpdaterDialog> preset_updater_dialog
) :
    m_navigator(navigator),
    m_top_bar(std::move(top_bar)),
    m_object_list(std::move(object_list)),
    m_cube_view(std::move(cube_view)),
    m_pop_notification_list_view(std::move(pop_notification_list_view)),
    m_sidebar_bed(std::move(sidebar_bed)),
    m_sidebar_print(std::move(sidebar_print)),
    m_sidebar_object(std::move(sidebar_object)),
    m_preferences_dialog(std::move(preferences_dialog)),
    m_numbers_entry_dialog(std::move(numbers_entry_dialog)),
    m_crashed_projects_dialog(std::move(crashed_projects_dialog)),
    m_preset_updater_dialog(std::move(preset_updater_dialog))
{}

AbstractRenderLayout::~AbstractRenderLayout()
{
    save_column_sizes();
}

void AbstractRenderLayout::set_size_info_from_screen(const Render::ScreenInfo& screen_info)
{
    m_info.dpi_scale_factor = screen_info.scale();
    m_info.dpi = screen_info.dpi() > 0 ? screen_info.dpi() :
                                         static_cast<int>(96.f * m_info.dpi_scale_factor);
    m_info.viewport_size_x   = static_cast<int>(screen_info.logical_width());
    m_info.viewport_size_y   = static_cast<int>(screen_info.logical_height());
    m_info.root_font_size    = screen_info.root_font_size();
    m_info.root_font_size_pt = screen_info.root_font_size_pt();
}

void AbstractRenderLayout::render()
{
    SetOurStyleVars our_vars;
    m_layout_main.root_render(m_info);
}

} // namespace Slic3r::App
