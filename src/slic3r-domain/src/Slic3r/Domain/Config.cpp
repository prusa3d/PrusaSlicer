#include "Slic3r/Domain/Config.hpp"
#include "Slic3r/Assert.hpp"
#include "Slic3r/Domain/Preset/HwConfig.hpp"
#include "Slic3r/Domain/Types.hpp"
#include "Slic3r/Domain/ConfigDefsFDM.hpp"
#include "Slic3r/Domain/ConfigDefsSLA.hpp"

#include <algorithm>
#include <numeric>

namespace std {

template <>
struct hash<Slic3r::Domain::Percentage>
{
    std::size_t operator()(const Slic3r::Domain::Percentage& v) const noexcept
    {
        return std::hash<double>{}(v.value);
    }
};

template <>
struct hash<Slic3r::Domain::FloatOrPercentage>
{
    std::size_t operator()(const Slic3r::Domain::FloatOrPercentage& v) const noexcept
    {
        const std::size_t seed{std::hash<double>{}(v.get_abs_value(1.0))};
        return v.is_percentage() ? seed ^ 0x9e3779b9 : seed;
    }
};

template <>
struct hash<Slic3r::Domain::Vec2d>
{
    std::size_t operator()(const Slic3r::Domain::Vec2d& v) const noexcept
    {
        std::size_t seed{std::hash<double>{}(v.x())};
        boost::hash_combine(seed, std::hash<double>{}(v.y()));
        return seed;
    }
};

    template <>
    struct hash<Slic3r::Domain::Vec3d>
    {
        std::size_t operator()(const Slic3r::Domain::Vec3d& v) const noexcept
        {
            std::size_t seed{std::hash<double>{}(v.x())};
            boost::hash_combine(seed, std::hash<double>{}(v.y()));
            boost::hash_combine(seed, std::hash<double>{}(v.z()));
            return seed;
        }
    };
}

