#include "Slic3r/Biz/Preset/PresetEvaluator.hpp"
#include <fmt/ranges.h>
#include "Slic3r/Biz/Preset/PresetCollectionEvaluator.hpp"
#include "Slic3r/Biz/Preset/ValueMapBuilder.hpp"
#include "Slic3r/Uuid.hpp"
#include "Slic3r/TypeInfo.hpp"
#include "Slic3r/Log.hpp"

#include <ranges>


namespace Slic3r::Biz::Preset {

namespace {
template <typename T, typename... Ts>
struct AnyTypeOf
{
    static constexpr bool value = (std::is_same<T, Ts>::value || ...);
};

template <typename FromType, typename ToType>
struct ValueCast
{
    static constexpr bool defined = false;
};

#define VALUE_CAST_DEF_STATIC(FromType, ToType)                                         \
template <>                                                                             \
struct ValueCast<FromType, ToType>                                                      \
{                                                                                       \
    static constexpr bool defined = true;                                               \
    static constexpr ToType cast(const FromType& v) { return static_cast<ToType>(v); }  \
};

VALUE_CAST_DEF_STATIC(double, bool);
VALUE_CAST_DEF_STATIC(double, int);
VALUE_CAST_DEF_STATIC(double, std::optional<int>);

template <>
struct ValueCast<double, Domain::Percentage>
{
    static constexpr bool defined = true;

    static constexpr Domain::Percentage cast(double v)
    {
        return Domain::Percentage{v};
    }
};

template <>
struct ValueCast<double, Domain::FloatOrPercentage>
{
    static constexpr bool defined = true;

    static Domain::FloatOrPercentage cast(double v)
    {
        return Domain::FloatOrPercentage{v};
    }
};

template <>
struct ValueCast<Domain::Percentage, Domain::FloatOrPercentage>
{
    static constexpr bool defined = true;

    static Domain::FloatOrPercentage cast(Domain::Percentage v)
    {
        return Domain::FloatOrPercentage{v};
    }
};

template <typename FromScalarT, typename ToScalarT>
    requires ValueCast<FromScalarT, ToScalarT>::defined
struct ValueCast<std::vector<FromScalarT>, std::vector<ToScalarT>>
{
    static constexpr bool defined = true;

    static constexpr std::vector<ToScalarT> cast(const std::vector<FromScalarT>& v)
    {
        std::vector<ToScalarT> dest;
        dest.reserve(v.size());
        for (const auto& vi : v)
            dest.push_back(ValueCast<FromScalarT, ToScalarT>::cast(vi));
        return dest;
    }
};

static_assert(ValueCast<std::vector<double>, std::vector<bool>>::defined);

#undef VALUE_CAST_DEF_STATIC

template <typename FromType>
struct CastingGetterVisitor
{
    Domain::ConfigItem& item;
    const FromType& value;

    template <typename ToType>
        requires ValueCast<FromType, ToType>::defined
    bool operator()(const ToType&)
    {
        item.set(ValueCast<FromType, ToType>::cast(value));
        return true;
    }

    template <typename ToType>
        requires(!ValueCast<FromType, ToType>::defined && std::is_same_v<FromType, ToType>)
    bool operator()(const ToType&)
    {
        item.set(value);
        return true;
    }

    template <typename ToType>
        requires(!ValueCast<FromType, ToType>::defined && !std::is_same_v<FromType, ToType>)
    bool operator()(const ToType& dest)
    {
        if constexpr (Domain::is_std_vector_v<FromType> && Domain::is_std_vector_v<ToType>) {
            // if we have empty vector as source, we don't really care about the FromType/ToType
            // so let's allow casting empty vector of any type to vector of any type
            if (value.empty()) {
                ToType dest_value{};
                item.set(dest_value);
                return true;
            }
        }
        SPDLOG_ERROR(
            "Type mismatched for item {}: source type: {}  dest type: {}",
            item.name(),
            type_name(value),
            type_name(dest)
        );
        return false;
    }
};

struct ConfigValueSetterVisitor
{
    Domain::ConfigItem& item;
    const bool is_override;

