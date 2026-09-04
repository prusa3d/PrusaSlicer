#include "Slic3r/Biz/Preset/IO/PresetMetadataLegacyLoader.hpp"
#include "Slic3r/Biz/Preset/HwConfigEvaluator.hpp"
#include "Slic3r/Uuid.hpp"

#include <ranges>
#include <string>

namespace Slic3r::Biz::Preset::IO {

Domain::Preset::EvaluatedPresetMetadata create_preset_metadata(const std::string& name)
{
    return Domain::Preset::EvaluatedPresetMetadata{
        .root_id    = generate_uuid(),
        .id         = generate_uuid(),
        .name       = name,
        .conditions = {}
    };
}

Domain::Preset::EvaluatedPresetMetadatas create_preset_metadatas(const std::string& name, size_t n)
{
    return Domain::Preset::EvaluatedPresetMetadatas{n, create_preset_metadata(name)};
}

Domain::Preset::EvaluatedPresetMetadatas create_preset_metadatas(const std::vector<std::string>& names)
{
    const size_t n = names.size();
    Domain::Preset::EvaluatedPresetMetadatas ret{n};
    for (size_t i = 0; i < n; ++i) {
        ret[i] = create_preset_metadata(names[i]);
    }
    return ret;
}

bool match_tool(
    const Domain::Preset::HwToolConfig& tool,
    const LegacyHwToolConfig& legacy_tool
)
{
    return (!legacy_tool.nozzle_diameter.has_value()
            || legacy_tool.nozzle_diameter.value()
                == std::get<double>(tool.features.at("nozzle_diameter")))
        && (!legacy_tool.nozzle_high_flow.has_value()
            || legacy_tool.nozzle_high_flow.value()
                == std::get<bool>(tool.features.at("nozzle_high_flow")));
}

tl::expected<Domain::Preset::SelectedPresetMetadata, std::string>
load_legacy_preset_metadata(const LegacyPresetMetadata& legacy_preset, const Domain::ConfigPack& config, const Domain::Preset::Bundle& preset_bundle)
{
    Domain::Preset::HwPrinterConfig hw_config{.id = generate_uuid(), .name = legacy_preset.printer_settings_id};
    const auto printer_model = legacy_preset.printer_model;

    if (printer_model.empty())
        return tl::unexpected{"Cannot find compatible printer HW config"};

    // Try to find template with given printer model name
    std::optional<std::tuple<
        std::reference_wrapper<const Domain::Preset::VendorBundle>,
        std::reference_wrapper<const Domain::Preset::HwPrinterConfigTemplate>>>
        printer_config;

    for (const auto& vendor_bundle : preset_bundle.vendor_bundles | std::views::values) {
        const auto* printer_config_template =
            vendor_bundle.vendor_data.find_printer_config_template_by_legacy_printer_model(
                printer_model
            );
        if (printer_config_template == nullptr)
            continue;

        printer_config = {std::cref(vendor_bundle), std::cref(*printer_config_template)};
        break;
    }

    std::optional<std::tuple<
        std::reference_wrapper<const Domain::Preset::VendorBundle>,
        std::reference_wrapper<const Domain::Preset::HwPrinterConfigDef>>>
        printer_def;

    HwConfigEvaluator hw_config_evaluator;
    if (printer_config.has_value()) {
        hw_config = hw_config_evaluator.create_printer_config(
            std::get<1>(printer_config.value()).get(),
            std::get<0>(printer_config.value()).get().vendor_data
        );
    } else {
        // Try to find bundle with printer config def

        for (const auto& vendor_bundle : preset_bundle.vendor_bundles | std::views::values) {
            const auto* p = vendor_bundle.vendor_data.find_printer_config_def_by_legacy_printer_model(printer_model);
            if (p != nullptr) {
                printer_def = std::make_tuple(std::cref(vendor_bundle), std::cref(*p));
                break;
            }
        }
        if (printer_def.has_value()) {
            hw_config = from_def(
                std::get<0>(printer_def.value()).get().vendor_data,
                std::get<1>(printer_def.value()).get()
            );
        } else {
            // TODO: if no def found, build it from scratch
            // this snippet will create configuration, but it has no vendor_id
            // which leads to issues down the stream (we don't have list of tool items to present, for instance)
            // we need to fix these issues before enabling it here
            
            // hw_config = new_scratch_config(
            //     legacy_preset.technology,
            //     legacy_preset.printer_settings_id,
            //     legacy_preset.tools.size()
            // );

            return  tl::unexpected{"Cannot find compatible printer HW config"};
        }

    }

    // update tools if needed
    if (hw_config.technology == Domain::PrinterTechnology::FFF && (printer_config.has_value() || printer_def.has_value())) {
        const auto& vendor_bundle =
            (printer_config.has_value() ? std::get<0>(printer_config.value()) :
                                          std::get<0>(printer_def.value()))
                .get();
        bool needs_name_update = false;

        while (hw_config.tool_count > hw_config.tools.size())
            hw_config.tools.emplace_back(hw_config.tools.front());
        for (size_t i = 0; i < hw_config.tools.size(); ++i) {
            auto& tool = hw_config.tools.at(i);
            const auto& legacy_tool = legacy_preset.tools.at(i);

            if (match_tool(tool, legacy_tool))
                continue;

            auto tools_it = hw_config_evaluator.iterate_tools(
                hw_config,
                vendor_bundle
                    .vendor_data.defs.at(Domain::PrinterTechnology::FFF)
                    .tools
            );

            bool tool_found = false;
            for (const auto& tool_def : tools_it) {
                auto loc_tool = from_def(vendor_bundle.vendor_data, tool_def);

                if (match_tool(loc_tool, legacy_tool)) {
                    tool = loc_tool;
                    tool_found = true;
                    break;
                }
            }

            if (!tool_found) {
                if (!legacy_tool.nozzle_diameter.has_value())
                    return tl::unexpected{"Cannot create print tool without known nozzle_diameter"};

                Domain::Preset::HwToolConfigDef tool_config;
                tool_config.features["nozzle_diameter"].default_value =
                    legacy_tool.nozzle_diameter.value();
                if (legacy_tool.nozzle_high_flow.has_value())
                    tool_config.features["nozzle_high_flow"].default_value =
                        legacy_tool.nozzle_high_flow.value();
                tool = from_def(vendor_bundle.vendor_data, tool_config);
            }

            needs_name_update = true;
        }

        if (needs_name_update) {
            hw_config.name =
                Domain::Preset::suggest_name(hw_config, vendor_bundle.vendor_data, false);
            hw_config.short_name =
                Domain::Preset::suggest_name(hw_config, vendor_bundle.vendor_data, true);
        }
    }
    Domain::Preset::SelectedPresetMetadata ret {.hw_config = hw_config};
    ret.printer = create_preset_metadata(legacy_preset.printer_settings_id);
    ret.print = create_preset_metadata(legacy_preset.print_settings_id);
    ret.tools = create_preset_metadatas(legacy_preset.print_settings_id, hw_config.tools.size());
    ret.materials = create_preset_metadatas(legacy_preset.material_settings_id);
    return ret;
}

} // namespace Slic3r::Biz::Preset::IO
