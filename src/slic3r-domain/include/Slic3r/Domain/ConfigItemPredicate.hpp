#pragma once

#include <functional>
#include <string>
#include <vector>

namespace Slic3r::Domain {

struct ConfigValue;

/**
 * @brief Reads sibling setting values while a dependency predicate is evaluated.
 *
 * A predicate must not care which config box a value lives in, nor how the
 * value is stored, so it gets this narrow view instead of the config itself.
 */
class ConfigItemLookup
{
public:
    virtual ~ConfigItemLookup() = default;

    /// @return the sibling's current value, or nullptr when it is not reachable here.
    virtual const ConfigValue* find_value(const std::string& key) const = 0;

    /**
     * @brief Value of a boolean setting.
     *
     * Falls back when the setting is missing or is not a boolean, so a
     * predicate naming a key that does not exist degrades to a stable answer
     * rather than throwing out of the middle of a UI refresh.
     */
    bool flag(const std::string& key, bool fallback = false) const;

    /// Value of a numeric setting, accepting int, double and percentage storage.
    double number(const std::string& key, double fallback = 0.0) const;

    /// Underlying integer of an enum setting.
    int enum_value(const std::string& key, int fallback = -1) const;
};

/**
 * @brief Decides whether a setting currently applies, given the rest of the config.
 *
 * Attached to a ConfigItemDef so that the relationships between settings live as
 * data next to the settings themselves, rather than in a separate pass that has
 * to be kept in sync by hand.
 */
using ConfigItemPredicate = std::function<bool(const ConfigItemLookup&)>;

/// True while the named boolean setting is on.
ConfigItemPredicate when_enabled(std::string key);

/// True while the named boolean setting is off.
ConfigItemPredicate when_disabled(std::string key);

/// True while at least one of the named boolean settings is on.
ConfigItemPredicate when_any_enabled(std::vector<std::string> keys);

/// True while every one of the named boolean settings is on.
ConfigItemPredicate when_all_enabled(std::vector<std::string> keys);

/// True while the named numeric setting is greater than zero.
ConfigItemPredicate when_positive(std::string key);

/// True while the named numeric setting is strictly greater than @p threshold.
ConfigItemPredicate when_greater_than(std::string key, double threshold);

/// True while the named enum setting holds @p value.
ConfigItemPredicate when_enum_is(std::string key, int value);

/// True while the named enum setting holds any of @p values.
ConfigItemPredicate when_enum_in(std::string key, std::vector<int> values);

/// Combines predicates. Empty inputs are true, matching "no constraint".
ConfigItemPredicate all_of(std::vector<ConfigItemPredicate> predicates);
ConfigItemPredicate any_of(std::vector<ConfigItemPredicate> predicates);

/// Inverts a predicate. An empty predicate stays empty.
ConfigItemPredicate negate(ConfigItemPredicate predicate);

/**
 * @brief Evaluates a predicate that may be empty.
 *
 * An unset predicate means the setting has no dependency, so it applies. Every
 * caller wants that reading, hence it lives here rather than at each call site.
 */
bool evaluate(const ConfigItemPredicate& predicate, const ConfigItemLookup& lookup);

} // namespace Slic3r::Domain