    explicit ConfigValueSetterVisitor(const Domain::FindResult& result) :
        item(*result.item),
        is_override(result.is_override)
    {}

    /*
    std::monostate,
    Bools, Doubles, Ints, OptInts, FloatOrPercentages, Vec2ds, Strings,
    bool, double, int, Percentage, Vec2d, std::string
     */
    bool operator()(const std::monostate& v)
    {
        if (!is_override) {
            item.set<std::optional<int>>(std::nullopt);
            return true;
        }
        return false;
    }

    template <typename ValueType>
        requires AnyTypeOf<ValueType, double, Domain::Percentage, Domain::Preset::FloatOrPercentages>::value
    bool operator()(const ValueType& v)
    {
        return item.visit(CastingGetterVisitor<ValueType>{item, v});
    }

    template <typename ValueType>
        requires AnyTypeOf<ValueType, std::string>::value
    bool operator()(const ValueType& v)
    {
        if (!item.holds_alternative<ValueType>() && !item.holds_alternative<Domain::EnumWrapper>()) {
            std::string dest_type_name = item.value().visit([](const auto& v) {
                return type_name(v);
            });
            SPDLOG_ERROR(
                "Type mismatched for item {}: source type: {}  dest type: {}",
                item.name(),
                type_name(v),
                dest_type_name
            );
            return false;
        }
        item.set(v);
        return true;
    }

    template <typename ValueType>
        requires AnyTypeOf<ValueType, Domain::Preset::Strings>::value
    bool operator()(const ValueType& v)
    {
        if (item.holds_alternative<std::string>()) {
            item.set(fmt::format("{}", fmt::join(v, ",")));
        } else if (!item.holds_alternative<ValueType>()
                   && !item.holds_alternative<Domain::EnumVectorWrapper>())
        {
            std::string dest_type_name = item.value().visit([](const auto& v) {
                return type_name(v);
            });
            SPDLOG_ERROR(
                "Type mismatched for item {}: source type: {}  dest type: {}",
                item.name(),
                type_name(v),
                dest_type_name
            );
            return false;
        } else
            item.set(v);
        return true;
    }

    template <typename ValueType>
        requires AnyTypeOf<
            ValueType,
            Domain::Vec2ds
        >::value
    bool operator()(const ValueType& v)
    {
        if (item.holds_alternative<std::string>()) {
            std::vector<std::string> values;
            for (const auto& vi : v)
                values.emplace_back(fmt::format("{}x{}", vi[0], vi[1]));
            item.set(fmt::format("{}", fmt::join(values, ",")));
        } else if (!item.holds_alternative<ValueType>()) {
            std::string dest_type_name = item.value().visit([](const auto& v) {
                return type_name(v);
            });
            SPDLOG_ERROR(
                "Type mismatched for item {}: source type: {}  dest type: {}",
                item.name(),
                type_name(v),
                dest_type_name
            );
            return false;
        } else
            item.set(v);
        return true;
    }

