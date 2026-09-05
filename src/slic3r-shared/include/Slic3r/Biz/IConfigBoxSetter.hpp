#pragma once

#include <vector>

namespace Slic3r::Domain {
class ConfigItem;
class ConfigItemLookup;
struct ConfigValue;
} // namespace Slic3r::Domain

namespace Slic3r::Biz {

/**
 * @brief The IConfigBoxSetter class acts as an Interface for methods covered by PresetInteractor
 */
class IConfigBoxSetter
{
public:
    IConfigBoxSetter()          = default;
    virtual ~IConfigBoxSetter() = default;

    virtual const Domain::ConfigValue*
    get_override_original_value(const Domain::ConfigItem& item, size_t index = 0) const = 0;

    virtual void set_item_value(
        const Domain::ConfigItem& item,
        const Domain::ConfigValue& value,
        const std::vector<size_t>& index = {0}
    ) = 0;

    virtual void
    set_item_override(const Domain::ConfigItem& item, bool enable, size_t index = 0) = 0;

    virtual void set_from_original_value(const Domain::ConfigItem& item, size_t index = 0) {}

    /**
     * @brief Reads sibling values, so a def's enable_if/visible_if can be evaluated.
     *
     * @return nullptr where dependency evaluation is not wired up, in which case
     *         every setting stays editable, exactly as before.
     */
    virtual const Domain::ConfigItemLookup* item_lookup() const { return nullptr; }
};

} // namespace Slic3r::Biz
