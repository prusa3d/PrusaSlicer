#pragma once

namespace Slic3r::Domain {
class ConfigItem;
} // namespace Slic3r::Domain

namespace Slic3r::App {

class IConfigNavigable
{
public:

    virtual ~IConfigNavigable() = default;

    virtual void navigate_to_item(const Domain::ConfigItem* config_item) = 0;
    virtual void clear_navigation()                                      = 0;
};

} // namespace Slic3r::App