    template <typename ValueType>
        requires AnyTypeOf<
            ValueType,
            Domain::Preset::Bools,
            Domain::Preset::Doubles,
            Domain::Preset::Ints,
            Domain::Preset::OptInts,
            //Domain::Preset::Percentages,
            bool,
            int,
            Domain::Vec2d>::value
    bool operator()(const ValueType& v)
    {
        if (item.holds_alternative<ValueType>()) {
            item.set(v);
            return true;
        }


        if constexpr (Domain::is_std_vector_v<ValueType>) {
            const bool set = item.visit(
                [&v, &item=this->item]<typename T>(const T&) -> bool
                {
                    if constexpr (Domain::is_std_vector_v<T>)  {
                        if constexpr (ValueCast<ValueType, T>::defined) {
                            item.set(ValueCast<ValueType, T>::cast(v));
                            return true;
                        }

                        // if the vector is empty, it can be in incompatible type
                        // as the loader is unable to figure out what type the vector is
                        if (v.empty()) {
                            item.set(T{});
                            return true;
                        }
                    }
                    return false;
                }
            );
            if (set)
                return true;
        }

        std::string dest_type_name = item.value().visit([](const auto& v) {
            return type_name(v);
        });
        SPDLOG_ERROR(
            "Type mismatched for item {}: source type: {}  dest type: {}",
            item.name(),
            type_name(v),
            dest_type_name
        );
        return false;

    }
};

template <typename ConfigType>
    requires std::is_base_of_v<Domain::ConfigBox, ConfigType>
ConfigType config_values(
    const Domain::Preset::HwPrinterConfig& hw_config,
    const EvalPresetValueMap& values
)
{
    // Constructing ConfigType from ConfigDefinitions iterates all definitions and is
    // expensive. Use a prototype constructed once per type; cloning it is a cheap
    // vector copy that avoids the definition scan on every preset.
    static const ConfigType s_proto;
    ConfigType config = s_proto;
    for (const auto& [k, v] : values) {
        const auto q = config.find(k);
        if (q.item == nullptr) {
            SPDLOG_ERROR("Invalid key {} for {}", k, type_name(config));
            continue;
        }

        if (std::visit(ConfigValueSetterVisitor{q}, v) ) {
            // if value was written and this is override, we need to enable that override
            if (q.is_override) {
                config.overrides.enable(q.item->name());
            }
            if (q.item->def().require_tool_parity) {
                q.item->visit(
                    Domain::overloaded{
                        [n = hw_config.tool_count]<typename T>(std::vector<T>& val)
                        {
                            if (val.size() > n) {
                                // shrink original values
                                val.resize(n);
                            } else if (val.size() < n) {
                                // repeat original values
                                const size_t original_size = val.size();
                                while (val.size() < n) {
                                    const size_t idx = (val.size() - original_size) % original_size;
                                    val.push_back(val.at(idx));
                                }
                            }
                        },
                        [](auto& val)
                        {
                            PANIC("Unsupported type for tool_parity: {}", type_name(val));
                        }
                    }
                );
            }
        }
    }
    return config;
}

template <typename FdmConfigType, typename SlaConfigType>
Domain::Preset::EvaluatedPreset<FdmConfigType, SlaConfigType>::PresetValues config_values(
    const Domain::Preset::HwPrinterConfig& hw_config,
    const EvalPresetValueMap& values
)
{
    if (hw_config.technology == Domain::PrinterTechnology::FFF) {
        if constexpr (std::is_same_v<FdmConfigType, std::monostate>)
            PANIC("Unsupported config type");
        else
            return config_values<FdmConfigType>(hw_config, values);
    }
    if (hw_config.technology == Domain::PrinterTechnology::SLA) {
        if constexpr (std::is_same_v<SlaConfigType, std::monostate>)
            PANIC("Unsupported config type");
        else
            return config_values<SlaConfigType>(hw_config, values);
    }
    PANIC("Unsupported printer technology");
}

void update_printer_preset_from_hw_config(
    const Domain::Preset::HwPrinterConfig& hw_config,
    Domain::ConfigBox& printer_preset
)
{
    const auto it = printer_preset.find("printer_model");
    ASSERT(it.item != nullptr);
    it.item->set(hw_config.legacy_printer_model.value_or(hw_config.printer_id));
}


} // namespace

bool PresetEvaluator::EvalPresetContext::has_same_values(const EvalPresetContext& rhs) const
{
    // last_node_location and conditions left intentionally
    return root_id == rhs.root_id
        && id == rhs.id
        && name == rhs.name
        && match_mode == rhs.match_mode
        && values == rhs.values
        && features == rhs.features
        && origin == rhs.origin;
}

template <typename FdmConfigType, typename SlaConfigType>
Domain::Preset::EvaluatedPreset<FdmConfigType, SlaConfigType> PresetEvaluator::preset_from_context(
    const Domain::Preset::HwPrinterConfig& hw_config,
    Domain::Preset::PresetKind kind,
    const EvalPresetContext& context
)
{
    std::vector<std::string> conditions;
    if (!context.conditions.empty() || !context.negative_conditions.empty()) {
        std::ranges::transform(context.conditions, std::back_inserter(conditions),
            [](const std::string* p) { return *p; });
        std::ranges::transform(context.negative_conditions, std::back_inserter(conditions),
            [](const std::string* p) { return fmt::format("!({})", *p); });

        // remove duplicates
        auto to_remove = std::ranges::unique(conditions);
        conditions.erase(std::ranges::begin(to_remove), std::ranges::end(to_remove));
    }

    return {
        .kind       = kind,
        .origin     = context.origin,
        .user_file  = context.user_file,
        .root_id    = context.root_id,
        .id         = context.id.empty() ? generate_uuid() : context.id,
        .name       = context.name,
        .values     = config_values<FdmConfigType, SlaConfigType>(hw_config, context.values),
        .features   = context.features,
        .conditions = conditions,
        .last_node_location = context.last_node_location
    };
}

PresetEvaluator::EvalPresetContexts PresetEvaluator::merged_same_presets(EvalPresetContexts presets)
{
    EvalPresetContexts ret;

    for (auto& p : presets) {
        const bool is_unique =
            std::ranges::none_of(ret, [&p](const auto& other) { return p.has_same_values(other); });
        if (is_unique) {
            ret.emplace_back(std::move(p));
        }
    }
    return ret;
}

void PresetEvaluator::build_named_presets()
{
    m_preset_ids.clear();
    for (const auto& [kind, presets] : m_presets)
        for (const auto& p : presets)
            collect_preset_ids(kind, p, {&p});
}

void PresetEvaluator::collect_preset_ids(
    PresetKind kind,
    const PresetNode& node,
    const PresetNodePath& node_path
)
{
    const auto& id = node.id.empty() && node.name.has_value() ? node.name.value() : node.id;
    if (!id.empty()) {
        std::string name;
        m_preset_ids[kind].emplace(std::make_pair(id, node_path));
    }

    for (const auto& v : node.variants) {
        PresetNodePath child_path = node_path;
        child_path.push_back(&v);
        collect_preset_ids(kind, v, child_path);
    }
}

const Domain::Preset::PresetNode* PresetEvaluator::find_node(PresetKind kind, std::string_view name) const
{
    auto presets_it = m_presets.find(kind);
    if (presets_it == m_presets.end())
        return nullptr;
    const auto& presets = presets_it->second;
    auto it             = std::find_if(
        presets.begin(),
        presets.end(),
        [&name](const auto& preset) { return preset.name == name; }
    );
    if (it == presets.end())
        return nullptr;
    return &*it;
}

void PresetEvaluator::fill_missing_tool_overrides_from_print(
    Domain::Preset::EvaluatedToolPrintPreset& tool_print_preset,
    const Domain::ConfigBox& print_config_box,
    const HwPrinterConfig& hw_config
)
{
    if (Domain::Preset::get_feature<bool>(hw_config.features, "multi_extruder").value_or(false)) {
        Domain::ConfigOverrides& tool_overrides = tool_print_preset.preset.config_box().overrides;
        for (Domain::ConfigItem& override_item : tool_overrides.all_items()) {
            const std::string& name = override_item.name();
            if (!tool_overrides.get(name).has_value()) {
                // ToolOverride is not set
                const Domain::ConfigItem* print_item = print_config_box.items.find(name);
                ASSERT(print_item);
                // Set it with Print value
                tool_overrides.set(name, print_item->value());
            }
        }
    }
}

PresetEvaluator::EvaluatedPrinterPresets PresetEvaluator::evaluate(const HwPrinterConfig& hw_config, bool use_material_cache) const
{
    Expr::ValueMap printer_values;
    append_printer_values(printer_values, hw_config);

    ValueMaps printer_tools_values;
    for (const auto& tool : hw_config.tools) {
        Expr::ValueMap tool_values = printer_values;

        append_tool_values(tool_values, tool);
        printer_tools_values.emplace_back(tool_values);
    }

    // 1. Printer preset
    PresetKind printer_kind = Domain::Preset::printer_kind(hw_config.technology);
    auto printers_it        = m_presets.find(printer_kind);
    auto printer_names_it   = m_preset_ids.find(printer_kind);
    ASSERT(printers_it != m_presets.end() && printer_names_it != m_preset_ids.end());

    PresetCollectionEvaluator printer_eval(printers_it->second, printer_names_it->second, m_eval, {}, hw_config.name);
    auto printer_presets = printer_eval.eval_preset({printer_tools_values});
    if (printer_presets.empty())
        SPDLOG_ERROR(
            "No printer presets available for configuration {} ({}) referring to printer {}",
            hw_config.name,
            hw_config.id,
            hw_config.printer_id
        );

    EvaluatedPrinterPresets ret;

    for (const auto& printer_preset : printer_presets) {
        EvaluatedPrinterPreset ep{.hw_config = hw_config};
        ep.preset = preset_from_context<Domain::PrinterSettings, Domain::SLAPrinterSettings>(
            hw_config,
            printer_kind,
            printer_preset
        );
        update_printer_preset_from_hw_config(hw_config, ep.preset.config_box());

        // 2. Print preset
        PresetKind print_kind = Domain::Preset::print_kind(hw_config.technology);
        auto prints_it        = m_presets.find(print_kind);
        auto print_names_it   = m_preset_ids.find(print_kind);
        ASSERT(prints_it != m_presets.end() && print_names_it != m_preset_ids.end());

        PresetCollectionEvaluator
            print_eval(prints_it->second, print_names_it->second, m_eval, {}, hw_config.name);
        auto print_presets = print_eval.eval_preset(printer_tools_values);

        // 3. Tool print presets
        // for each tool
        PresetKind tool_kind = Domain::Preset::tool_print_kind(hw_config.technology);
        auto tool_it         = m_presets.find(tool_kind);
        auto tool_names_it   = m_preset_ids.find(tool_kind);
        ASSERT(hw_config.technology == Domain::PrinterTechnology::SLA || (tool_it != m_presets.end() && tool_names_it != m_preset_ids.end()));

        for (const auto& print_preset : print_presets) {
            auto evaluated_print_preset = preset_from_context<Domain::PrintSettings, Domain::SLAPrintSettings>(
                hw_config,
                print_kind,
                print_preset
            );
            Domain::Preset::AllToolsEvaluatedToolPrintPresets tools;

            Expr::ValueMap print_values = printer_values;
            std::visit([&print_values](const auto& v) {
                append_print_values(print_values, v);
            }, evaluated_print_preset.values);

            if (hw_config.technology == Domain::PrinterTechnology::FFF) {
                PresetCollectionEvaluator
                    tool_eval(tool_it->second, tool_names_it->second, m_eval, {}, hw_config.name);
                for (const auto& tool : hw_config.tools) {
                    Expr::ValueMap tool_values = print_values;
                    append_tool_values(tool_values, tool);

                    // evaluate all variants
                    Domain::Preset::SingleToolEvaluatedToolPrintPresets eval_variants;
                    EvalPresetContexts tool_preset_variants = tool_eval.eval_preset({tool_values});
                    for (const EvalPresetContext& tool_variant : tool_preset_variants) {
                        Domain::Preset::EvaluatedToolPrintPreset evaluated_tool_preset{
                            preset_from_context<Domain::ToolPrintSettings, std::monostate>(
                                hw_config,
                                tool_kind,
                                tool_variant
                            )
                        };
                        fill_missing_tool_overrides_from_print(
                            evaluated_tool_preset,
                            evaluated_print_preset.config_box(),
                            hw_config
                        );
                        eval_variants.push_back(std::move(evaluated_tool_preset));
                    }
                    tools.emplace_back(std::move(eval_variants));
                }
            }

            // 4. Material
            PresetKind mat_kind = Domain::Preset::material_kind(hw_config.technology);
            auto mats_it        = m_presets.find(mat_kind);
            auto mat_names_it   = m_preset_ids.find(mat_kind);
            ASSERT(mats_it != m_presets.end() && mat_names_it != m_preset_ids.end());

            Domain::Preset::AllToolsEvaluatedMaterialPresets materials;

            using MaterialCache =
                std::map<std::string, Domain::Preset::SingleToolEvaluatedMaterialPresets>;
            MaterialCache material_cache;
            for (const auto& it : Domain::Preset::MaterialIterator{hw_config}) {
                Expr::ValueMap tool_values = print_values;
                const auto& tool = it.tool_config();
                append_tool_values(tool_values, tool);

                // TODO: feeder settings?
                // const auto feeder_count = it.feeder_count();
                // if (feeder_count > 0) {
                //     const auto& feeder = it.feeder_config(feeder_count - 1);
                //     append_feeder_values(feeder_values, feeder);
                // }

                Domain::Preset::SingleToolEvaluatedMaterialPresets variants;
                // Cache key is tool.id because tool_values only depends on
                // print_values (shared) + tool.features (same for same tool.id).
                // If feeder values are added to tool_values, the cache key must
                // include the feeder address as well.
                if (use_material_cache) {
                    if (const auto cache_it = material_cache.find(tool.id);
                        cache_it != material_cache.end())
                    {
                        variants = cache_it->second;
                        materials.emplace_back(std::move(variants));
                        continue;
                    }
                }

                PresetCollectionEvaluator material_eval(
                    mats_it->second,
                    mat_names_it->second,
                    m_eval,
                    {},
                    hw_config.name
                );
                auto mat_presets = material_eval.eval_preset({tool_values});
                for (const auto& mat : mat_presets) {
                    variants.emplace_back(
                        preset_from_context<Domain::FilamentSettings, Domain::SLAMaterialSettings>(
                            hw_config,
                            mat_kind,
                            mat
                        )
                    );
                }
                if (use_material_cache) {
                    material_cache[tool.id] = variants;
                }
                materials.emplace_back(std::move(variants));
            }

            // For FFF add only prints with tools filled
            const bool drop_prints_wo_tools =
                hw_config.technology != Domain::PrinterTechnology::FFF || hw_config.tool_count > 1;
            if (!drop_prints_wo_tools
                || std::ranges::all_of(tools, [](const auto& t) { return !t.empty(); }))
            {
                ep.prints.emplace_back(
                    std::move(evaluated_print_preset),
                    std::move(tools),
                    std::move(materials)
                );
            } else if (drop_prints_wo_tools) {
                SPDLOG_WARN(
                    "Print preset {} for printer {} was removed as it has at least one tool without tool print presets",
                    evaluated_print_preset.name,
                    printer_preset.name
                );
            }
        }

        // deduplicate print presets
        std::map<
            std::string,
            std::vector<std::reference_wrapper<Domain::Preset::EvaluatedPrintPreset>>>
            presets_to_deduplicate;
        Domain::Preset::EvaluatedPrintPresets deduplicated_presets;

        for (auto& print : ep.prints) {
            presets_to_deduplicate[print.preset.name].push_back(std::ref(print));
        }

        const bool needs_deduplication = std::ranges::any_of(
            presets_to_deduplicate | std::views::values,
            [](const auto& ps) { return ps.size() > 1; }
        );
        if (needs_deduplication) {
            //for (auto& [name, presets]: presets_to_deduplicate) {
            for (auto& prints_to_dedup : presets_to_deduplicate | std::views::values) {
                auto it = prints_to_dedup.begin();
                Domain::Preset::EvaluatedPrintPreset& print = it->get();
                std::for_each(++it, prints_to_dedup.end(), [&print](const auto& p)
                {
                    auto preset = p.get();
                    // assert that all preset values for same
                    DEBUG_ASSERT(print.preset.values == preset.preset.values);
                    // assert that all tool print presets has same size
                    ASSERT(print.tools.size() == preset.tools.size());
                    for (size_t i = 0; i < print.tools.size(); ++i) {
                        ASSERT(print.tools[i].size() == preset.tools[i].size());
                    }
                });

                deduplicated_presets.emplace_back(std::move(print));
            }
            ep.prints = std::move(deduplicated_presets);
        }

        ret.emplace_back(std::move(ep));
    }
    return ret;
}

} // namespace Slic3r::Biz::Preset
