#pragma once

#include <algorithm>
#include <boost/container_hash/hash.hpp>
#include <cfloat>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "Slic3r/Domain/ConfigValue.hpp"
#include "Slic3r/Domain/ConfigDef.hpp"
#include "Slic3r/Domain/Preset/HwConfig.hpp"

namespace Slic3r::Domain {
// A wrapper type for a single config item. Not polymorphic, not templated. The caller
// shall not be bothered by dynamic casts, pointer ownership and other technicalities.
class ConfigItem
{
public:
    ConfigItem(const ConfigItemDef& def, const ConfigLocation location);

    bool operator==(const ConfigItem&) const = default;

    const ConfigItemDef& def() const {
        return *m_def;
    }

    template <typename T>
    T get() const
    {
        return m_value.get<T>();
    }

    template <typename T>
    void set(const T& value)
    {
        m_value.set(value);
    }

    template <typename Visitor>
    auto visit(Visitor&& visitor) const {
        return m_value.visit(std::forward<Visitor>(visitor));
    }

    template <typename Visitor>
    auto visit(Visitor&& visitor) {
        return m_value.visit(std::forward<Visitor>(visitor));
    }

    const ConfigValue& value() const {
        return m_value;
    }

    template <typename T>
    bool holds_alternative() const {
        return m_value.holds_alternative<T>();
    }

    const std::string& name() const {
        return m_def->name;
    }

    ConfigLocation location() const {
        return m_current_location;
    }

    CompatibilityRule compatibility_rule() const;

private:
    ConfigValue m_value;
    ConfigLocation m_current_location;

    // Comparision operator, compares the defintion pointers.
    // It cannot be nullptr.
    const ConfigItemDef* m_def;
};

class ConfigItems
{
public:
    ConfigItems(const ConfigDefinitions& defs, const ConfigLocation& location);

    const ConfigItem& opt(const std::string_view key) const;
    ConfigItem& opt(const std::string_view key);

    ConfigItem* find(const std::string& key);
    const ConfigItem* find(const std::string& key) const;

    const std::vector<ConfigItem>& all_items() const;
    std::vector<ConfigItem>& all_items();

    virtual ~ConfigItems() = default;

    bool operator==(const ConfigItems&) const = default;

    std::vector<std::string> diff_keys(const ConfigItems& other) const;

private:
    std::vector<ConfigItem> m_items;
    ConfigLocation m_location;
};

class ConfigOverrides {
public:
    ConfigOverrides(const ConfigDefinitions& defs, const ConfigLocation location);

    template <typename T>
    void set(const std::string& key, const T& value) {
        const auto item_index{find_item_by_index(key)};
        m_items.at(item_index).set(value);
        m_used_overrides.insert({key, item_index});
    }

    void set(const std::string& key, const ConfigValue& value) {
        const auto item_index{find_item_by_index(key)};
        m_items.at(item_index).set(value);
        m_used_overrides.insert({key, item_index});
    }

    void disable(const std::string& key);

    void enable(const std::string& key);

    std::size_t size() const;

    bool empty() const;

    std::optional<ConfigItem> get(const std::string& key) const;

    ConfigItem* find(const std::string& key);
    const ConfigItem* find(const std::string& key) const;

    std::vector<std::reference_wrapper<const ConfigItem>> overridden_items() const;

    std::vector<ConfigItem>& all_items();

    const std::vector<ConfigItem>& all_items() const;

    bool operator==(const ConfigOverrides&) const = default;

    std::vector<std::string> diff_overriden_keys(const ConfigOverrides& other) const;

private:
    std::size_t find_item_by_index(const std::string& key) const;

    std::map<std::string, std::size_t> m_used_overrides;
    std::vector<ConfigItem> m_items;
};

struct FindResult {
    ConfigItem* item{nullptr};
    bool is_override{};
};

struct ConstFindResult {
    const ConfigItem* item{nullptr};
    bool is_override{};
};

struct ConfigBox
{
    ConfigItems items;
    ConfigOverrides overrides;
    ConfigLocation location;

    FindResult find(const std::string& key);

    ConstFindResult find(const std::string& key) const;

    bool operator==(const ConfigBox&) const = default;

