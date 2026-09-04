#include "Slic3r/App/TopBar.hpp"

#include "Slic3r/App/Yoga/Item.hpp"
#include "Slic3r/App/Yoga/LayoutButton.hpp"
#include "Slic3r/App/Yoga/Menu.hpp"
#include "Slic3r/App/Yoga/MenuItem.hpp"
#include "Slic3r/App/Yoga/Icon.hpp"
#include "Slic3r/App/Yoga/Separator.hpp"

#include "Slic3r/App/SearchBar.hpp"
#include "Slic3r/App/Platform/AbstractRenderModule.hpp"
#include "Slic3r/App/MenuBuilder.hpp"
#include "Slic3r/App/MenuManager.hpp"
#include "Slic3r/App/AppServices.hpp"
#include "Slic3r/App/AppConfig.hpp"
#include "Slic3r/App/CommandBindingManager.hpp"
#include "Slic3r/App/Platform/CommandName.hpp"
#include "Slic3r/App/Lua/PluginSystem.hpp"
#include "Slic3r/App/ProjectButton.hpp"

#include "Slic3r/Biz/I18N/I18N.hpp"

#include <imgui/imgui_internal.h>

namespace Slic3r::App {

using namespace Yoga;
using namespace Slic3r::Biz;

using CommandName = Platform::CommandName;

TopBar::TopBar(
    Biz::ProjectInteractor* project_interactor,
    Platform::AbstractRenderModule* render_module,
    Navigator& navigator,
    App::Undo::Store* undo_store,
    ProjectSaver& project_saver,
    Lua::PluginSystem* plugin_system
) :
    Window("TopBar"),
    m_selected_project_changed_listener_scope(*project_interactor, *this),
    m_projects_changed_listener_scope(*project_interactor, *this),
    m_plugin_rescan_listener_scope(
        plugin_system == nullptr ?
            std::nullopt :
            std::make_optional<decltype(m_plugin_rescan_listener_scope)::value_type>(
                *plugin_system,
                *this
            )
    ),
#ifndef USE_NATIVE_MENU
    m_menu_updated_listener_scope(render_module->menu_manager(), *this),
#endif
    m_project_interactor(*project_interactor),
    m_project_saver(project_saver),
    m_render_module(render_module),
    m_navigator(navigator),
    m_menu_command_registrar(*render_module, *project_interactor, navigator, project_saver),
    m_menu_manager(render_module->menu_manager()),
    m_undo_store(undo_store),
    m_plugin_system(plugin_system)
{
    m_menu_command_registrar.register_top_bar_menus(plugin_system);
    if (m_undo_store) {
        m_undo_store->add_listener<Undo::IStoreChangedListener>(this);
    }

    set_padding(0);
    set_rounding(0.f);
    set_gap(0.f);
    set_flex_shrink(0);
    set_height(35_fpx);

    Item* left_wrapper = emplace_back<Item>();
    left_wrapper->set_flex_shrink(0);
    left_wrapper->set_align_items(YGAlignCenter);

#ifndef USE_NATIVE_MENU
    add_menu_btns(left_wrapper);
    for (LayoutButton* btn : std::initializer_list<LayoutButton*>{m_save_btn, m_show_ui_btn}) {
        if (!btn)
            continue;
        btn->callbacks().hovered_changed = [this](bool hovered)
        {
            if (hovered) {
                if (m_file_menu->opened())
                    m_file_menu->close();
                if (m_main_menu->opened())
                    m_main_menu->close();
            }
        };
    }

    left_wrapper->emplace_back<Separator>(Orientation::Vertical);
#endif // !USE_NATIVE_MENU

    add_save_project_btn(left_wrapper);
    m_undo_redo_wrapper = left_wrapper->emplace_back<Item>();
    m_undo_redo_wrapper->set_min_width(100_fpx);
    if (m_undo_store) {
        add_undo_btn(m_undo_redo_wrapper);
        add_redo_btn(m_undo_redo_wrapper);
    }
    add_show_ui_btn(left_wrapper);

    m_list_view = emplace_back<ProjectButtonListView>(ProjectButtonFactory{m_project_interactor, m_project_saver});
    m_list_view->set_window_flags(
        ImGuiWindowFlags_HorizontalScrollbar // compute horizontal scroll range
        | ImGuiWindowFlags_NoScrollbar // don't show any scrollbar
        | ImGuiWindowFlags_NoScrollWithMouse // don't let ImGui consume wheel vertically
    );
    m_list_view->set_remap_horizontal_scroll(true);
    m_list_view->set_source_list(&project_interactor->observable_project_list());

    Item* project_actions_wrapper = emplace_back<Item>();
    project_actions_wrapper->set_flex_grow(1.);
    project_actions_wrapper->set_align_items(YGAlignCenter);
    project_actions_wrapper->set_padding({5, 0});
    project_actions_wrapper->set_flex_shrink(0);

    LayoutButton* add_new_project_btn = project_actions_wrapper->emplace_back<LayoutButton>(
        std::string{},
        Render::Icon::Plus,
        Biz::_u8L("Add new project")
    );
    add_new_project_btn->callbacks().action = [this] { m_project_interactor.new_project(); };

    Item* right_wrapper = emplace_back<Item>();
    right_wrapper->set_flex_shrink(0);
    m_search_bar = right_wrapper->emplace_back<SearchBar>(m_project_interactor, m_navigator);

    for (LayoutButton* btn :
         std::initializer_list<LayoutButton*>{
             m_save_btn,
             m_undo_btn,
             m_undo_stack_btn,
             m_redo_btn,
             m_redo_stack_btn,
             m_show_ui_btn,
             m_main_menu_btn,
             m_file_menu_btn,
             add_new_project_btn
         })
    {
        if (!btn) {
            continue;
        }
        btn->set_background_color(Platform::Color::ButtonTransparent);
        btn->set_tooltip_position(Position::Bottom);
        btn->set_content_align_items(YGAlignCenter);
        btn->icon_object()->set_height(16_fpx);
        btn->icon_object()->set_width(16_fpx);
        btn->set_height(35_fpx);
        btn->set_content_padding({7_fpx, 0});
        btn->set_draw_flags(ImDrawFlags_None);
    }

    if (m_main_menu_btn) {
        m_main_menu_btn->icon_object()->set_width(24_fpx);
        m_main_menu_btn->icon_object()->set_height(24_fpx);
    }
    add_new_project_btn->set_width(22_fpx);
    add_new_project_btn->set_height(22_fpx);
    add_new_project_btn->set_content_padding({});
    // This pixel manipulation is just to force plus icon to be
    // same size as close project icon
    add_new_project_btn->icon_object()->set_width(20_fpx);
    add_new_project_btn->icon_object()->set_height(20_fpx);

    if (m_undo_stack_btn) {
        m_undo_stack_btn->set_content_padding({2_fpx, 0});
    }
    if (m_redo_stack_btn) {
        m_redo_stack_btn->set_content_padding({2_fpx, 0});
    }

    left_wrapper->set_padding(5_fpx);
    right_wrapper->set_padding(5_fpx);

    if (m_project_interactor.selected_project_id() != Domain::INVALID_ID) {
        on_selected_project_changed(m_project_interactor.selected_project_id());
    }
}

TopBar::~TopBar()
{
    if (m_undo_store) {
        m_undo_store->remove_listener<Undo::IStoreChangedListener>(this);
    }
}

void TopBar::on_selected_project_changed(size_t index)
{
    for (size_t button_index = 0; button_index < m_list_view->list_item_count(); ++button_index) {
        ProjectButton* button = m_list_view->item_at(button_index);
        if (button->project_id() == index) {
            m_list_view->scroll_at_item(m_list_view->get_item(button_index));
            button->set_separator_visible(false);
            if (button_index > 0) {
                m_list_view->item_at(button_index - 1)->set_separator_visible(false);
            }
        } else {
            button->set_separator_visible(true);
        }
    }
}

static void append_snapshot(
    Yoga::Menu& menu,
    std::size_t snapshot_id,
    const std::string& name,
    bool selected,
    ProjectInteractor& project_interactor
)
{
    const Render::Icon icon{selected ? Render::Icon::ArrowRight : Render::Icon::None};
    Yoga::MenuItem* menu_item{menu.append_item(name, icon, "", false)};
    menu_item->set_enabled(!selected);
    menu_item->callbacks().action = [snapshot_id, &project_interactor]()
    { project_interactor.undo_provider().select_snapshot(snapshot_id); };
}

void TopBar::on_undo_store_changed(
    Domain::SelectionId project_id,
    const std::vector<std::pair<std::size_t, std::string>>& snapshots,
    std::size_t selected_index
)
{
    if (m_project_interactor.selected_project_id() != project_id) {
        return;
    }

    const bool undo_enabled{m_render_module->command(CommandName::Undo).enabled()};
    const bool redo_enabled{m_render_module->command(CommandName::Redo).enabled()};
    m_undo_btn->set_enabled(undo_enabled);
    m_undo_stack_btn->set_enabled(undo_enabled);
    m_redo_btn->set_enabled(redo_enabled);
    m_redo_stack_btn->set_enabled(redo_enabled);

    m_undo_menu->clear();
    m_redo_menu->clear();

    for (std::size_t index{selected_index + 1}; index-- > 0;) {
        const auto& [id, name]{snapshots.at(index)};
        append_snapshot(*m_undo_menu, id, name, index == selected_index, m_project_interactor);
    }

    for (std::size_t index{selected_index}; index < snapshots.size(); ++index) {
        const auto& [id, name]{snapshots.at(index)};
        append_snapshot(*m_redo_menu, id, name, index == selected_index, m_project_interactor);
    }
}

void TopBar::on_project_loaded(Domain::SelectionId project_id)
{
    update_recent_projects();
}

void TopBar::on_project_saved(Domain::SelectionId project_id)
{
    update_recent_projects();
}

static void unbind_menu(CommandBindingManager& binding_mgr, const Menu* menu)
{
    for (size_t i = 0, n = menu->menu_item_count(); i < n; ++i) {
        auto it = menu->item_at(i);
        binding_mgr.unbind_ui_item(it);
        auto* sub = it->submenu();
        if (sub) {
            unbind_menu(binding_mgr, sub);
        }
    }
}

void TopBar::on_menu_updated()
{
    auto& binding_mgr = m_render_module->command_binding_manager();
    unbind_menu(binding_mgr, m_main_menu);
    unbind_menu(binding_mgr, m_file_menu);
    m_main_menu->clear();
    m_file_menu->clear();
    add_menu_btns_items();
}

void TopBar::on_plugins_scanned(const Lua::PluginRegistry& registry)
{
    m_menu_command_registrar.update_main_menu_plugin_commands(*m_plugin_system);
}

void TopBar::focus_search()
{
    m_search_bar->focus_search();
}

void TopBar::add_save_project_btn(Item* parent)
{
    m_save_btn = parent->emplace_back<LayoutButton>("", Render::Icon::TobBarSave, _u8L("Save"));
    m_render_module->command_binding_manager().bind_tb_item(CommandName::SaveProject, m_save_btn);
}

static void toggle_menu_visibility(Menu* menu)
{
    if (!menu)
        return;
    menu->opened() ? menu->close() : menu->open();
}

void TopBar::add_undo_btn(Item* parent)
{
    auto item{parent->emplace_back<Item>()};
    m_undo_btn =
        item->emplace_back<LayoutButton>(std::string{}, Render::Icon::TopBarUndo, _u8L("Undo"));
    m_undo_stack_btn = item->emplace_back<LayoutButton>(
        std::string{},
        Render::Icon::CaretDown,
        _u8L("Undo stack")
    );
    m_undo_stack_btn->set_enabled(false);

    m_undo_menu = m_undo_stack_btn->emplace_back<Yoga::Menu>("undo_menu", Yoga::Position::Bottom);
    m_undo_menu->set_max_height(550);

    m_undo_stack_btn->callbacks().action = [this]() { toggle_menu_visibility(m_undo_menu); };

    m_render_module->command_binding_manager().bind_tb_item(CommandName::Undo, m_undo_btn);
}

void TopBar::add_redo_btn(Item* parent)
{
    auto item{parent->emplace_back<Item>()};
    m_redo_btn =
        item->emplace_back<LayoutButton>(std::string{}, Render::Icon::TopBarRedo, _u8L("Redo"));
    m_redo_stack_btn = item->emplace_back<LayoutButton>(
        std::string{},
        Render::Icon::CaretDown,
        _u8L("Redo stack")
    );
    m_redo_stack_btn->set_enabled(false);

    m_redo_menu = m_redo_stack_btn->emplace_back<Yoga::Menu>("redo_menu", Yoga::Position::Bottom);
    m_redo_menu->set_max_height(550);

    m_redo_stack_btn->callbacks().action = [this]() { toggle_menu_visibility(m_redo_menu); };

    m_render_module->command_binding_manager().bind_tb_item(CommandName::Redo, m_redo_btn);
}

void TopBar::add_show_ui_btn(Item* parent)
{
    m_show_ui_btn =
        parent->emplace_back<LayoutButton>("", Render::Icon::TobBarShowUI, _u8L("Hide sidebars"));
    m_show_ui_btn->set_checkable(true);

    m_show_ui_btn->callbacks().action = [this]()
    {
        m_show_ui_btn->set_tooltip(
            m_show_ui_btn->checked() ? _u8L("Show sidebars") : _u8L("Hide sidebars")
        );
        // Propagate sidebars visibility into active RenderModule
        m_render_module->set_sidebars_visible(!m_show_ui_btn->checked());

        // ysTODO: save hide value into app_config
    };
}

void TopBar::add_menu_btns(Item* parent)
{
    if (App::MenuItem* main_menu_item =
            m_render_module->menu_manager().menu_item(MenuItemName::MainMenu))
    {
        m_main_menu_btn = parent->emplace_back<LayoutButton>(
            MenuBuilder::item_name_translated(main_menu_item->name()),
            MenuBuilder::item_icon(main_menu_item->name())
        );
        m_main_menu_btn->icon_object()->set_preserve_colors(true);
        m_main_menu_btn->set_background_color(Platform::Color::ButtonTransparent);
        m_main_menu_btn->callbacks().action = [this]() { toggle_menu_visibility(m_main_menu); };
        m_main_menu_btn->callbacks().hovered_changed = [this](bool hovered)
        {
            if (hovered && m_file_menu->opened())
                m_main_menu->open();
        };

        m_main_menu =
            m_main_menu_btn->emplace_back<Yoga::Menu>("main_menu", Yoga::Position::Bottom);
        m_main_menu->set_offset(0.f);
    }

    if (App::MenuItem* file_menu_item =
            m_render_module->menu_manager().menu_item(MenuItemName::FileMenu))
    {
        m_file_menu_btn = parent->emplace_back<LayoutButton>(
            MenuBuilder::item_name_translated(file_menu_item->name()),
            MenuBuilder::item_icon(file_menu_item->name())
        );
        m_file_menu_btn->set_background_color(Platform::Color::ButtonTransparent);
        m_file_menu_btn->callbacks().action = [this]() { toggle_menu_visibility(m_file_menu); };
        m_file_menu_btn->callbacks().hovered_changed = [this](bool hovered)
        {
            if (hovered && m_main_menu->opened())
                m_file_menu->open();
        };

        m_file_menu =
            m_file_menu_btn->emplace_back<Yoga::Menu>("file_menu", Yoga::Position::Bottom);
        m_file_menu->set_offset(0.f);
    }

    add_menu_btns_items();
}

void TopBar::add_menu_btns_items()
{
    MenuBuilder menu_builder(
        m_render_module->menu_manager(),
        m_render_module->command_binding_manager()
    );

    if (App::MenuItem* main_menu_item =
            m_render_module->menu_manager().menu_item(MenuItemName::MainMenu))
    {
        menu_builder.add_menu_items(m_main_menu, main_menu_item);
    }

    if (App::MenuItem* file_menu_item =
            m_render_module->menu_manager().menu_item(MenuItemName::FileMenu))
    {
        menu_builder.add_menu_items(m_file_menu, file_menu_item);

        const std::vector<App::MenuItem*> children                     = file_menu_item->children();
        std::vector<App::MenuItem*>::const_iterator recent_projects_it = std::find_if(
            children.cbegin(),
            children.cend(),
            [](App::MenuItem* menu_item)
            { return menu_item->name().matches(MenuItemName::RecentProjects); }
        );
        ASSERT(recent_projects_it != children.cend());
        m_recent_projects_item =
            m_file_menu->item_at(std::distance(children.cbegin(), recent_projects_it));

        update_recent_projects();
    }
}

void TopBar::update_recent_projects()
{
    if (!m_recent_projects_item) {
        return;
    }

    m_recent_projects_item->clear_submenu();

    const AppSettingsAdvanced::RecentProjects& recent_projects =
        AppServices::instance().app_config().app_settings_advanced().recent_projects;

    for (const std::string& recent_project : recent_projects) {
        Yoga::MenuItem* recent_project_item =
            m_recent_projects_item->append_sub_menu_item(recent_project);
        recent_project_item->callbacks().action = [this, recent_project]
        { m_project_interactor.load_project(recent_project); };
    }

    m_recent_projects_item->set_enabled(!recent_projects.empty());
}

void TopBar::register_context_menus(
    Scene::GeometryDataFactory& data_factory,
    Scene::ISceneProvider* scene_provider
)
{
    m_menu_command_registrar.register_context_menus(data_factory, scene_provider);
}

} // namespace Slic3r::App
