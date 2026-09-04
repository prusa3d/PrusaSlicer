#include "Slic3r/Biz/Preset/PresetCollectionEvaluator.hpp"
#include <nlohmann/json.hpp>
#include <spdlog/sinks/basic_file_sink.h>
#include "Slic3r/Biz/Preset/JsonUtils.hpp"


// Xcode 15 has not finished ranges support we need here
#include <version>
#include <magic_enum/magic_enum.hpp>
#if !defined(__cpp_lib_ranges) \
    || __cpp_lib_ranges < 201'911L \
    || (__clang_major__ < 16 && defined(__apple_build_version__))
#define HAS_RANGES_VIEWS 0
#else
#define HAS_RANGES_VIEWS 1
#include <ranges>
#endif

#ifdef WIN32
#include <boost/nowide/convert.hpp>
#endif

#include <fmt/format.h>

#include <Slic3r/Log.hpp>

namespace Slic3r::Biz::Preset {

namespace {
using namespace Domain::Preset;

template<typename DestMap, typename SrcMap>
void override_preset_values(DestMap& dest, const SrcMap& overrides)
{
    for (const auto& [key, value] : overrides) {
        if (key.starts_with("custom_parameters_")) {
            auto it = dest.find(key);
            ASSERT(std::holds_alternative<std::string>(value));
            if (it != dest.end()) {
                ASSERT(std::holds_alternative<std::string>(it->second));
                dest[key] = merge_json(std::get<std::string>(it->second), std::get<std::string>(value));
                continue;
            }
        }
        dest[key] = value;
    }
}

void override_feature_values(FeatureValueMap& dest, const FeatureValueMap& overrides)
{
    for (const auto& [key, value] : overrides)
        dest[key] = value;
}

}

PresetCollectionEvaluator::PresetCollectionEvaluator(
    const Domain::Preset::Presets& presets,
    const PresetEvaluator::IdentifiedPresets& named_presets,
    Expr::Eval eval,
    const Expr::ValueMap& overrides,
    const std::string& debug_name
) :
    m_presets(presets),
    m_named_presets(named_presets),
    m_eval(std::move(eval))
{
    m_eval.set_vars(overrides);
#if DEBUG_CONDITION_EVAL
    std::string kind_name;
    if (!presets.empty()) {
        kind_name = magic_enum::enum_name(presets.front().kind);
    }
    auto log_path =
#ifdef WIN32
        boost::nowide::widen(
#endif
            fmt::format("preset-eval-{}-{}.log", kind_name, debug_name)
#ifdef WIN32
        )
#endif
    ;
    auto file_sink = std::make_shared<spdlog::sinks::basic_file_sink_st>(log_path, true);
    std::vector<spdlog::sink_ptr> sinks{file_sink};
    m_logger = std::make_shared<spdlog::logger>("eval", sinks.begin(), sinks.end());

    m_eval.set_debug_output([&logger = this->m_logger](std::string_view message)
                        { logger->info(message); });
#endif
}

#if DEBUG_CONDITION_EVAL
PresetCollectionEvaluator::~PresetCollectionEvaluator()
{
    m_eval.set_debug_output(nullptr);
}
#else
PresetCollectionEvaluator::~PresetCollectionEvaluator() = default;
#endif

#if DEBUG_CONDITION_EVAL
#define EVAL_LOG(...) m_logger->info(__VA_ARGS__);
#else
#define EVAL_LOG(...)
#endif //DEBUG_CONDITION_EVAL


PresetEvaluator::EvalPresetContexts PresetCollectionEvaluator::eval_preset(
    const ValueMaps& overrides,
    bool only_public,
    ExprCombine expr_combine
) const
{
#if !HAS_RANGES_VIEWS
    PresetEvaluator::EvalPresetContexts ret;
    for (const auto& preset : m_presets) {
        auto eval_presets = eval_preset(preset, preset.id, preset.origin, {{preset.id}}, overrides);
        auto it           = only_public ?
                      std::remove_if(
                eval_presets.begin(),
                eval_presets.end(),
                [](const auto& ep) { return !Domain::Preset::is_public_name(ep.name); }
            ) :
                      eval_presets.end();

        ret.insert(ret.end(), std::make_move_iterator(eval_presets.begin()), std::make_move_iterator(it));
    }
    return ret;
#else
    auto joined_view =
        m_presets
        | std::views::transform(
            [&](const auto& preset)
            {
                auto pred = [only_public
#if DEBUG_CONDITION_EVAL
                             ,
                             this
#endif
                ](const auto& ep)
                {
                    const bool included = !only_public || Domain::Preset::is_public_name(ep.name);
#if DEBUG_CONDITION_EVAL
                    if (!included) {
                        EVAL_LOG("{} ({}) removed as not being public", ep.id, ep.name);
                    }
#endif
                    return included;
                };

                return eval_preset(
                           preset,
                           preset.id,
                           preset.origin,
                           preset.user_file,
                           {{
                             .origin    = preset.origin,
                             .root_id   = preset.id,
                             .user_file = preset.user_file
                           }},
                           overrides.empty() ? ValueMaps{{}} : overrides,
                           expr_combine
                       )
                    | std::views::filter(pred);
            }
        )
        | std::views::join;


    PresetEvaluator::EvalPresetContexts ret;
    for (auto&& ep : joined_view)
        ret.push_back(std::move(ep));
    EVAL_LOG("Final presets returned: {}", ret.size());
    std::ranges::sort(
        ret,
        [](const auto& x, const auto& y)
        { return x.name < y.name || (x.name == y.name && x.id < y.id); }
    );

    return ret;
#endif
}

PresetEvaluator::EvalPresetContexts PresetCollectionEvaluator::eval_preset(
    const Domain::Preset::PresetNode& node,
    const std::string& root_id,
    PresetOrigin origin,
    std::optional<std::string> user_file,
    const PresetEvaluator::EvalPresetContexts& parent_contexts,
    const ValueMaps& overrides,
    ExprCombine expr_combine,
    EvalMode mode,
    bool skip_condition_eval
) const
{
    ASSERT(!parent_contexts.empty());

    if (!skip_condition_eval
        && node.condition.has_value()
        && !eval_condition(overrides, expr_combine, node.condition.value().expr))
        return {};

    PresetEvaluator::EvalPresetContexts ret = parent_contexts;

    if (!node.inherits.empty()) {
        for (const auto& inh : node.inherits) {
            const auto& node_path = named_preset(inh);
            ASSERT(node_path.size() == 1);

            // if this node has condition, it overrides the superclass condition
            // (and hence we want to skip its evaluation)
            const bool skip_superclass_condition_eval =
                skip_condition_eval || node.condition.has_value();
            ret = eval_preset(
                *node_path.front(),
                root_id,
                origin,
                user_file,
                ret,
                overrides,
                expr_combine,
                EvalMode::Downstream,
                skip_superclass_condition_eval
            );

            if (ret.empty()) {
                EVAL_LOG(
                    "Inherited node {} root condition fails => quitting evaluation of {}",
                    inh,
                    node.source_location.to_string()
                );
                return {};
            };
        }
    }

    Domain::Preset::PresetValueMap unconditional_inherited_values;
    Domain::Preset::FeatureValueMap unconditional_inherited_features;
    for (const auto& unc_inh : node.unconditional_inherits) {
        const auto& node_path = named_preset(unc_inh);

        for (const auto& n : node_path) {
            auto node_ctxs = eval_preset(
                *n,
                root_id,
                origin,
                user_file,
                ret,
                overrides,
                expr_combine,
                EvalMode::NodeOnly,
                true
            );

            ASSERT(node_ctxs.size() == 1);
            const auto& ctx = node_ctxs.front();
            override_preset_values(unconditional_inherited_values, ctx.values);
            override_feature_values(unconditional_inherited_features, ctx.features);
        }
    }

    for (auto& context : ret) {
        if (!node.id.empty())
            context.id = Domain::Preset::derive_name(node.id, context.id);
        if (node.name.has_value())
            context.name = Domain::Preset::derive_name(node.name.value(), context.name);
        if (node.match_mode.has_value())
            context.match_mode = node.match_mode.value();
        if (node.name.has_value() && node.id.empty()) {
            context.id = Domain::Preset::derive_name(node.name.value(), context.id);
            SPDLOG_WARN(
                "{}: Preset node has defined name ({}) but not id! "
                "The id is derived from the name ({}) and MAY NOT BE UNIQUE.",
                node.source_location.to_string(),
                node.name.value(),
                context.id
             );
        }
        if (node.condition.has_value() && !skip_condition_eval) {
            // append condition if present and not ignored (because overridden in subclass)
            context.conditions.push_back(&node.condition.value().expr_str);
        }
        context.last_node_location = node.source_location;
        override_preset_values(context.values, unconditional_inherited_values);
        override_preset_values(context.values, node.values);
        override_feature_values(context.features, unconditional_inherited_features);
        override_feature_values(context.features, node.features);
    }

    if (mode != EvalMode::Downstream) {
        return PresetEvaluator::merged_same_presets(ret);
    }

    size_t conditional_variants   = 0;
    size_t unconditional_variants = 0;

    PresetEvaluator::EvalPresetContexts var_contexts;

    const bool first_match_only = std::ranges::all_of(
        ret,
        [](const auto& ctx)
        { return *ctx.match_mode == Domain::Preset::ConditionMatchMode::FirstMatch; }
    );

    // make sure that the match_mode is same for contexts
    ASSERT(
        first_match_only
        || std::ranges::none_of(
            ret,
            [](const auto& ctx)
            { return *ctx.match_mode == Domain::Preset::ConditionMatchMode::FirstMatch; }
        )
    );

    PresetEvaluator::StringPtrs conditions, negative_conditions;
    bool any_variant_visited = false;

    for (const auto& var : node.variants) {
        const bool conditional = var.condition.has_value();
        (conditional ? conditional_variants : unconditional_variants)++;
        if (conditional) {
            // There no unconditional variants allowed prior condition
            ASSERT(unconditional_variants == 0);

            // Condition is not met, continue with next variant
            if (!eval_condition(overrides, expr_combine, var.condition.value().expr)) {
                negative_conditions.push_back(&var.condition.value().expr_str);
                continue;
            }

            conditions.push_back(&var.condition.value().expr_str);

        } else if (conditional_variants > 0) {
            // if there was at least one condition case met before
            // only one unconditional variant is valid
            ASSERT(unconditional_variants == 1);
        }

        any_variant_visited = true;

        auto var_ctx = eval_preset(
            var,
            root_id,
            origin,
            user_file,
            {
                {.origin     = origin,
                 .root_id    = root_id,
                 .match_mode = first_match_only ? ConditionMatchMode::FirstMatch :
                                                  ConditionMatchMode::AllMatches,
                 .user_file  = user_file},

            },
            overrides,
            expr_combine,
            EvalMode::Downstream,
            true
        );
        for (auto& v : var_ctx) {
            v.conditions.insert(
                v.conditions.end(),
                conditions.begin(),
                conditions.end()
            );
            v.negative_conditions.insert(
                v.negative_conditions.end(),
                negative_conditions.begin(),
                negative_conditions.end()
            );
        }
        var_contexts.insert(
            var_contexts.end(),
            std::make_move_iterator(var_ctx.begin()),
            std::make_move_iterator(var_ctx.end())
        );

        // first successful condition met, break (switch-like statement behavior)
        if (conditional && first_match_only)
            break;
    }

    if (var_contexts.empty()) {
        ASSERT(!any_variant_visited);
        for (auto& ctx : ret) {
            ctx.negative_conditions.insert(
                ctx.negative_conditions.end(),
                negative_conditions.begin(),
                negative_conditions.end()
            );
        }
        return PresetEvaluator::merged_same_presets(std::move(ret));
    }

    // resolve variants
    PresetEvaluator::EvalPresetContexts product;
    product.reserve(ret.size() * var_contexts.size());
    for (size_t ci = 0; ci < ret.size(); ++ci) {
        for (size_t vi = 0; vi < var_contexts.size(); ++vi) {
            const auto& var_ctx = var_contexts[vi];
            const bool last_var = (vi + 1 == var_contexts.size());
            PresetEvaluator::EvalPresetContext context =
                last_var ? std::move(ret[ci]) : ret[ci];
            if (!var_ctx.id.empty())
                context.id   = Domain::Preset::derive_name(var_ctx.id, context.id);
            if (!var_ctx.name.empty())
                context.name = Domain::Preset::derive_name(var_ctx.name, context.name);
            if (var_ctx.match_mode.has_value())
                context.match_mode = var_ctx.match_mode;
            context.conditions.insert(
                context.conditions.end(),
                var_ctx.conditions.begin(),
                var_ctx.conditions.end()
            );
            context.negative_conditions.insert(
                context.negative_conditions.end(),
                var_ctx.negative_conditions.begin(),
                var_ctx.negative_conditions.end()
            );
            context.last_node_location = var_ctx.last_node_location;
            override_preset_values(context.values, var_ctx.values);
            override_feature_values(context.features, var_ctx.features);

            product.emplace_back(std::move(context));
        }
    }

    return PresetEvaluator::merged_same_presets(std::move(product));
}

struct BoolCaster
{
    bool operator()(const std::string&) const
    {
        return false;
    }

