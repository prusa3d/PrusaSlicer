#pragma once

#include "Slic3r/App/Config/ConfigFormElement.hpp"
#include "Slic3r/Domain/ConfigDef.hpp"

#include <functional>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace Slic3r::App {

/**
 * @brief Custom controls that stand in for the default one-row-per-setting form.
 *
 * This is the seam between the config and what the user sees. Anything that
 * wants to present a group of settings as something other than a list of rows
 * registers here; everything unregistered keeps rendering exactly as it does
 * today, so adopting this is per-group and reversible.
 *
 * It is also where a plugin would eventually attach. Note the factory returns a
 * widget rather than taking a draw callback: this is an immediate-mode UI whose
 * render runs every frame alongside the 3D scene, so an element is built once
 * and updated on change, never redrawn by a caller-supplied function.
 */
class ConfigFormElementRegistry
{
public:
    using Factory = std::function<std::unique_ptr<ConfigFormElement>(const ConfigFormContext&)>;

    struct Entry
    {
        Domain::ConfigItemDef::Category category{Domain::ConfigItemDef::Category::Unknown};
        Domain::ConfigItemDef::OptionGroup option_group{
            Domain::ConfigItemDef::OptionGroup::Unknown
        };
        /// Settings this element renders. They get no default row of their own.
        std::set<std::string> claimed_keys;
        Factory factory;
    };

    /// The registry, with the built-in elements registered on first access.
    static ConfigFormElementRegistry& instance();

    /// Register an element. Later registrations render after earlier ones.
    void register_element(Entry entry);

    /// Elements standing in for part of this group, in registration order.
    std::vector<const Entry*> elements_for(
        Domain::ConfigItemDef::Category category,
        Domain::ConfigItemDef::OptionGroup option_group
    ) const;

    /// True when an element renders this setting, so no default row should.
    bool is_claimed(
        Domain::ConfigItemDef::Category category,
        Domain::ConfigItemDef::OptionGroup option_group,
        const std::string& key
    ) const;

private:
    ConfigFormElementRegistry() = default;

    std::vector<Entry> m_entries;
};

} // namespace Slic3r::App
