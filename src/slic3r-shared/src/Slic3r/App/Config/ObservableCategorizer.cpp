#include "Slic3r/App/Config/ObservableCategorizer.hpp"

namespace Slic3r::App {

ObservableCategorizer::ObservableCategorizer()
{
    set_filter_fn(
        [](const Biz::ConfigItemContext& data)
        { return data.config_item->def().category != Domain::ConfigItemDef::Category::Hidden; }
    );
    set_group_by_fn(
        [](const Biz::ConfigItemContext& data,
           std::unordered_set<Domain::ConfigItemDef::Category>& seen_keys) -> bool
        {
            DEBUG_ASSERT(
                data.config_item->def().category != Domain::ConfigItemDef::Category::Unknown,
                "ConfigItemDef cannot have unknown category, please fill it."
            );

            if (seen_keys.contains(data.config_item->def().category)) {
                return true;
            } else {
                seen_keys.insert(data.config_item->def().category);
                return false;
            }
        }
    );
    set_sort_fn(
        [](const Biz::ConfigItemContext& lhs, const Biz::ConfigItemContext& rhs)
        {
            return static_cast<uint8_t>(lhs.config_item->def().category)
                < static_cast<uint8_t>(rhs.config_item->def().category);
        }
    );
}

} // namespace Slic3r::App
