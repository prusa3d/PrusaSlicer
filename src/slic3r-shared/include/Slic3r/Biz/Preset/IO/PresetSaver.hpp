#pragma once

#include <ranges>

#include "Slic3r/Domain/Preset/EvaluatedPreset.hpp"
#include "Slic3r/Biz/Expr/Simplify.hpp"

#include "boost/filesystem/path.hpp"

namespace Slic3r::Biz::Preset::IO {

constexpr const char* FEATURE_BASED_ID = "based_id";
constexpr const char* FEATURE_BASED_ROOT_ID = "based_root_id";
using KeySet = std::set<std::string>;

namespace Details {

Domain::Expr::ExprAst and_chain_exprs(const std::vector<std::string>& exprs);
Domain::Preset::PresetValueMap
config_box_to_values(const Domain::ConfigBox& cfg, const KeySet& items_to_omit);


} // namespace Details

struct BundlePaths;

template <typename FdmConfig, typename SlaConfig>
Domain::Preset::RootPresetNode transform_for_saving(
    const Domain::Preset::EvaluatedPreset<FdmConfig, SlaConfig>& source,
    const Domain::Preset::EvaluatedPreset<FdmConfig, SlaConfig>* system,
    const KeySet& items_to_omit
)
{
    using namespace Domain::Preset;
    RootPresetNode ret;

    ret.kind = source.kind;
    ret.origin = PresetOrigin::User;
    ret.id = source.root_id;
    ret.user_file = source.user_file;

    PresetNode main;
    auto it = source.features.find(FEATURE_BASED_ID);
    if (it != source.features.end() && system != nullptr) {
        main.unconditional_inherits = {std::get<std::string>(it->second)};
    }

    main.condition = Domain::Preset::ParsedExpr{
        SourceLocatedExpr{Expr::simplify(Details::and_chain_exprs(source.conditions))}
    };
    main.condition.value().expr_str = Domain::Expr::to_string(*main.condition.value().expr);
    main.id = source.id;
    main.name = source.name;
    main.features = source.features;

    std::vector<std::string> diff_keys;
    const auto& source_cb = source.config_box();
    if (system != nullptr) {
        const auto& system_cb = system->config_box();
        diff_keys = source_cb.diff_keys(system_cb);
    } else {
        auto append_keys = [&diff_keys]<typename T>(const T& items) {
            std::ranges::copy(
                items.all_items()
                    | std::views::transform(
                        [](const auto& it) -> const auto& { return it.name(); }
                    ),
                std::back_inserter(diff_keys)
            );
        };
        append_keys(source_cb.items);
        append_keys(source_cb.overrides);
    }

    KeySet items_to_include;
    auto not_omitted = [&items_to_omit](const auto& key) { return !items_to_omit.contains(key); };
    std::ranges::copy(
        diff_keys | std::views::filter(not_omitted),
        std::inserter(items_to_include, items_to_include.begin())
    );

    main.values = Details::config_box_to_values(source.config_box(), items_to_include);

    ret.variants.emplace_back(main);

    return ret;
}

void save_transformed_preset_as_user(
    const Domain::Preset::RootPresetNode& root_preset,
    const std::string& path
);


template <typename FdmConfig, typename SlaConfig>
void save_evaluated_preset_as_user(
    const Domain::Preset::EvaluatedPreset<FdmConfig, SlaConfig>& preset,
    const std::string& path,
    const KeySet& items_to_omit
)
{
    save_transformed_preset_as_user(transform_for_saving(preset, items_to_omit), path);
}

std::string preset_file_prefix(Domain::Preset::PresetKind kind);

boost::filesystem::path preset_path(
    const BundlePaths& paths,
    Domain::Preset::PresetKind kind,
    const std::string& preset_name,
    const std::string& vendor_id,
    const std::string& repo_id
);

} // namespace Slic3r::Biz::Preset::IO

