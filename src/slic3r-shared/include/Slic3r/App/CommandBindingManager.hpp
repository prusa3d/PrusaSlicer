#pragma once

#include "Slic3r/App/Platform/CommandRegistry.hpp"

#include "Slic3r/Biz/ProjectScoped.hpp"
#include "Slic3r/Biz/ISelectedProjectChangedListener.hpp"
#include "Slic3r/Biz/Scene/SceneInteractor.hpp"

namespace Slic3r::App {

namespace Yoga {
class AbstractButton;
class MenuItem;
} // namespace Yoga

namespace Scene {
class GizmoCommandRegistry;
} // namespace Scene

class UIItemCommand;

class CommandBindingManager :
    public Biz::UserAccount::IUserAccountListener,
    public Biz::IStatusCacheChangedListener,
    public Biz::ISelectedBedInstancesChangedListener,
    public Biz::RemovableDrive::IRemovableDriveStatusListener,
    public Biz::Scene::ISceneSelectionChangedListener
{
public:
    using UIItemsPerCommandMap =
        std::unordered_map<std::string, std::vector<Yoga::AbstractButton*>>;

    CommandBindingManager(Platform::CommandRegistry& main_command_registry) :
        m_main_command_registry(main_command_registry)
    {}

    void set_gizmos_command_registry(const Scene::GizmoCommandRegistry* gizmos_command_registry)
    {
        m_gizmos_command_registry = gizmos_command_registry;
    }

    void bind_menu_item(const UIItemCommand* command, Yoga::MenuItem* ui_item);

    void bind_tb_item(const char* command_name, Yoga::AbstractButton* ui_item);

    void unbind_ui_item(const Yoga::AbstractButton* ui_item);

    void update_ui_items();

    Platform::CommandRegistry& main_command_registry()
    {
        return m_main_command_registry;
    }

    const Platform::ICommand& command(const char* command_name) const;
    bool has_command(const char* command_name) const;

    void on_user_account_id_success(bool, const std::string&) override;
    void on_user_account_logged_out() override;

    void on_selected_bed_instances_changed(
        Domain::SelectionId project_id,
        const Biz::Scene::BedSelection& bed_selection
    ) override;
    void on_status_cache_status_code_changed(const Domain::SlicingId slicing_id) override;

    void on_removable_drive_status_changed(
        const boost::filesystem::path&,
        Biz::RemovableDrive::RemovableDriveStatus
    ) override;

    void on_scene_selection_changed(
        Domain::SelectionId project_id,
        const Biz::Scene::ObjectSelection&
    ) override;

private:
    void bind(const char* command_name, Yoga::AbstractButton* ui_item);

    using UIItemToCommandMap = std::unordered_map<const Yoga::AbstractButton*, std::string>;

    Platform::CommandRegistry& m_main_command_registry;
    const Scene::GizmoCommandRegistry* m_gizmos_command_registry{nullptr};

    UIItemsPerCommandMap m_ui_items;
    UIItemToCommandMap m_ui_item_to_command;
};

} // namespace Slic3r::App
