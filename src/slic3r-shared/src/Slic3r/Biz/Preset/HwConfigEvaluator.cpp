#include "Slic3r/Biz/Preset/HwConfigEvaluator.hpp"
#include "Slic3r/Biz/Preset/ValueMapBuilder.hpp"
#include "Slic3r/Uuid.hpp"

namespace Slic3r::Biz::Preset {

HwToolConfigIterator HwConfigEvaluator::iterate_tools(
    const Domain::Preset::HwPrinterConfig& printer,
    const HwToolConfigIterator::Container& tools
) const
{
    Expr::ValueMap values;
    append_printer_values(values, printer);
    return HwToolConfigIterator{tools, m_eval, std::move(values)};
}

HwFeederConfigIterator HwConfigEvaluator::iterate_feeders(
    const Domain::Preset::HwPrinterConfig& printer,
    const Domain::Preset::HwToolConfig& tool,
    const HwFeederConfigIterator::Container& feeders
) const
{
    Expr::ValueMap values;
    append_printer_values(values, printer);
    append_tool_values(values, tool);
    return HwFeederConfigIterator{feeders, m_eval, std::move(values)};
}

HwSheetConfigIterator HwConfigEvaluator::iterate_sheets(
    const Domain::Preset::HwPrinterConfig& printer,
    const HwSheetConfigIterator::Container& sheets
) const
{
    Expr::ValueMap values;
    append_printer_values(values, printer);
    return HwSheetConfigIterator{sheets, m_eval, std::move(values)};
}

Domain::Preset::HwPrinterConfig from_def(
    const Domain::Preset::VendorData& vendor_data,
    const Domain::Preset::HwPrinterConfigDef& printer_def,
    const Domain::Preset::HwPrinterConfigTemplate* templ
)
{
    auto features = build_features(vendor_data.info.features.printer);
    Domain::Preset::override_features(features, printer_def.features);
    if (templ)
        Domain::Preset::override_features(features, templ->features);

    Domain::Preset::VisualRepresentation visual = printer_def.visual;
    if (templ)
        Domain::Preset::override_visual(visual, templ->visual);

    Domain::Preset::HwPrinterConfig printer_config = {
        .id           = generate_uuid(),
        .printer_id   = printer_def.id,
        .vendor_id    = vendor_data.info.id,
        .repo_id      = vendor_data.info.repo_id,
        .repo_version = vendor_data.info.version,
        .name         = templ == nullptr || templ->name.empty() ? printer_def.name : templ->name,
        .short_name   = templ == nullptr || templ->name.empty() ? printer_def.name : templ->name,
        .technology   = printer_def.technology,
        .model        = printer_def.model,
        .tool_count =
            templ && templ->tool_count.has_value() ? templ->tool_count.value() : printer_def.tool_count,
        .features = features,
        .visual   = visual
    };
    if (templ != nullptr && !templ->legacy_printer_model.empty()) {
        printer_config.legacy_printer_model = templ->legacy_printer_model.front();
    }
    else if (!printer_def.legacy_printer_model.empty()) {
        printer_config.legacy_printer_model = printer_def.legacy_printer_model.front();
    }

    return printer_config;
}

Domain::Preset::HwPrinterConfig HwConfigEvaluator::create_printer_config(
    const Domain::Preset::HwPrinterConfigTemplate& templ,
    const Domain::Preset::VendorData& vendor_data
) const
{
    const auto* printer_def = vendor_data.find_printer_config_def_by_id(templ.printer);
    ASSERT(printer_def != nullptr, "Printer config not found", templ.printer);

    Domain::Preset::HwPrinterConfig printer_config = from_def(vendor_data, *printer_def, &templ);

    ASSERT(templ.tools.size() == templ.tool_count || templ.tools.size() == 1, templ.id);
    for (const auto& tool_templ : templ.tools) {
        const auto* tool_def = vendor_data.find_tool_config_def_by_id(tool_templ.id);
        ASSERT(tool_def != nullptr, tool_templ.id);
        ASSERT(tool_def->technology == printer_def->technology, tool_templ.id);

        Domain::Preset::HwToolConfig tool_config = from_def(vendor_data, *tool_def, tool_templ.features);
        printer_config.tools.emplace_back(std::move(tool_config));
    }

    // If only single tool provided, fill the rest slots with same tool-config
    while (printer_config.tools.size() < printer_config.tool_count)
        printer_config.tools.push_back(printer_config.tools.front());

    for (const auto& feeder_templ : templ.feeders) {
        const auto* feeder_def = vendor_data.find_feeder_config_def_by_id(feeder_templ.id);
        ASSERT(feeder_def != nullptr, feeder_templ.id);
        ASSERT(feeder_def->technology == printer_def->technology, feeder_templ.id);
        auto feeder_features = build_features(vendor_data.info.features.feeder);
        Domain::Preset::override_features(feeder_features, feeder_def->features);
        Domain::Preset::override_features(feeder_features, feeder_templ.features);
        Domain::Preset::HwFeederConfig feeder_config{
            .id         = feeder_def->id,
            .type       = feeder_def->type,
            .model      = feeder_def->model,
            .slot_count = feeder_def->slot_count,
            .features   = feeder_features,
        };
        printer_config.feeders.emplace(std::make_pair(feeder_templ.address, std::move(feeder_config)));
    }

    if (printer_config.technology == Domain::PrinterTechnology::FFF) {
        const auto* sheet_def = templ.sheet.has_value() ?
            vendor_data.find_sheet_config_def_by_id(*templ.sheet) :
            first_compatible_sheet(
                printer_config,
                vendor_data.defs.find(Domain::PrinterTechnology::FFF)->second.sheets
            );
        ASSERT(sheet_def != nullptr, sheet_def->id);
        printer_config.sheet = from_def(vendor_data, *sheet_def);
    }

    printer_config.name       = Domain::Preset::suggest_name(printer_config, vendor_data, false);
    printer_config.short_name = Domain::Preset::suggest_name(printer_config, vendor_data, true);

    return printer_config;
}

void HwConfigEvaluator::set_debug_output(const Expr::Eval::Logger& logger)
{
    m_eval.set_debug_output(logger);
}

const Domain::Preset::HwSheetConfigDef* HwConfigEvaluator::first_compatible_sheet(
    const Domain::Preset::HwPrinterConfig& printer,
    const HwSheetConfigIterator::Container& sheets
) const
{
    auto it = iterate_sheets(printer, sheets);
    return it == std::end(it) ? nullptr : &*it;
}

Domain::Preset::HwToolConfig
from_def(const Domain::Preset::VendorData& vendor_data, const Domain::Preset::HwToolConfigDef& def, std::optional<Domain::Preset::FeatureValueMap> template_overrides)
{
    auto tool_features = build_features(vendor_data.info.features.tool);
    Domain::Preset::override_features(tool_features, def.features);
    if (template_overrides.has_value()) {
        Domain::Preset::override_features(tool_features, *template_overrides);
    }
    return Domain::Preset::HwToolConfig{
        .id       = def.id,
        .name     = def.name,
        .features = tool_features,
    };
}

Domain::Preset::HwSheetConfig
from_def(const Domain::Preset::VendorData& vendor_data, const Domain::Preset::HwSheetConfigDef& def)
{
    auto sheet_features = build_features(vendor_data.info.features.sheet);
    Domain::Preset::override_features(sheet_features, def.features);
    return Domain::Preset::HwSheetConfig{
        .id       = def.id,
        .name     = def.name,
        .type     = def.type,
        .features = sheet_features,
    };

}

Domain::Preset::HwPrinterConfig from_def(
    const Domain::Preset::VendorData& vendor_data,
    const Domain::Preset::HwPrinterConfigDef& printer_def
)
{
    auto config = from_def(vendor_data, printer_def, nullptr);
    if (config.technology == Domain::PrinterTechnology::FFF) {
        config.tools.resize(config.tool_count, {});
    }
    if (!printer_def.legacy_printer_model.empty()) {
        config.legacy_printer_model = printer_def.legacy_printer_model.front();
    }
    return config;
}

Domain::Preset::HwPrinterConfig
new_scratch_config(Domain::PrinterTechnology technology, const std::string& name, size_t tool_count)
{
    Domain::Preset::HwPrinterConfig config = {
        .id           = generate_uuid(),
        // .printer_id   = printer_def.id,
        // .vendor_id    = vendor_data.info.id,
        // .repo_id      = vendor_data.info.repo_id,
        // .repo_version = vendor_data.info.version,
        .name         = name,
        .technology   = technology,
        //.model        = printer_def.model,
        .tool_count = static_cast<uint8_t>(tool_count),
        .features = {},
        //.visual   = visual
    };

    if (config.technology == Domain::PrinterTechnology::FFF) {
        config.tools.resize(config.tool_count, {});
    }

    return config;
}
} // namespace Slic3r::Biz::Preset
