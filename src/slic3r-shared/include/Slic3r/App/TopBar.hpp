#pragma once

#include <Slic3r/Domain/SelectionId.hpp>

#include <Slic3r/Biz/Platform/ListenerScope.hpp>
#include "Slic3r/Biz/ProjectInteractor.hpp"
#include "Slic3r/Biz/ISelectedProjectChangedListener.hpp"

#include "Slic3r/App/Undo/Store.hpp"
#include "Slic3r/App/Yoga/Window.hpp"
#include "Slic3r/App/Yoga/ListView.hpp"
#include "Slic3r/App/Yoga/ScrollArea.hpp"
#include "Slic3r/App/MenuCommandRegistrar.hpp"
#include "Slic3r/App/IMenuUpdatedListener.hpp"
#include "Slic3r/App/Lua/IPluginRescanListener.hpp"

namespace Slic3r::App::Platform {
class AbstractRenderModule;
} // namespace Slic3r::App::Platform

namespace Slic3r::App::Lua {
class PluginSystem;
} // namespace Slic3r::App::Lua

namespace Slic3r::App::Yoga {
class LayoutButton;
class Rectangle;
class ScrollArea;
class Menu;
class MenuItem;
} // namespace Slic3r::App::Yoga

namespace Slic3r::App {

class SearchBar;
class Navigator;
class ProjectSaver;
class ProjectButton;

class TopBar final :
    public Yoga::Window,
    public Biz::ISelectedProjectChangedListener,
    public Undo::IStoreChangedListener,
    public Biz::IProjectsChangedListener,
    public IMenuUpdatedListener,
    public Lua::IPluginRescanListener
{
public:
    TopBar(
        Biz::ProjectInteractor* project_interactor,
        Platform::AbstractRenderModule* render_module,
        Navigator& navigator,
        App::Undo::Store* undo_store,
        ProjectSaver& project_saver,
        Lua::PluginSystem* plugin_system = nullptr
    );
    ~TopBar();

    void on_selected_project_changed(size_t index) override;

    void register_context_menus(
        Scene::GeometryDataFactory& data_factory,
        Scene::ISceneProvider* scene_provider
    );

    void on_undo_store_changed(
        Domain::SelectionId project_id,
        const std::vector<std::pair<std::size_t, std::string>>& snapshots,
        std::size_t selected_index
    ) override;

    void on_project_loaded(Domain::SelectionId project_id) override;
    void on_project_saved(Domain::SelectionId project_id) override;

    void on_menu_updated() override;
    void on_plugins_scanned(const Lua::PluginRegistry& registry) override;

    void focus_search();

private:
    void add_save_project_btn(Item* parent);
    void add_undo_btn(Item* parent);
    void add_redo_btn(Item* parent);
    void add_show_ui_btn(Item* parent);

    void add_menu_btns(Item* parent);
    void add_menu_btns_items();

    void update_recent_projects();

private:
    using ProjectButtonFactory = Yoga::
        ViewFactory<ProjectButton, Domain::SelectionId, Biz::ProjectInteractor&, ProjectSaver&>;
    using ProjectButtonListView = Yoga::
        ListView<ProjectButton, Domain::SelectionId, ProjectButtonFactory, Yoga::ScrollArea>;

    Biz::ListenerScope<Biz::ISelectedProjectChangedListener, Biz::ProjectInteractor, TopBar>
        m_selected_project_changed_listener_scope;

    Biz::ListenerScope<Biz::IProjectsChangedListener, Biz::ProjectInteractor, TopBar>
        m_projects_changed_listener_scope;

    std::optional<Biz::ListenerScope<IPluginRescanListener, Lua::PluginSystem, TopBar>>
        m_plugin_rescan_listener_scope;

#ifndef USE_NATIVE_MENU
    Biz::ListenerScope<IMenuUpdatedListener, MenuManager, TopBar> m_menu_updated_listener_scope;
#endif
    ProjectButtonListView* m_list_view{nullptr};

    Yoga::LayoutButton* m_main_menu_btn{nullptr};
    Yoga::LayoutButton* m_file_menu_btn{nullptr};
    Yoga::LayoutButton* m_save_btn{nullptr};
    Yoga::LayoutButton* m_undo_btn{nullptr};
    Yoga::LayoutButton* m_undo_stack_btn{nullptr};
    Yoga::LayoutButton* m_redo_btn{nullptr};
    Yoga::LayoutButton* m_redo_stack_btn{nullptr};
    Yoga::LayoutButton* m_show_ui_btn{nullptr};
    Item* m_undo_redo_wrapper{nullptr};

    Yoga::Menu* m_main_menu{nullptr};
    Yoga::Menu* m_file_menu{nullptr};
    Yoga::Menu* m_undo_menu{nullptr};
    Yoga::Menu* m_redo_menu{nullptr};

    Yoga::MenuItem* m_recent_projects_item{nullptr};

    SearchBar* m_search_bar{nullptr};

    Biz::ProjectInteractor& m_project_interactor;
    ProjectSaver& m_project_saver;
    Platform::AbstractRenderModule* m_render_module{nullptr};
    Navigator& m_navigator;

    MenuCommandRegistrar m_menu_command_registrar;
    MenuManager& m_menu_manager;

    Undo::Store* m_undo_store{nullptr};
    Lua::PluginSystem* m_plugin_system{nullptr};
};

} // namespace Slic3r::App