    std::vector<std::string> diff_keys(const ConfigBox& other) const;

protected:
    ConfigBox(const ConfigDefinitions& defs, const ConfigLocation& location)
        : items{defs, location}, overrides{defs, location}, location{location}
    {}
};

using BoxRef = std::reference_wrapper<const ConfigBox>;
using BoxRefs = std::vector<BoxRef>;
using BoxOrBoxesVector = std::vector<std::variant<BoxRef, BoxRefs>>;

using MutBoxRef = std::reference_wrapper<ConfigBox>;
using MutBoxRefs = std::vector<MutBoxRef>;
using MutBoxOrBoxesVector = std::vector<std::variant<MutBoxRef, MutBoxRefs>>;

/* @param default_value: Used if any of the items is nullptr. Might be nullptr,
 *     but then all the item values need to be specified.
 *
 * @param items: Vector with the **same length** as used tools count.
 *     If a value for some tool is not specified, **there must be a nullptr**.
 *
 * @param extruder_candidates: A vector of extruders that are actually potentially used.
 *     Value for unused extruders are ignored. Extruder candidates can be empty,
 *     in that case all extruders are taken into account.
 *
 * @return the bool is true if the rule was applied, if all the item values are the same,
 *     there is no need to apply the rule
 */
std::pair<ConfigValue, bool> apply_compatibility_rule(
    const ConfigValue* default_value,
    const std::vector<const ConfigItem*>& items,
    const std::vector<unsigned>& extruder_candidates
);
std::pair<ConfigValue, bool> apply_compatibility_rule(
    const ConfigValue* default_value,
    const std::vector<const ConfigItem*>& items,
    const std::set<unsigned>& extruder_candidates
);

class SquashedConfig {
public:
    SquashedConfig(
        const BoxOrBoxesVector& boxes,
        const std::vector<unsigned>& extruder_candidates,
        const Preset::HwPrinterConfig& hw_config
    );

    const Preset::HwPrinterConfig& hw_config() const;

    std::vector<std::string> diff_keys(const SquashedConfig& other) const;

    bool operator==(const SquashedConfig& other) const;

    const std::map<std::string, ConfigValue>& values() const;

    /* This is an extremelly dangerous operation, possibly modifying the slicing input.
     * Use with caution, and not that it is up to you to ensure the type of the
     * value is correct. */
    template<typename T>
    void set(const std::string& key, const T& value) {
        // extremelly dangerous, anything can be set...
        m_values.insert_or_assign(key, ConfigValue{value});
    }

    std::size_t hash() const;

    const std::map<std::string, ConfigValue>& original_values() const;
    const std::vector<unsigned>& extruder_candidates() const;

protected:
    std::map<std::string, ConfigValue> m_values;
    std::map<std::string, ConfigValue> m_original_values;
    Preset::HwPrinterConfig m_hw_config;
    std::vector<unsigned> m_extruder_candidates;
};

class FullConfig : public SquashedConfig {
public:
    template<typename T>
    T get(const std::string& key) const {
        return get_value(key).get<T>();
    }

    const ConfigValue& get_value(const std::string& key) const;

    const std::vector<std::string>& keys() const;

    virtual ~FullConfig() = default;
protected:
    FullConfig(
        const BoxOrBoxesVector& input,
        const std::vector<unsigned>& extruder_candidates,
        const Preset::HwPrinterConfig& hw_config
    );

private:
    std::vector<std::string> m_keys;
};

class PartialConfig : public SquashedConfig{
public:
    template<typename T>
    std::optional<T> get(const std::string& key) const {
        if (const auto value{get_value(key)}) {
            return value->get<T>();
        }
        return std::nullopt;
    }

    virtual ~PartialConfig() = default;


protected:
    PartialConfig(
        const BoxOrBoxesVector& input,
        const Preset::HwPrinterConfig& hw_config
    );

private:
    friend class ConfigView;
    std::optional<ConfigValue> get_value(const std::string& key) const;
};


using FullConfigPtr = std::shared_ptr<const FullConfig>;
using PartialConfigPtr = std::shared_ptr<const PartialConfig>;

class ConfigView
{
public:
    ConfigView(FullConfigPtr full_config, const std::vector<PartialConfigPtr>& partial_configs);

    template<class T>
    T get(const std::string& key) const {
        ASSERT(m_finalized);
        const auto it{m_values.find(key)};
        ASSERT(it != m_values.end());
        return it->second.get<T>();
    }

    bool operator==(const ConfigView& other) const;

    void finalize();

    const std::map<std::string, ConfigValue>& values() const;

    std::size_t hash() const;

    std::vector<std::string> diff_keys(const ConfigView& other) const;

    const Domain::Preset::HwPrinterConfig& hw_config() const;

protected:
    bool m_finalized{false};
    FullConfigPtr m_full_config;
    std::vector<PartialConfigPtr> m_partial_configs;
    std::vector<std::string> m_keys;
    std::map<std::string, ConfigValue> m_values;
    Domain::Preset::HwPrinterConfig m_hw_config;

    ConfigValue resolve_value(const std::string& key) const;
};

} // namespace Slic3r::Domain