namespace Slic3r::Domain {

ConfigItem::ConfigItem(const ConfigItemDef& def, ConfigLocation location)
    : m_value{def.init_fn ? def.init_fn() : def.init_fn_ex(location)}, m_current_location{location}, m_def{&def}
{}

CompatibilityRule ConfigItem::compatibility_rule() const
{
    return m_def->compatibility_rule;
}

ConfigItems::ConfigItems(const ConfigDefinitions& defs, const ConfigLocation& location) :
    m_location{location}
{
    for (const ConfigItemDef& def : defs.defs()) {
        if (def.location == location) {
            m_items.push_back(ConfigItem(def, location));
        }
    }
}

const ConfigItem& ConfigItems::opt(const std::string_view key) const
{ return const_cast<ConfigItems*>(this)->opt(key); }

ConfigItem& ConfigItems::opt(const std::string_view key)
{
    auto it = std::lower_bound(m_items.begin(), m_items.end(), key,
        [](const ConfigItem& i, const auto& val) { return i.def().name < val; });
    if (it != m_items.end() && it->def().name == key)
        return *it;
    PANIC("Option not found", key);
    throw std::exception(); // to silence a warning
}

namespace {
template<typename Range>
std::optional<std::size_t> find_item_index(Range& items, const std::string& key)
{
    const auto it{std::lower_bound(
        items.begin(), items.end(), key,
        [](const ConfigItem& i, const auto& val) { return i.def().name < val; }
    )};
    if (it == items.end() || it->name() != key) {
        return std::nullopt;
    }
    return std::distance(items.begin(), it);
}

template<typename Range>
auto find_item(Range& items, const std::string& key) -> decltype(&(*items.begin()))
{
    if (const auto index{find_item_index(items, key)}) {
        return &items[*index];
    }
    return nullptr;
}

template <typename K, typename V>
std::vector<K> diff_keys(
    const std::map<K, V>& a,
    const std::map<K, V>& b,
    std::function<bool(const V&, const V&)> equals_comp
)
{
    std::vector<K> result;

    auto a_it{a.begin()};
    auto b_it{b.begin()};
    const auto a_end{a.end()};
    const auto b_end{b.end()};

    while(a_it != a_end && b_it != b_end) {
        const auto& [a_key, a_value]{*a_it};
        const auto& [b_key, b_value]{*b_it};

        if (a_key < b_key) {
            result.push_back(a_key);
            ++a_it;
        } else if (a_key > b_key) {
            result.push_back(b_key);
            ++b_it;
        } else {
            if (!(equals_comp(a_value, b_value))) {
                result.push_back(a_key);
            }
            ++a_it;
            ++b_it;
        }
    }

    while (a_it != a.end()) {
        result.push_back(a_it->first);
        ++a_it;
    }
    while (b_it != b.end()) {
        result.push_back(b_it->first);
        ++b_it;
    }

    return result;
}
}

ConfigItem* ConfigItems::find(const std::string& key)
{
    return find_item(m_items, key);
}

const ConfigItem* ConfigItems::find(const std::string& key) const
{
    return find_item(m_items, key);
}

const std::vector<ConfigItem>& ConfigItems::all_items() const
{
    return m_items;
}

std::vector<ConfigItem>& ConfigItems::all_items()
{
    return m_items;
}

std::vector<std::string> ConfigItems::diff_keys(const ConfigItems& other) const
{
    ASSERT(m_location == other.m_location && m_items.size() == other.m_items.size());

    std::vector<std::string> result;

    for (std::size_t i{}; i < m_items.size(); ++i) {
        const ConfigItem& a{m_items[i]};
        const ConfigItem& b{other.m_items[i]};
        ASSERT(a.name() == b.name());
        if (a != b) {
            result.push_back(a.name());
        }
    }
    return result;
}

ConfigOverrides::ConfigOverrides(const ConfigDefinitions& defs, const ConfigLocation location)
{
    for (const ConfigItemDef& def : defs.defs()) {
        if (def.overrides_in.contains(location)) {
            m_items.emplace_back(ConfigItem(def, location));
        }
    }
}

void ConfigOverrides::disable(const std::string& key)
{
    m_used_overrides.erase(key);
}

void ConfigOverrides::enable(const std::string& key)
{
    const auto item_index{find_item_by_index(key)};
    m_used_overrides.insert({key, item_index});
}

std::size_t ConfigOverrides::size() const {
    return m_used_overrides.size();
}

bool ConfigOverrides::empty() const
{
    return m_used_overrides.empty();
}

std::optional<ConfigItem> ConfigOverrides::get(const std::string& key) const
{
    // In debug, check that the key even should be here.
    DEBUG_ASSERT(
        this->find(key) != nullptr,
        "The " + key + " is not part of this instance of ConfigOverrides!"
    );

    const auto it{m_used_overrides.find(key)};
    if (it == m_used_overrides.end()) {
        return std::nullopt;
    }
    return m_items.at(it->second);
}

ConfigItem* ConfigOverrides::find(const std::string& key)
{
    return find_item(m_items, key);
}
const ConfigItem* ConfigOverrides::find(const std::string& key) const
{
    return find_item(m_items, key);
}

std::vector<std::reference_wrapper<const ConfigItem>> ConfigOverrides::overridden_items() const
{
    std::vector<std::reference_wrapper<const ConfigItem>> result;

    std::ranges::transform(m_used_overrides, std::back_inserter(result), [&](const auto& pair) {
        return std::ref(m_items.at(pair.second));
    });

    return result;
}

std::vector<ConfigItem>& ConfigOverrides::all_items()
{
    return m_items;
}

const std::vector<ConfigItem>& ConfigOverrides::all_items() const
{
    return m_items;
}

std::size_t ConfigOverrides::find_item_by_index(const std::string& key) const
{
    const auto index{find_item_index(m_items, key)};
    ASSERT(index, "The key does not belong to this!");
    return *index;
}

std::vector<std::string> ConfigOverrides::diff_overriden_keys(const ConfigOverrides& other) const
{
    return diff_keys<std::string, std::size_t>(
        m_used_overrides,
        other.m_used_overrides,
        [&](std::size_t index_a, std::size_t index_b)
        { return m_items.at(index_a) == other.m_items.at(index_b); }
    );
}

FindResult ConfigBox::find(const std::string &key)
{
    if (auto* item{overrides.find(key)}) {
        return {item, true};
    }
    return {items.find(key), false};
}

ConstFindResult ConfigBox::find(const std::string &key) const
{
    if (auto* item{overrides.find(key)}) {
        return {item, true};
    }
    return {items.find(key), false};
}

std::vector<std::string> ConfigBox::diff_keys(const ConfigBox& other) const
{
    std::vector<std::string> result{items.diff_keys(other.items)};
    const std::vector<std::string> overriden_diff_keys{
        overrides.diff_overriden_keys(other.overrides)
    };

    result.insert(result.end(), overriden_diff_keys.begin(), overriden_diff_keys.end());

    return result;
}

static std::vector<size_t> get_tool_slot_counts(const Preset::HwPrinterConfig& hw_config)
{
    std::vector<size_t> counts;
    for (size_t i{}; i < hw_config.tool_count; ++i) {
        size_t count{1};
        for (const auto& [address, feeder] : hw_config.feeders) {
            ASSERT(!address.empty());
            if (address.front() == static_cast<uint8_t>(i)) {
                count += feeder.slot_count - 1;
            }
        }
        counts.push_back(count);
    }
    return counts;
}

static BoxOrBoxesVector
spread_feeders(const BoxOrBoxesVector& boxes, const Preset::HwPrinterConfig& hw_config)
{
    BoxOrBoxesVector result;
    for (const auto& box_or_boxes : boxes) {
        if (std::holds_alternative<BoxRef>(box_or_boxes)) {
            result.push_back(box_or_boxes);
        } else {
            ASSERT(std::holds_alternative<BoxRefs>(box_or_boxes));
            const BoxRefs& boxes{std::get<BoxRefs>(box_or_boxes)};
            if (boxes.size() == hw_config.material_slot_count()) {
                result.push_back(box_or_boxes);
                continue;
            }

            ASSERT(boxes.size() == hw_config.tools.size());

            BoxRefs spread_boxes;
            const std::vector<std::size_t> tool_slot_counts{get_tool_slot_counts(hw_config)};
            for (std::size_t tool_index{}; tool_index < hw_config.tools.size(); ++tool_index) {
                const BoxRef box_ref{boxes.at(tool_index)};
                spread_boxes.resize(spread_boxes.size() + tool_slot_counts.at(tool_index), box_ref);
            }
            result.push_back(spread_boxes);
        }
    }
    return result;
}

struct ConfigValueAndDef
{
    ConfigValue value;
    std::optional<ConfigValue> original_value;
    const ConfigItemDef* def{nullptr};
};

using ConfigItemRef = std::reference_wrapper<const ConfigItem>;
static ConfigValue extract_values(const std::vector<ConfigItemRef>& items)
{
    ASSERT(!items.empty());
    return items.front().get().visit([&](auto&& value) -> ConfigValue {
        using ValueType = std::remove_cvref_t<decltype(value)>;
        if constexpr (std::is_same_v<ValueType, EnumWrapper>) {
            std::vector<int> enum_values;
            std::ranges::transform(items, std::back_inserter(enum_values), [](const ConfigItemRef& item) {
                return item.get().get<EnumWrapper>().value();
            });

            const auto first_enum{items.front().get().get<EnumWrapper>()};
            const EnumVectorWrapper
                enum_vector{enum_values, first_enum.type(), first_enum.def()};
            return ConfigValue{enum_vector};
        } else if constexpr (
            std::is_same_v<ValueType, EnumVectorWrapper>
            || is_std_vector_v<ValueType>
        ) {
            // This can be implemented later simply by adding variants of ConfigValue.
            // The only special case is the EnumVectorWrapper.
            PANIC("Vectors of vectors are not supported!");
        } else {
            std::vector<ValueType> item_values;
            std::ranges::transform(items, std::back_inserter(item_values), [](const ConfigItemRef& item) {
                return item.get().get<ValueType>();
            });
            return ConfigValue{item_values};
        }
    });
}

static ConfigValue extract_values(const std::vector<ConfigItem>& items)
{
    std::vector<ConfigItemRef> refs;
    refs.insert(refs.end(), items.begin(), items.end());
    return extract_values(refs);
}

static ConfigValue spread_values(const ConfigItem& item, const std::size_t size)
{
    const std::vector<ConfigItem> items(size, item);
    return extract_values(items);
}

static bool is_defined_per_material_slot(const ConfigItemDef& def)
{
    if (def.location == ConfigLocation{FDMConfigLocation::Tool}
        || def.location == ConfigLocation{FDMConfigLocation::Filament})
    {
        return true;
    }
    if (def.overrides_in.contains(FDMConfigLocation::Tool)
        || def.overrides_in.contains(FDMConfigLocation::Filament))
    {
        return true;
    }
    return false;
}

static ConfigValueAndDef
normalize(const ConfigItem& item, std::size_t material_slot_count)
{
    ConfigValueAndDef normalized_value;
    normalized_value.def = &item.def();
    if (is_defined_per_material_slot(item.def())) {
        normalized_value.value = spread_values(item, material_slot_count);
    } else {
        normalized_value.value = item.value();
    }
    return normalized_value;
};

static void set_vector_value(ConfigValue& vector, std::size_t index, const ConfigValue& value)
{
    vector.visit(
        [&](auto&& vector)
        {
            using VectorType = std::remove_cvref_t<decltype(vector)>;
            if constexpr (Domain::is_std_vector_v<VectorType>) {
                using ValueType  = VectorType::value_type;
                vector.at(index) = value.get<ValueType>();
            } else if constexpr (std::is_same_v<VectorType, EnumVectorWrapper>) {
                const int enum_value{value.get<EnumWrapper>().value()};
                vector.raw().at(index) = enum_value;
            } else {
                PANIC("Vector is expected!");
            }
        });
}

static std::map<std::string, ConfigValueAndDef>
squash_boxes(const BoxOrBoxesVector& boxes, std::size_t material_slot_count)
{
    std::map<std::string, ConfigValueAndDef> result;
    for (const auto& box_or_boxes : boxes) {
        std::visit(
            Domain::overloaded{
                [&](const BoxRef& box)
                {
                    for (const auto& item : box.get().items.all_items()) {
                        const auto [it, inserted]{
                            result.insert({item.def().name, normalize(item, material_slot_count)})};
                        it->second.original_value = item.value();
                        ASSERT(inserted);
                    }

                    for (const auto& item : box.get().overrides.overridden_items()) {
                        const std::string& name{item.get().name()};
                        auto it{result.find(name)};
                        const ConfigValueAndDef value{normalize(item.get(), material_slot_count)};
                        if (it == result.end()) {
                            result[name] = value;
                        } else {
                            it->second = value;
                        }
                    }
                },
                [&](const BoxRefs& boxes)
                {
                    ASSERT(!boxes.empty());

                    for (std::size_t box_index{}; box_index < boxes.size(); ++box_index) {
                        const ConfigBox& box{boxes[box_index].get()};
                        for (const auto& item : box.items.all_items()) {
                            auto it{result.find(item.name())};
                            if (it == result.end()) {
                                result.insert({item.name(), normalize(item, material_slot_count)});
                            } else {
                                set_vector_value(it->second.value, box_index, item.value());
                            }
                        }

                        for (const auto& item : box.overrides.overridden_items()) {
                            auto it{result.find(item.get().name())};
                            if (it == result.end()) {
                                result.insert({item.get().name(), normalize(item.get(), material_slot_count)});
                            } else {
                                set_vector_value(it->second.value, box_index, item.get().value());
                            }
                        }
                    }
                }},
            box_or_boxes);
    }
    return result;
}

static void
ensure_slot_parity(std::map<std::string, ConfigValueAndDef>& squashed_boxes, const std::size_t size)
{
    for (auto& [_, item] : squashed_boxes) {
        if (!item.def || !item.def->require_tool_parity) {
            continue;
        }

        item.value.visit(overloaded{
            [size]<typename T>(std::vector<T>& vector)
            {
                if (vector.size() == size) {
                    return;
                }
                ASSERT(vector.size() == 1);
                vector.resize(size, vector.front());
            },
            []<typename T>(const T& v)
            { PANIC("Only types of std::vector<T> can have require_tool_parity set to true"); }});
    }
}

static FloatOrPercentage get_average(const std::vector<FloatOrPercentage>& values)
{
    double sum{};
    for (const FloatOrPercentage& value : values) {
        ASSERT(!value.is_percentage());
        sum += value.float_value();
    }
    return {sum / (double) values.size()};
}

static Percentage get_average(const std::vector<Percentage>& values)
{
    double sum{};
    for (const Percentage& value : values) {
        sum += value.value;
    }
    return {sum / (double) values.size()};
}

static double get_average(const std::vector<double>& values)
{
    double sum{};
    for (const double& value : values) {
        sum += value;
    }
    return sum / (double) values.size();
}

template <typename T>
static T get_min(const std::vector<T>& values)
{
    using Type = std::remove_cvref_t<T>;
    if constexpr (std::is_same_v<Type, FloatOrPercentage>) {
        double min{std::numeric_limits<double>::max()};
        for (const FloatOrPercentage& value : values) {
            ASSERT(!value.is_percentage());
            if (value.float_value() < min) {
                min = value.float_value();
            }
        }
        return FloatOrPercentage{min};
    } else if constexpr (std::is_same_v<Type, Percentage>) {
        double min{std::numeric_limits<double>::max()};
        for (const Percentage& value : values) {
            if (value.value < min) {
                min = value.value;
            }
        }
        return Percentage{min};
    } else {
        auto min{std::numeric_limits<T>::max()};
        for (const T& value : values) {
            if (value < min) {
                min = value;
            }
        }
        return {min};
    }
}

template <typename T>
static T get_max(const std::vector<T>& values)
{
    using Type = std::remove_cvref_t<T>;
    if constexpr (std::is_same_v<Type, FloatOrPercentage>) {
        double max{std::numeric_limits<double>::lowest()};
        for (const FloatOrPercentage& value : values) {
            ASSERT(!value.is_percentage());
            if (value.float_value() > max) {
                max = value.float_value();
            }
        }
        return FloatOrPercentage{max};
    } else if constexpr (std::is_same_v<Type, Percentage>) {
        double max{std::numeric_limits<double>::lowest()};
        for (const Percentage& value : values) {
            if (value.value > max) {
                max = value.value;
            }
        }
        return Percentage{max};
    } else {
        auto max{std::numeric_limits<T>::lowest()};
        for (const T& value : values) {
            if (value > max) {
                max = value;
            }
        }
        return {max};
    }
}

static std::pair<ConfigValue, bool> apply_compatibility_rule(
    const std::string& name,
    const std::vector<ConfigValue>& values,
    const ConfigValue& value_before_overrides,
    CompatibilityRule compatibility_rule)
{
    const bool all_same{std::ranges::all_of(
        values,
        [&](const ConfigValue& value) { return value == values.front(); })};
    if (all_same) {
        return {values.front(), false};
    }

    ASSERT(!values.empty());
    ConfigValue result{values.front().visit(Domain::overloaded{
        [&]<typename T>(const std::vector<T>&)
        {
            PANIC("Compatibility rule cannot be applied to a set of vectors!");
            return ConfigValue{0};
        },
        [&](const EnumVectorWrapper&)
        {
            PANIC("Compatibility rule cannot be applied to a set of enums!");
            return ConfigValue{0};
        },
        [&]<typename T>(const T&)
        {
            if (compatibility_rule == CompatibilityRule::IgnoreOverrides) {
                return value_before_overrides;
            }

            std::vector<T> vector;
            std::ranges::transform(
                values,
                std::back_inserter(vector),
                [](const ConfigValue& value) { return value.get<T>(); });

            if constexpr (
                std::is_same_v<T, int>
                || std::is_same_v<T, FloatOrPercentage>
                || std::is_same_v<T, Percentage>
                || std::is_same_v<T, double>)
            {
                switch (compatibility_rule) {
                case CompatibilityRule::Average: {
                    if constexpr (
                        std::is_same_v<T, double>
                        || std::is_same_v<T, Percentage>
                        || std::is_same_v<T, FloatOrPercentage>)
                    {
                        return ConfigValue{get_average(vector)};
                    }
                } break;
                case CompatibilityRule::Min:
                    return ConfigValue{get_min(vector)};
                case CompatibilityRule::Max:
                    return ConfigValue{get_max(vector)};
                default:
                    break; // Intentionally pass.
                }
            }
            PANIC("Failed to apply compatibility rule: {}", name);
            return ConfigValue{0};
        },
    })};

    return {result, true};
}

const ConfigItemDef* find_def(const std::string& key, PrinterTechnology printer_technology)
{
    if (printer_technology == PrinterTechnology::FFF) {
        auto fdm_it{std::lower_bound(
            get_defs_fdm().defs().begin(),
            get_defs_fdm().defs().end(),
            key,
            [](const ConfigItemDef& def, const std::string& key) { return def.name < key; })};

        if (fdm_it != get_defs_fdm().defs().end() && fdm_it->name == key) {
            return &*fdm_it;
        }
    }

    if (printer_technology == PrinterTechnology::SLA) {
        auto sla_it{std::lower_bound(
            get_defs_sla().defs().begin(),
            get_defs_sla().defs().end(),
            key,
            [](const ConfigItemDef& def, const std::string& key) { return def.name < key; })};

        if (sla_it != get_defs_sla().defs().end() && sla_it->name == key) {
            return &*sla_it;
        }
    }

    return nullptr;
}

static double resolve_float_or_percentage_scalar(
    const std::string& key,
    const ConfigValue& value,
    std::map<std::string, ConfigValue>& values,
    PrinterTechnology printer_technology
)
{
    if (value.holds_alternative<double>()) {
        return value.get<double>();
    }
    ASSERT(value.holds_alternative<FloatOrPercentage>());
    const FloatOrPercentage float_or_percentage{value.get<FloatOrPercentage>()};
    if (!float_or_percentage.is_percentage()) {
        return float_or_percentage.float_value();
    }

    const ConfigItemDef* def{find_def(key, printer_technology)};
    ASSERT(def);

    const ConfigValue& parent{values.at(def->ratio_over)};
    const double parent_value{
        resolve_float_or_percentage_scalar(def->ratio_over, parent, values, printer_technology)};
    return float_or_percentage.get_abs_value(parent_value);
}

static double resolve_float_or_percentage_for_tool(
    const std::string& key,
    const ConfigValue& value,
    std::size_t tool_index,
    std::map<std::string, ConfigValue>& values,
    PrinterTechnology printer_technology
)
{
    if (value.holds_alternative<double>()) {
        return value.get<double>();
    } else if (value.holds_alternative<std::vector<double>>()){
        return value.get<std::vector<double>>().at(tool_index);
    }

    const FloatOrPercentage float_or_percentage{value.visit(Domain::overloaded{
        [](const FloatOrPercentage& single_value) { return single_value; },
        [&](const std::vector<FloatOrPercentage>& vector) { return vector.at(tool_index); },
        [&](auto&&)
        {
            PANIC("Invalid float or percentage!");
            return FloatOrPercentage{};
        },
    })};

    if (!float_or_percentage.is_percentage()) {
        return float_or_percentage.float_value();
    }

    const ConfigItemDef* def{find_def(key, printer_technology)};
    ASSERT(def);

    const ConfigValue& parent{values.at(def->ratio_over)};
    const double parent_value{resolve_float_or_percentage_for_tool(
        def->ratio_over,
        parent,
        tool_index,
        values,
        printer_technology)};
    return float_or_percentage.get_abs_value(parent_value);
}

static void resolve_float_or_percentages(
    std::map<std::string, ConfigValue>& values,
    PrinterTechnology printer_technology)
{
    for (auto& [key, value] : values) {
        const ConfigItemDef* def{find_def(key, printer_technology)};
        if (!def) {
            continue;
        }

        value.visit(Domain::overloaded{
            [&](FloatOrPercentage& value)
            {
                value = resolve_float_or_percentage_scalar(
                    key,
                    ConfigValue{value},
                    values,
                    printer_technology);
            },
            [&](std::vector<FloatOrPercentage>& value)
            {
                for (std::size_t tool_index{}; tool_index < value.size(); ++tool_index) {
                    value[tool_index] = resolve_float_or_percentage_for_tool(
                        key,
                        ConfigValue{value},
                        tool_index,
                        values,
                        printer_technology);
                }
            },
            []<typename T>(const T&)
            {
                // Intentionally skip.
            },
        });
    }
}

static void apply_compatibility_rules(
    std::map<std::string, ConfigValue>& values,
    const std::map<std::string, ConfigValue>& original_values,
    const std::vector<unsigned>& extruder_candidates,
    PrinterTechnology printer_technology)
{
    const std::set<unsigned>
        extruder_candidates_set{extruder_candidates.begin(), extruder_candidates.end()};

    for (auto& [key, value] : values) {
        const ConfigItemDef* def{find_def(key, printer_technology)};

        if (!def || def->compatibility_rule == CompatibilityRule::Undefined) {
            continue;
        }
        const std::vector<ConfigValue> values{value.visit(Domain::overloaded{
            [&]<typename T>(const std::vector<T>& vector)
            {
                std::vector<ConfigValue> result;
                for (std::size_t tool_index{}; tool_index < vector.size(); ++tool_index) {
                    if (!extruder_candidates_set.empty()
                        && !extruder_candidates_set.contains(tool_index))
                    {
                        continue;
                    }
                    result.push_back(ConfigValue{static_cast<T>(vector[tool_index])});
                }

                return result;
            },
            [&](const EnumVectorWrapper& enum_vector)
            {
                std::vector<ConfigValue> result;
                for (std::size_t tool_index{}; tool_index < enum_vector.values().size();
                     ++tool_index) {
                    if (!extruder_candidates_set.empty()
                        && !extruder_candidates_set.contains(tool_index))
                    {
                        continue;
                    }
                    result.push_back(ConfigValue{
                        EnumWrapper{enum_vector.values().at(tool_index), enum_vector.type(), enum_vector.def()}});
                }
                return result;
            },
            [&]<typename T>(const T&)
            {
                PANIC("Compatibility rule can only be applied to a vector.");
                return std::vector<ConfigValue>{};
            },
        })};

        value =
            apply_compatibility_rule(
                def->name,
                values,
                original_values.at(def->name),
                def->compatibility_rule)
                .first;
    }
}

static std::vector<double> get_nozzle_diameters(const Domain::Preset::HwPrinterConfig& hw_config)
{
    std::vector<double> result;

    const std::vector<std::size_t> slot_counts{get_tool_slot_counts(hw_config)};
    ASSERT(slot_counts.size() == hw_config.tools.size());

    for (std::size_t i{}; i < hw_config.tools.size(); ++i) {
        const Preset::HwToolConfig& tool{hw_config.tools[i]};
        const std::optional<double> nozzle_diameter{
            Domain::Preset::get_feature<double>(tool.features, "nozzle_diameter")};

        ASSERT(nozzle_diameter);
        result.resize(result.size() + slot_counts.at(i), *nozzle_diameter);
    }

    return result;
}

static std::vector<bool> get_nozzle_high_flows(const Domain::Preset::HwPrinterConfig& hw_config)
{
    std::vector<bool> result;

    for (const auto& tool : hw_config.tools) {
        const std::optional<bool> high_flow{
            Domain::Preset::get_feature<bool>(tool.features, "nozzle_high_flow")
        };
        result.push_back(high_flow.value_or(false));
    }

    return result;
}

static ConfigValueAndDef*
find_param(std::map<std::string, ConfigValueAndDef>& squashed_boxes, const std::string& name)
{
    auto it{squashed_boxes.find(name)};
    if (it == squashed_boxes.end()) {
        return nullptr;
    }
    return &it->second;
}

static void expand_parameters(std::map<std::string, ConfigValueAndDef>& squashed_boxes)
{
    if (const ConfigValueAndDef* extruder{find_param(squashed_boxes, "extruder")}) {
        if (extruder->value != ConfigValue{0}) {
            for (const std::string& key :
                 {"perimeter_extruder", "infill_extruder", "solid_infill_extruder"})
            {
                ConfigValueAndDef* specific_extruder{find_param(squashed_boxes, key)};
                if (!specific_extruder) {
                    squashed_boxes[key] = ConfigValueAndDef{extruder->value, std::nullopt, nullptr};
                } else if (specific_extruder->value == ConfigValue{0}) {
                    specific_extruder->value = extruder->value;
                }
            }
        }
        squashed_boxes.erase("extruder");
    }

    if (const ConfigValueAndDef* first_layer_speed{find_param(squashed_boxes, "first_layer_speed")})
    {
        for (
            const std::string& key :
            {
                "first_layer_infill_speed",
                "first_layer_solid_infill_speed",
                "first_layer_perimeter_speed",
                "first_layer_external_perimeter_speed",
                "first_layer_support_material_speed",
                "first_layer_top_solid_infill_speed",
                "first_layer_gap_fill_speed",
            })
        {
            ConfigValueAndDef* specific_speed{find_param(squashed_boxes, key)};

            if (!specific_speed) {
                squashed_boxes[key] =
                    ConfigValueAndDef{first_layer_speed->value, std::nullopt, nullptr};
            } else {
                const auto first_layer_speed_value{first_layer_speed->value.get<std::vector<FloatOrPercentage>>()};
                specific_speed->value.visit(overloaded{
                    [&](std::vector<FloatOrPercentage>& values){
                        for (std::size_t i{}; i < values.size(); ++i) {
                            FloatOrPercentage& value{values[i]};
                            if (value.is_zero()) {
                                value = first_layer_speed_value.at(i);
                            }
                        }
                    },
                    [](auto&&){
                        PANIC("Vector of values is expected!");
                    },
                });
            }
        }
        squashed_boxes.erase("first_layer_speed");
    }
}

SquashedConfig::SquashedConfig(
    const BoxOrBoxesVector& all_boxes,
    const std::vector<unsigned>& extruder_candidates,
    const Preset::HwPrinterConfig& hw_config) :
    m_hw_config{hw_config},
    m_extruder_candidates{extruder_candidates}
{
    const BoxOrBoxesVector boxes{spread_feeders(all_boxes, hw_config)};
    const std::size_t material_slot_count{hw_config.material_slot_count()};

    for (const auto& box_or_boxes : boxes) {
        std::visit(
            overloaded{
                [](const BoxRef&) {},
                [&](const BoxRefs& box_refs) { ASSERT(box_refs.size() == material_slot_count); },
            },
            box_or_boxes);
    }

    std::map<std::string, ConfigValueAndDef> squashed_boxes{
        squash_boxes(boxes, material_slot_count)};

    if (hw_config.technology == PrinterTechnology::FFF) {
        squashed_boxes["nozzle_diameter"] =
            ConfigValueAndDef{ConfigValue{get_nozzle_diameters(hw_config)}, std::nullopt, nullptr};

        squashed_boxes["nozzle_high_flow"] =
            ConfigValueAndDef{ConfigValue{get_nozzle_high_flows(hw_config)}, std::nullopt, nullptr};
    }

    ensure_slot_parity(squashed_boxes, hw_config.material_slot_count());

    expand_parameters(squashed_boxes);

    for (const auto& [key, item] : squashed_boxes) {
        m_values[key] = item.value;
        if (item.original_value) {
            m_original_values[key] = *item.original_value;
        }
    }
}

const Preset::HwPrinterConfig& SquashedConfig::hw_config() const {
    return m_hw_config;
}

std::vector<std::string> SquashedConfig::diff_keys(const SquashedConfig& other) const
{
    return ::Slic3r::Domain::diff_keys<std::string, ConfigValue>(
        m_values,
        other.m_values,
        [](const ConfigValue& a, const ConfigValue& b) { return a == b; }
    );
}

bool SquashedConfig::operator==(const SquashedConfig& other) const
{
    return diff_keys(other).empty();
}

const std::map<std::string, ConfigValue>& SquashedConfig::values() const
{
    return m_values;
}

std::pair<ConfigValue, bool> apply_compatibility_rule(
    const ConfigValue* default_value,
    const std::vector<const ConfigItem*>& items,
    const std::vector<unsigned int>& extruder_candidates
)
{
    return apply_compatibility_rule(
        default_value,
        items,
        std::set<unsigned>{extruder_candidates.cbegin(), extruder_candidates.cend()}
    );
}

std::pair<ConfigValue, bool> apply_compatibility_rule(
    const ConfigValue* default_value,
    const std::vector<const ConfigItem*>& items,
    const std::set<unsigned>& extruder_candidates)
{
    auto it{std::ranges::find_if(items, [](const ConfigItem* item) { return item != nullptr; })};
    if (it == items.end()) {
        ASSERT(default_value);
        return {*default_value, false};
    }

    const ConfigItem& first_item{**it};

    const CompatibilityRule compatibility_rule{first_item.def().compatibility_rule};
    if (compatibility_rule == CompatibilityRule::Undefined) {
        return {*default_value, false};
    }

    std::vector<ConfigValue> values;
    for (std::size_t tool_index{}; tool_index < items.size(); ++tool_index) {
        const auto& item{items[tool_index]};
        if (!extruder_candidates.empty() && !extruder_candidates.contains(tool_index)) {
            continue;
        }
        if (item != nullptr) {
            values.push_back(item->value());
        } else {
            values.push_back(*default_value);
        }
    }

    return apply_compatibility_rule(
        first_item.def().name,
        values,
        *default_value,
        compatibility_rule);
}

const std::vector<std::string>& FullConfig::keys() const
{
    return m_keys;
}

FullConfig::FullConfig(
    const BoxOrBoxesVector& input,
    const std::vector<unsigned>& extruder_candidates,
    const Preset::HwPrinterConfig& hw_config) :
    SquashedConfig{input, extruder_candidates, hw_config}
{
    std::ranges::transform(
        m_values,
        std::back_inserter(m_keys),
        [](const auto& pair) { return pair.first; });
}

const ConfigValue& FullConfig::get_value(const std::string& key) const
{
    const auto it{m_values.find(key)};
    ASSERT(it != m_values.end(), "The key '" + key + "' is not part of this config.");
    return it->second;
}

PartialConfig::PartialConfig(
    const BoxOrBoxesVector& input,
    const Preset::HwPrinterConfig& hw_config
) :
    SquashedConfig{input, {}, hw_config}
{}

std::optional<ConfigValue> PartialConfig::get_value(const std::string& key) const
{
    const auto it{m_values.find(key)};
    if (it == m_values.end()) {
        return std::nullopt;
    }
    return it->second;
}

namespace {
template <typename T>
std::size_t hash(const T& value)
{
    if constexpr (!std::is_same_v<T, std::string>
                  && !std::is_same_v<T, std::vector<bool>>
                  && std::ranges::range<T>)
    {
        std::size_t seed = 0;
        for (const auto& v : value) {
            boost::hash_combine(seed, hash(v));
        }
        return seed;
    } else {
        return std::hash<T>{}(value);
    }
}

std::size_t hash(const ConfigValue& value)
{
    return value.visit(
        [](auto&& value)
        {
            using ValueType = std::remove_cvref_t<decltype(value)>;
            if constexpr (std::is_same_v<ValueType, EnumVectorWrapper>) {
                const EnumVectorWrapper& wrapped_enums{value};
                return hash(wrapped_enums.values());
            } else if constexpr (std::is_same_v<ValueType, EnumWrapper>) {
                const EnumWrapper& wrapped_enum{value};
                return hash(wrapped_enum.value());
            } else if constexpr (std::is_same_v<ValueType, std::vector<EnumWrapper>>) {
                std::vector<int> values;
                for (const EnumWrapper& wrapped_enum : value) {
                    values.push_back(wrapped_enum.value());
                }
                return hash(values);
            } else {
                return hash(value);
            }
        }
    );
}
} // namespace

std::size_t SquashedConfig::hash() const
{
    std::size_t seed{0};
    for (const auto& [key, value] : m_values) {
        boost::hash_combine(seed, Domain::hash(key));
        boost::hash_combine(seed, Domain::hash(value));
    }
    return seed;
}

const std::map<std::string, ConfigValue>& SquashedConfig::original_values() const {
    return m_original_values;
}

const std::vector<unsigned>& SquashedConfig::extruder_candidates() const {
    return m_extruder_candidates;
}

ConfigView::ConfigView(
    FullConfigPtr full_config,
    const std::vector<PartialConfigPtr>& partial_configs
) :
    m_full_config{full_config},
    m_partial_configs{partial_configs},
    m_hw_config{full_config->hw_config()}
{
    std::vector<std::string> partial_config_keys;
    ASSERT(m_full_config);
    for (const PartialConfigPtr& ptr : m_partial_configs) {
        ASSERT(ptr);
        for (const auto& [key, _] : ptr->m_values) {
            partial_config_keys.push_back(key);
        }
    }

    m_keys = m_full_config->keys();
    m_keys.insert(m_keys.end(), partial_config_keys.begin(), partial_config_keys.end());
    std::ranges::sort(m_keys);
    m_keys.erase(std::unique(m_keys.begin(), m_keys.end()), m_keys.end());
}

bool ConfigView::operator==(const ConfigView& other) const
{
    if (m_finalized != other.m_finalized) {
        return false;
    }

    if (m_finalized) {
        return m_values == other.m_values;
    }

    if (m_partial_configs.size() != other.m_partial_configs.size()) {
        return false;
    }
    for (std::size_t i{}; i < m_partial_configs.size(); ++i) {
        if (m_partial_configs[i] != other.m_partial_configs[i]) {
            return false;
        }
    }

    return *m_full_config == *other.m_full_config;
}

void ConfigView::finalize() {
    for (const std::string& key : m_keys) {
        m_values[key] = resolve_value(key);
    }

    resolve_float_or_percentages(m_values, m_full_config->hw_config().technology);
    apply_compatibility_rules(
        m_values,
        m_full_config->original_values(),
        m_full_config->extruder_candidates(),
        m_full_config->hw_config().technology
    );

    m_finalized = true;

    m_full_config = {};
    m_partial_configs = {};
}

const std::map<std::string, ConfigValue>& ConfigView::values() const {
    return m_values;
}

std::size_t ConfigView::hash() const
{
    ASSERT(!m_finalized);
    std::size_t seed{m_full_config->hash()};
    for (const PartialConfigPtr& partial_config : m_partial_configs) {
        boost::hash_combine(seed, partial_config->hash());
    }
    return seed;
}

std::vector<std::string> ConfigView::diff_keys(const ConfigView& other) const
{
    std::vector<std::string> result;
    for (const std::string& key : m_keys) {
        if (m_values.at(key) != other.m_values.at(key)) {
            result.push_back(key);
        }
    }

    return result;
}

const Domain::Preset::HwPrinterConfig& ConfigView::hw_config() const {
    return m_hw_config;
}

ConfigValue ConfigView::resolve_value(const std::string& key) const
{
    ASSERT(!m_finalized);
    for (auto rev_it = m_partial_configs.rbegin(); rev_it != m_partial_configs.rend(); ++rev_it) {
        const PartialConfigPtr& partial_config{*rev_it};
        if (auto result{partial_config->get_value(key)}) {
            return *result;
        }
    }
    return m_full_config->get_value(key);
}

} // namespace Slic3r::Domain
