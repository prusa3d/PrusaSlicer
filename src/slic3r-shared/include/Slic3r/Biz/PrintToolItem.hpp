#pragma once

#include <Slic3r/Domain/ConfigValue.hpp>

#include <string>
#include <set>

namespace Slic3r::Domain {
struct ConfigBox;
class ConfigItem;
} // namespace Slic3r::Domain

namespace Slic3r::Biz {
/**
 * @brief The ToolPrintItem class is a wrapper for Domain::ConfigItem for Items
 * defined in Print and tool. Essentially these two sources are aggregated
 * into a single ToolPrintItem.
 */
struct PrintToolItem
{
    struct SharedContext
    {
        bool has_multiple_extruders = false;
        const std::set<unsigned>&
            extruder_candidates; ///< extruder candidates vector stored probably in PrintToolCBOL
    };

    std::string name;
    bool mixed = false; ///< true if all sources values are same, false otherwise
    const Domain::ConfigItem* print_item{nullptr}; ///< pointer to this item in the Print ConfigBox
    std::vector<const Domain::ConfigItem*> tool_overrides; ///< vector of turned overrides from Tool
    const Domain::ConfigItem* original_print_item{nullptr}; ///< pointer to this item in the Print ConfigBox in original preset
    std::vector<const Domain::ConfigItem*> original_tool_overrides; ///< vector of turned overrides from Tool in original preset
    std::pair<Domain::ConfigValue, bool> value;
    const SharedContext& shared_context;
    bool is_favorite{false};

    const Domain::ConfigValue& tool_value(size_t index) const;

    void update_value();

    bool is_dirty() const;
    bool is_dirty_print() const;
    // Check if any tool is dirty when index is not specified, or if a specific tool is dirty when index is specified
    bool is_dirty_tool(std::optional<size_t> index = std::nullopt) const;
};

} // namespace Slic3r::Biz
