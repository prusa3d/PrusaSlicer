#pragma once
#include <Slic3r/Domain/Config.hpp>

namespace Slic3r::Biz {

struct ConfigItemContext
{
    std::string name;
    const Domain::ConfigItem* config_item{nullptr};
    const Domain::ConfigItem* original_config_item{nullptr};

    inline bool is_dirty() const
    {
        if (config_item->def().category == Domain::ConfigItemDef::Category::Hidden)
            return false;
        return config_item && original_config_item ?
            config_item->value() != original_config_item->value() :
            false;
    }
};

} // namespace Slic3r::Biz
