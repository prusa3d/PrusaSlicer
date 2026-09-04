#include "Slic3r/Biz/PrintToolItem.hpp"

#include "Slic3r/Domain/Config.hpp"

namespace Slic3r::Biz {

const Domain::ConfigValue& PrintToolItem::tool_value(size_t index) const
{
    ASSERT(index < tool_overrides.size());
    return tool_overrides.at(index)->value();
}

static double get_average(const std::vector<double>& values)
{
    double sum{};
    for (const double& value : values) {
        sum += value;
    }
    return sum / (double) values.size();
}

static double get_min(const std::vector<double>& values)
{
    double min{std::numeric_limits<double>::max()};
    for (const double& value : values) {
        if (value < min) {
            min = value;
        }
    }
    return min;
}

static double get_max(const std::vector<double>& values)
{
    double max{std::numeric_limits<double>::lowest()};
    for (const double& value : values) {
        if (value > max) {
            max = value;
        }
    }
    return max;
}

void PrintToolItem::update_value()
{
    if (*print_item->def().type == typeid(Domain::FloatOrPercentage)
        && print_item->def().compatibility_rule != Domain::CompatibilityRule::Undefined)
    {
        // TEMPORARY HACK: apply_compatibility_rule must not be called for
        // float or percentage unless all the tool overrides are resolved
        // to float values! The backend handles this correctly, but
        // we have nothing to show to the user.
        //
        // The correct fix is to resolve the tool override values before
        // applying compatibility rules to them.
        //
        // The ultimate fix is to get rid of compatibility rules for such
        // values.

        std::vector<Domain::ConfigValue> values;
        for (std::size_t i{}; i < tool_overrides.size(); ++i) {
            const Domain::ConfigItem* item{tool_overrides[i]};
            if (!item) {
                continue;
            }
            if (shared_context.extruder_candidates.empty()
                || shared_context.extruder_candidates.contains(i))
            {
                values.push_back(item->value());
            }
        }
        if (values.empty()) {
            value = {print_item->value(), false};
            return;
        }

        const bool all_same{std::ranges::all_of(
            values,
            [&](const Domain::ConfigValue& value) { return value == values.front(); })};

        if (all_same) {
            value = {values.front(), false};
            return;
        }

        // lukasmatena thinks that the following block is wrong.
        // If the values are percentages, we cannot resolve them here, as the comment
        // above states. The get_min/max/average static functions above return misleading
        // values here, because each tool can have different base value the percentage relates to.
        // Commenting this out means that UI shows nan due to the last-resort fallback.
        //
        // const bool all_percentage{std::ranges::all_of(
        //     values,
        //     [](const Domain::ConfigValue& value)
        //     { return value.get<Domain::FloatOrPercentage>().is_percentage(); })};

        // if (all_percentage) {
        //     std::vector<double> percentage_values;
        //     for (const Domain::ConfigValue& value : values) {
        //         percentage_values.push_back(value.get<Domain::FloatOrPercentage>().get_abs_value(100));
        //     }
        //     double resulting_value{};
        //     if (print_item->def().compatibility_rule == Domain::CompatibilityRule::Average) {
        //         resulting_value = get_average(percentage_values);
        //     } else if (print_item->def().compatibility_rule == Domain::CompatibilityRule::Min) {
        //         resulting_value = get_min(percentage_values);
        //     } else if (print_item->def().compatibility_rule == Domain::CompatibilityRule::Max) {
        //         resulting_value = get_max(percentage_values);
        //     } else {
        //         PANIC("Invalid compatibility rule");
        //     }
        //     value = {Domain::ConfigValue{Domain::FloatOrPercentage{Domain::Percentage{resulting_value}}}, true};
        //     return;
        // }

        const bool all_floats{std::ranges::all_of(
            values,
            [](const Domain::ConfigValue& value)
            { return !value.get<Domain::FloatOrPercentage>().is_percentage(); })};

        if (all_floats) {
            std::vector<double> float_values;
            for (const Domain::ConfigValue& value : values) {
                float_values.push_back(value.get<Domain::FloatOrPercentage>().float_value());
            }
            double resulting_value{};
            if (print_item->def().compatibility_rule == Domain::CompatibilityRule::Average) {
                resulting_value = get_average(float_values);
            } else if (print_item->def().compatibility_rule == Domain::CompatibilityRule::Min) {
                resulting_value = get_min(float_values);
            } else if (print_item->def().compatibility_rule == Domain::CompatibilityRule::Max) {
                resulting_value = get_max(float_values);
            } else {
                PANIC("Invalid compatibility rule");
            }
            value = {Domain::ConfigValue{Domain::FloatOrPercentage{resulting_value}}, true};
            return;
        }

        // This std::nan, is a massive shortcut for now, just to show something.
        value = {Domain::ConfigValue{Domain::FloatOrPercentage{std::nan("")}}, true};
        return;
    }

    value = Domain::apply_compatibility_rule(
        &print_item->value(),
        tool_overrides,
        shared_context.extruder_candidates
    );
}

bool PrintToolItem::is_dirty() const
{
    if (print_item->def().category == Domain::ConfigItemDef::Category::Hidden) {
        return false;
    }

    if (shared_context.has_multiple_extruders && !tool_overrides.empty()) {
        return is_dirty_tool();
    }
    return is_dirty_print();
}

bool PrintToolItem::is_dirty_print() const
{
    return !original_print_item || original_print_item->value() != print_item->value();
}

bool PrintToolItem::is_dirty_tool(std::optional<size_t> index) const
{
    if (index.has_value()) {
        return index.value() < original_tool_overrides.size()
            && original_tool_overrides.at(index.value())->value() != tool_value(index.value());
    }
    for (size_t tool_id{}; tool_id < tool_overrides.size(); tool_id++) {
        if (is_dirty_tool(tool_id)) {
            return true;
        }
    }
    return false;
}

} // namespace Slic3r::Biz
