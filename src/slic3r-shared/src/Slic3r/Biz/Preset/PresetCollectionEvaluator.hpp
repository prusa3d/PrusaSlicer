#pragma once

#include "Slic3r/Log.hpp"

#include "Slic3r/Biz/Preset/PresetEvaluator.hpp"
#include "Slic3r/Biz/Expr/Eval.hpp"

#define DEBUG_CONDITION_EVAL 0

namespace Slic3r::Biz::Preset {

using ValueMaps = std::vector<Expr::ValueMap>;
enum class ExprCombine
{
    Or, And
};

enum class EvalMode
{
    Downstream,
    NodeOnly
};


class PresetCollectionEvaluator {
public:
    PresetCollectionEvaluator(
        const Domain::Preset::Presets& presets,
        const PresetEvaluator::IdentifiedPresets& named_presets,
        Expr::Eval eval,
        const Expr::ValueMap& overrides,
        const std::string& debug_name
    );

    ~PresetCollectionEvaluator();
    PresetEvaluator::EvalPresetContexts eval_preset(const ValueMaps& overrides, bool only_public = true, ExprCombine expr_combine = ExprCombine::Or) const;

private:
    PresetEvaluator::EvalPresetContexts eval_preset(
        const Domain::Preset::PresetNode& node,
        const std::string& root_id,
        Domain::Preset::PresetOrigin origin,
        std::optional<std::string> user_file,
        const PresetEvaluator::EvalPresetContexts& parent_contexts,
        const ValueMaps& overrides,
        ExprCombine expr_combine = ExprCombine::Or,
        EvalMode mode = EvalMode::Downstream,
        bool skip_condition_eval = false
    ) const;
    bool eval_condition(const Expr::ValueMap& overrides, const Domain::Preset::SourceLocatedExpr& expr) const;
    bool eval_condition(const ValueMaps& overrides, ExprCombine expr_combine, const Domain::Preset::SourceLocatedExpr& expr) const;

    const PresetEvaluator::PresetNodePath& named_preset(const std::string& id) const;

private:
    const Domain::Preset::Presets& m_presets;
    const PresetEvaluator::IdentifiedPresets& m_named_presets;
    Expr::Eval m_eval;

#if DEBUG_CONDITION_EVAL
    std::shared_ptr<spdlog::logger> m_logger;
#endif
};

}

