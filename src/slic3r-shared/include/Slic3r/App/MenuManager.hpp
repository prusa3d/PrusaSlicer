#pragma once

#include "Slic3r/App/MenuItemName.hpp"
#include "Slic3r/App/MenuItem.hpp"
#include "Slic3r/Biz/Platform/WithListeners.hpp"
#include "Slic3r/App/IMenuUpdatedListener.hpp"

#include <unordered_map>
#include <memory>

namespace Slic3r::App {

namespace Platform {
class CommandRegistry;
} // namespace Platform

class UIItemCommand;
class CommandBindingManager;

class MenuManager : public WithListeners<IMenuUpdatedListener>
{
public:
    using MenusMap = std::unordered_map<UniversalMenuItemName, std::unique_ptr<MenuItem>>;

    explicit MenuManager(Platform::CommandRegistry& command_registry) :
        m_command_registry(command_registry)
    {}

    /**
     * @brief Register single menu item with related command
     * @param path Path to menu item as a stack of UniversalMenuItemNames, where first is a root item name
     * @param command Pointer to command implementation
     */
    MenuManager&
    register_menu_item(std::vector<UniversalMenuItemName> path, std::unique_ptr<UIItemCommand> command);

    MenuManager& register_command(std::unique_ptr<Platform::ICommand> command);

    /**
     * @brief Register single menu item with related command
     * @param path Path to menu item as a stack of UniversalMenuItemNames, where first is a root item name
     * @param command Reference to command implementation
     */
    MenuManager& register_menu_item_from_command(
        std::vector<UniversalMenuItemName> path,
        const Platform::ICommand& command
    );

    /**
     * @brief Registers a single menu item linked to an existing command.
     * @param path Path to the menu item (e.g., MainMenu -> View -> ZoomIn).
     */
    MenuManager& register_menu_separator_item(std::vector<UniversalMenuItemName> path);

    MenuItem* menu_item(const UniversalMenuItemName& name);

    bool empty() const
    {
        return m_menus_by_id.empty();
    }

    void rebuild_submenu(const UniversalMenuItemName& name, const std::function<void()>& modifier);

private:
    void clear_submenu_of(const UniversalMenuItemName& name);

    void
    create_and_distribute_new_menu_item(std::vector<UniversalMenuItemName> path, const UIItemCommand* cmd);
    void distribute_into_menu_hierarchy(UniversalMenuItemName child_name, std::vector<UniversalMenuItemName> path);

private:
    Platform::CommandRegistry& m_command_registry;
    MenusMap m_menus_by_id;
};

} // namespace Slic3r::App
