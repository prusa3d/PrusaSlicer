#pragma once

#include <string>
#include <optional>
#include "Slic3r/Domain/Config.hpp"

namespace Slic3r::Biz {
/**
 * @brief The OverrideItem class is a wrapper for Domain::ConfigItem from multiple sources
 */
struct OverrideItem
{
    std::string name;
    bool mixed = false; ///< true if all sources values are same, false otherwise
    std::optional<bool> overriden; ///< true if overide is on, false if override is off, nullopt if item is not override
    const Domain::ConfigItem* config_item = nullptr; ///< pointer to this item in the first source

    std::optional<bool> original_overridden        = std::nullopt;
    const Domain::ConfigItem* original_config_item = nullptr;

    inline bool is_override() const
    {
        return overriden.has_value();
    }

    inline bool is_dirty() const
    {
        if (!is_override() && config_item->def().category == Domain::ConfigItemDef::Category::Hidden)
            return false;
        return (!config_item
            || !original_config_item
            || config_item->value() != original_config_item->value())
            || overriden != original_overridden;
    }
};

} // namespace Slic3r::Biz
