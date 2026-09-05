#include "Slic3r/Domain/ConfigItemPredicate.hpp"

#include "Slic3r/Domain/ConfigValue.hpp"

#include <algorithm>
#include <utility>

namespace Slic3r::Domain {

bool ConfigItemLookup::flag(const std::string& key, bool fallback) const
{
    const ConfigValue* value = find_value(key);
    if (value == nullptr || !value->holds_alternative<bool>())
        return fallback;
    return value->get<bool>();
}

double ConfigItemLookup::number(const std::string& key, double fallback) const
{
    const ConfigValue* value = find_value(key);
    if (value == nullptr)
        return fallback;

    // A numeric setting may be stored as any of these depending on how it is
    // declared, and a predicate should not have to know which.
    if (value->holds_alternative<double>())
        return value->get<double>();
    if (value->holds_alternative<int>())
        return static_cast<double>(value->get<int>());
    if (value->holds_alternative<Percentage>())
        return value->get<Percentage>().value;
    if (value->holds_alternative<std::optional<int>>()) {
        const std::optional<int> opt = value->get<std::optional<int>>();
        return opt.has_value() ? static_cast<double>(*opt) : fallback;
    }
    return fallback;
}

int ConfigItemLookup::enum_value(const std::string& key, int fallback) const
{
    const ConfigValue* value = find_value(key);
    if (value == nullptr)
        return fallback;
    if (value->holds_alternative<EnumWrapper>())
        return value->get<EnumWrapper>().value();
    if (value->holds_alternative<int>())
        return value->get<int>();
    return fallback;
}

ConfigItemPredicate when_enabled(std::string key)
{
    return [key = std::move(key)](const ConfigItemLookup& lookup)
    { return lookup.flag(key); };
}

ConfigItemPredicate when_disabled(std::string key)
{
    return [key = std::move(key)](const ConfigItemLookup& lookup)
    { return !lookup.flag(key); };
}

ConfigItemPredicate when_any_enabled(std::vector<std::string> keys)
{
    return [keys = std::move(keys)](const ConfigItemLookup& lookup)
    {
        return std::any_of(
            keys.begin(), keys.end(),
            [&lookup](const std::string& key) { return lookup.flag(key); }
        );
    };
}

ConfigItemPredicate when_all_enabled(std::vector<std::string> keys)
{
    return [keys = std::move(keys)](const ConfigItemLookup& lookup)
    {
        return std::all_of(
            keys.begin(), keys.end(),
            [&lookup](const std::string& key) { return lookup.flag(key); }
        );
    };
}

ConfigItemPredicate when_positive(std::string key)
{
    return when_greater_than(std::move(key), 0.0);
}

ConfigItemPredicate when_greater_than(std::string key, double threshold)
{
    return [key = std::move(key), threshold](const ConfigItemLookup& lookup)
    { return lookup.number(key) > threshold; };
}

ConfigItemPredicate when_enum_is(std::string key, int value)
{
    return [key = std::move(key), value](const ConfigItemLookup& lookup)
    { return lookup.enum_value(key) == value; };
}

ConfigItemPredicate when_enum_in(std::string key, std::vector<int> values)
{
    return [key = std::move(key), values = std::move(values)](const ConfigItemLookup& lookup)
    {
        const int current = lookup.enum_value(key);
        return std::find(values.begin(), values.end(), current) != values.end();
    };
}

ConfigItemPredicate all_of(std::vector<ConfigItemPredicate> predicates)
{
    return [predicates = std::move(predicates)](const ConfigItemLookup& lookup)
    {
        return std::all_of(
            predicates.begin(), predicates.end(),
            [&lookup](const ConfigItemPredicate& p) { return evaluate(p, lookup); }
        );
    };
}

ConfigItemPredicate any_of(std::vector<ConfigItemPredicate> predicates)
{
    return [predicates = std::move(predicates)](const ConfigItemLookup& lookup)
    {
        return std::any_of(
            predicates.begin(), predicates.end(),
            [&lookup](const ConfigItemPredicate& p) { return evaluate(p, lookup); }
        );
    };
}

ConfigItemPredicate negate(ConfigItemPredicate predicate)
{
    if (!predicate)
        return {};
    return [predicate = std::move(predicate)](const ConfigItemLookup& lookup)
    { return !predicate(lookup); };
}

bool evaluate(const ConfigItemPredicate& predicate, const ConfigItemLookup& lookup)
{
    return predicate ? predicate(lookup) : true;
}

ConfigItemRequirement requires_that(ConfigItemPredicate predicate, std::string reason)
{
    return ConfigItemRequirement{std::move(predicate), std::move(reason)};
}

const ConfigItemRequirement* first_unmet(
    const std::vector<ConfigItemRequirement>& requirements,
    const ConfigItemLookup& lookup
)
{
    for (const ConfigItemRequirement& requirement : requirements) {
        if (!evaluate(requirement.predicate, lookup))
            return &requirement;
    }
    return nullptr;
}

} // namespace Slic3r::Domain
