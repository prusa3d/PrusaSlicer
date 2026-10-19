#include <ranges>
#include "Slic3r/Biz/Preset/HwBundleEvaluator.hpp"
#include "Slic3r/Assert.hpp"

namespace Slic3r::Biz::Preset {

namespace {

struct VendorId
{
    std::string repo_id;
    std::string vendor_id;

    bool valid() const
    {
        return !repo_id.empty() && !vendor_id.empty();
    }
};

std::optional<VendorId> find_vendor_for_printer(
    const Domain::Preset::HwPrinterConfig& external_config,
    const Domain::Preset::Bundle& preset_bundle
)
{
    auto range = preset_bundle.vendor_bundles | std::views::values;
    auto it = std::ranges::find_if(
        range,
        [&external_config](const auto& vendor_bundle)
        {
            return std::ranges::any_of(
                vendor_bundle.vendor_data.defs
                    | std::views::values
                    | std::views::transform([](const auto& p) -> const auto& { return p.printers; })
                    | std::views::join
                    | std::views::values,
                [&external_config](const auto& printer)
                { return printer.model == external_config.model; }
            );
        }
    );
    if (it != std::ranges::end(range)) {
        const auto& vendor_info = (*it).vendor_data.info;
        return VendorId{vendor_info.repo_id, vendor_info.id};
    }
    return std::nullopt;
}

size_t count_feature_match(const Domain::Preset::FeatureValueMap& f1,const Domain::Preset::FeatureDefs& f2)
{
    return std::ranges::count_if(
        f1,
        [&f2](const auto& fv)
        {
            auto it = f2.find(fv.first);
            return it != f2.end() && it->second.default_value == fv.second;
        }
    );
}

const Domain::Preset::HwToolConfigDef* find_tool_def(
    const Domain::Preset::HwPrinterConfig& hw_config,
    const Domain::Preset::HwToolConfig& external_tool,
    const Domain::Preset::VendorData& vendor_data
)
{
    HwConfigEvaluator eval;
    auto tool_it = eval.iterate_tools(hw_config, vendor_data.defs.at(hw_config.technology).tools);

    struct ToolRank
    {
        const Domain::Preset::HwToolConfigDef* tool_def{nullptr};
        size_t rank{0};
    };
    using ToolRanks = std::vector<ToolRank>;

    ToolRanks tool_ranks;
    for (const auto& tool: tool_it) {
        tool_ranks.emplace_back(&tool, count_feature_match(external_tool.features, tool.features));
    }

    auto it = std::ranges::max_element(tool_ranks, {}, [](const auto& tr) { return tr.rank; });
    if (it == tool_ranks.end()) {
        return nullptr;
    }

    return it->tool_def;
}

} // namespace

tl::expected<Domain::Preset::HwPrinterConfig, std::string>
HwBundleEvaluator::evaluate_config(const Domain::Preset::HwPrinterConfig& external_config) const
{
    VendorId vid{external_config.repo_id, external_config.vendor_id};
    if (!vid.valid()) {
        if (auto result = find_vendor_for_printer(external_config, m_preset_bundle); result.has_value()) {
            vid = result.value();
        } else {
            return tl::unexpected{fmt::format(
                "Cannot find vendor bundle for model: '{}'  base_model: '{}'",
                external_config.model.model,
                external_config.model.base_model
            )};
        }
    }

    const auto& vendor_bundle = m_preset_bundle.vendor_bundles.at(vid.vendor_id);
    const auto& vendor_data   = vendor_bundle.vendor_data;
    const auto& defs          = vendor_data.defs.at(external_config.technology);
    auto template_it          = std::ranges::find_if(
        vendor_data.printer_configs,
        [&](const auto& printer)
        {
            auto printer_def_it = defs.printers.find(printer.printer);
            ASSERT(printer_def_it != defs.printers.end());
            const auto& printer_def = printer_def_it->second;
            return printer_def.model == external_config.model
                && printer.tool_count.value_or(printer_def.tool_count)
                == external_config.tool_count
                // we are looking for template without feeder, as we need to
                // set it up anyway (if present in external hw config)
                && printer.feeders.empty();
        }
    );

    Domain::Preset::HwPrinterConfig hw_config;

    if (template_it != vendor_data.printer_configs.end()) {
        const auto& printer_template = *template_it;
        hw_config = HwConfigEvaluator{}.create_printer_config(printer_template, vendor_data);
    } else {
        auto printer_def_it = std::ranges::find_if(defs.printers, [&](const auto& printer)
        {
            return printer.second.model == external_config.model;
        });

        if (printer_def_it == defs.printers.end()) {
            return tl::unexpected{fmt::format(
                "Cannot find printer definition for model '{}' and base_model: '{}'",
                external_config.model.model,
                external_config.model.base_model
            )};
        }

        hw_config = from_def(vendor_data, printer_def_it->second);
    }

    if (hw_config.tool_count != external_config.tool_count) {
        hw_config.tool_count = external_config.tool_count;
        hw_config.tools.resize(hw_config.tool_count);
    }

    for (size_t i = 0; i < hw_config.tool_count; ++i) {
        auto* td = find_tool_def(hw_config, external_config.tools[i], vendor_data);
        if (td == nullptr) {
            return  tl::unexpected{fmt::format("Cannot find tool at slot {} for external tool", i)};
        }
        if (td->id == hw_config.tools[i].id) {
            continue;
        }
        hw_config.tools[i] = from_def(vendor_data, *td);
    }

    auto find_feeder = [&](const Domain::Preset::HwFeederConfig& external_feeder) -> const Domain::Preset::HwFeederConfigDef*
    {
        auto r  = defs.feeders | std::views::values;
        auto it = std::ranges::find_if(
            r,
            [&external_feeder](const Domain::Preset::HwFeederConfigDef& d) -> bool
            { return d.model == external_feeder.model; }
        );
        return (it == std::ranges::end(r)) ? nullptr : &*it;
    };

    for (const auto& [addr, feeder] : external_config.feeders) {
        const auto* feeder_def = find_feeder(feeder);
        if (feeder_def == nullptr) {
            return tl::unexpected{fmt::format(
                "Cannot find definition for feeder with model: {} and base_model: {}",
                feeder.model.model,
                feeder.model.base_model
            )};
        }
        hw_config.feeders[addr] = from_def(vendor_data, *feeder_def);
    }

    return hw_config;
}

} // namespace Slic3r::Biz::Preset
