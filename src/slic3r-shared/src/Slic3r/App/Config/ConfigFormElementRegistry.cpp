#include "Slic3r/App/Config/ConfigFormElementRegistry.hpp"

#include "Slic3r/App/Config/TravelAvoidanceElement.hpp"

#include <algorithm>
#include <utility>

namespace Slic3r::App {

ConfigFormElementRegistry& ConfigFormElementRegistry::instance()
{
    static ConfigFormElementRegistry registry;
    // Registered on first access rather than from a start-up hook, so there is
    // no ordering to get wrong and the CLI never pays for GUI-only state. The
    // built-ins are listed here so the set is readable in one place instead of
    // being left to static initialisers and link order.
    static const bool builtins_registered = []
    {
        register_travel_avoidance_element(registry);
        return true;
    }();
    (void) builtins_registered;
    return registry;
}

void ConfigFormElementRegistry::register_element(Entry entry)
{
    m_entries.push_back(std::move(entry));
}

std::vector<const ConfigFormElementRegistry::Entry*> ConfigFormElementRegistry::elements_for(
    const Domain::ConfigItemDef::Category category,
    const Domain::ConfigItemDef::OptionGroup option_group
) const
{
    std::vector<const Entry*> found;
    for (const Entry& entry : m_entries) {
        if (entry.category == category && entry.option_group == option_group)
            found.push_back(&entry);
    }
    return found;
}

bool ConfigFormElementRegistry::is_claimed(
    const Domain::ConfigItemDef::Category category,
    const Domain::ConfigItemDef::OptionGroup option_group,
    const std::string& key
) const
{
    return std::any_of(
        m_entries.begin(),
        m_entries.end(),
        [&](const Entry& entry)
        {
            return entry.category == category && entry.option_group == option_group
                && entry.claimed_keys.contains(key);
        }
    );
}

} // namespace Slic3r::App