    bool operator()(const Domain::Expr::RegEx&) const
    {
        return false;
    }

    bool operator()(const auto& v) const
    {
        return static_cast<bool>(v);
    }
};

bool PresetCollectionEvaluator::eval_condition(
    const Expr::ValueMap& overrides,
    const Domain::Preset::SourceLocatedExpr& expr
) const
{
    try {
        auto result = m_eval.eval(*expr, overrides);
        return boost::apply_visitor(BoolCaster{}, result);
    } catch (Expr::EvalError& e) {
        auto msg = fmt::format("[{}] {}", expr.source_location.to_string(), e.what());
#if DEBUG_CONDITION_EVAL
        m_logger->error(msg);
#endif
        SPDLOG_ERROR("Condition eval failed: {}", msg);
        throw Expr::EvalError(msg);
    }
}

bool PresetCollectionEvaluator::eval_condition(
    const ValueMaps& overrides,
    ExprCombine expr_combine,
    const Domain::Preset::SourceLocatedExpr& expr
) const
{
    EVAL_LOG(
        "Evaluating expression defined in {}",
        expr.source_location.to_string(),
        Domain::Expr::to_string(expr.value)
    );

    for (const auto& var : overrides) {
        const bool val = eval_condition(var, expr);

        EVAL_LOG("expression result: {}", val);

        if (val && expr_combine == ExprCombine::Or) {
            EVAL_LOG("Final result: True (or combination)");

            return true;
        }
        if (!val && expr_combine == ExprCombine::And) {
            EVAL_LOG("Final result: False (and combination)");

            return false;
        }
    }

    EVAL_LOG(
        "Final result: {} ({} combination)",
        expr_combine == ExprCombine::And,
        expr_combine == ExprCombine::Or ? "or" : "and"
    );

    return expr_combine == ExprCombine::And;
}

const PresetEvaluator::PresetNodePath& PresetCollectionEvaluator::named_preset(const std::string& id) const
{
    auto it = m_named_presets.find(id);
    ASSERT(it != m_named_presets.end());
    return it->second;
}

} // namespace Slic3r::Biz::Preset
