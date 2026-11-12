#include "Slic3r/App/MenuManager.hpp"
#include "Slic3r/App/MenuItem.hpp"
#include "Slic3r/App/UIItemCommand.hpp"

#include "Slic3r/App/Platform/CommandRegistry.hpp"

#include "Slic3r/Assert.hpp"

namespace Slic3r::App {

MenuManager& MenuManager::register_menu_item(
    std::vector<UniversalMenuItemName> path,
    std::unique_ptr<UIItemCommand> command
)
{
    UIItemCommand* cmd = command.get();
    m_command_registry.register_command(std::move(command));

    create_and_distribute_new_menu_item(path, cmd);
    return *this;
}

MenuManager& MenuManager::register_command(std::unique_ptr<Platform::ICommand> command)
{
    m_command_registry.register_command(std::move(command));
    return *this;
}

MenuManager& MenuManager::register_menu_item_from_command(
    std::vector<UniversalMenuItemName> path,
    const Platform::ICommand& command
)
{
    const UIItemCommand* cmd = dynamic_cast<const UIItemCommand*>(&command);
    ASSERT(cmd);

    create_and_distribute_new_menu_item(path, cmd);
    return *this;
}

// NOTE:
// MenuItemName::Separator is intentionally implemented as a shared singleton MenuItem.
// Separators are treated as a purely visual UI element and do not represent a logical
// node in the menu hierarchy.
//
// The same separator instance may be appended to multiple parent menus.
// This is safe because:
// - separators never have children
// - separators do not store state
// - separators are rendered immediately and not mutated after insertion
//
// IMPORTANT:
// Do NOT add state, children, or ownership semantics to separators.
// If separators ever need to become stateful or hierarchical, this design
// must be revisited and separators must become distinct instances.
MenuManager& MenuManager::register_menu_separator_item(std::vector<UniversalMenuItemName> path)
{
    ASSERT(!path.empty());

    MenuItemName child_name = MenuItemName::Separator;
    if (!m_menus_by_id.contains(child_name)) {
        m_menus_by_id[child_name] = std::make_unique<MenuItem>(child_name);
    }

    distribute_into_menu_hierarchy(child_name, path);

    return *this;
}

void MenuManager::create_and_distribute_new_menu_item(
    std::vector<UniversalMenuItemName> path,
    const UIItemCommand* cmd
)
{
    UniversalMenuItemName child_name = path.back();
    ASSERT(
        !m_menus_by_id.contains(child_name)
        && !child_name.matches(MenuItemName::Separator)
    );

    m_menus_by_id[child_name] = std::make_unique<MenuItem>(child_name, cmd);
    path.pop_back();

    if (path.empty())
        return;

    distribute_into_menu_hierarchy(child_name, path);
}

void MenuManager::distribute_into_menu_hierarchy(
    UniversalMenuItemName child_name,
    std::vector<UniversalMenuItemName> path
)
{
    // Create parent if it doesn't exist yet

    UniversalMenuItemName parent_name = path.back();
    if (!m_menus_by_id.contains(parent_name)) {
        m_menus_by_id[parent_name] = std::make_unique<MenuItem>(parent_name);
    }

    // Append child for the parent
    // Note: All parents doesn't have command but have all other needed info

    m_menus_by_id[parent_name]->append(m_menus_by_id[child_name].get());
    path.pop_back();

    // Distribute it to the upper parents if needed

    while (!path.empty()) {
        child_name  = parent_name;
        parent_name = path.back();
        if (!m_menus_by_id.contains(parent_name)) {
            m_menus_by_id[parent_name] = std::make_unique<MenuItem>(parent_name);
        }
        const std::vector<MenuItem*>& children = m_menus_by_id[parent_name]->children();
        if (std::find(children.begin(), children.end(), m_menus_by_id[child_name].get())
            == children.end())
        {
            m_menus_by_id[parent_name]->append(m_menus_by_id[child_name].get());
        }

        path.pop_back();
    }
}

MenuItem* MenuManager::menu_item(const UniversalMenuItemName& name)
{
    if (m_menus_by_id.count(name) == 0) {
        return nullptr;
    }
    ASSERT(m_menus_by_id.contains(name), "Non-existed command");
    return m_menus_by_id[name].get();
}

void MenuManager::clear_submenu_of(const UniversalMenuItemName& name)
{
    const auto& menu = m_menus_by_id.at(name);
    for (auto& ch : menu->children()) {
        if (!ch->children().empty()) {
            clear_submenu_of(ch->name());
        }

        auto cmd = ch->command();
        if (cmd != nullptr) {
            m_command_registry.remove_command(cmd->name());
        }
        if (ch->name() != MenuItemName::Separator) {
            m_menus_by_id.erase(ch->name());
        }
    }
    menu->clear_children();
}

void MenuManager::rebuild_submenu(
    const UniversalMenuItemName& name,
    const std::function<void()>& modifier
)
{
    clear_submenu_of(name);
    modifier();
    invoke_listeners<IMenuUpdatedListener>([](auto* l) { l->on_menu_updated(); });
}

} // namespace Slic3r::App
