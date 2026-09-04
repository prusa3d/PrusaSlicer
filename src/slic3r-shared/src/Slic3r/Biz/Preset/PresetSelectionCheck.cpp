#include "Slic3r/Biz/Preset/PresetSelectionCheck.hpp"

#include "Slic3r/Biz/Preset/PresetInteractor.hpp"
#include "Slic3r/Biz/Preset/PresetSelectionNames.hpp"
#include "Slic3r/Biz/Preset/IPresetDialogManager.hpp"
#include "Slic3r/Biz/I18N/I18N.hpp"

#include "Slic3r/Domain/Preset/EvaluatedPreset.hpp"
#include "Slic3r/Domain/FullConfigFDM.hpp"
#include "Slic3r/Domain/FullConfigSLA.hpp"

namespace Slic3r::Biz::Preset::PresetSelectionCheck {

using PresetsSwitchStates = IPresetDialogManager::PresetsSwitchStates;

static Domain::ConfigPack config(
    const Domain::Preset::EvaluatedPrinterPreset::Preset& printer_preset,
    const Domain::Preset::EvaluatedPrintPreset::Preset& print_preset,
    const std::vector<const Domain::Preset::EvaluatedToolPrintPreset::Preset*>& tools,
    const std::vector<const Domain::Preset::EvaluatedMaterialPreset::Preset*>& materials
)
{
    if (printer_preset.kind == Domain::Preset::PresetKind::FdmPrinter) {
        Domain::ConfigPackFDM config;
        config.printer = std::get<Domain::PrinterSettings>(printer_preset.values);
        config.print   = std::get<Domain::PrintSettings>(print_preset.values);
        config.tool.resize(tools.size());
        for (size_t i = 0, n = tools.size(); i < n; i++)
            config.tool[i] = std::get<Domain::ToolPrintSettings>(tools[i]->values);

        config.filament.resize(materials.size());
        for (size_t i = 0, n = materials.size(); i < n; i++)
            config.filament[i] = std::get<Domain::FilamentSettings>(materials[i]->values);

        return config;
    } else {
        ASSERT(printer_preset.kind == Domain::Preset::PresetKind::SlaPrinter);
        Domain::ConfigPackSLA config;
        config.sla_printer_settings = std::get<Domain::SLAPrinterSettings>(printer_preset.values);
        config.sla_print_settings   = std::get<Domain::SLAPrintSettings>(print_preset.values);
        ASSERT(materials.size() == 1);
        config.sla_material_settings =
            std::get<Domain::SLAMaterialSettings>(materials.at(0)->values);

        return config;
    }
}

/** @brief Get initial (without modifications) configuration for selected_preset.
 *
 * @param ignore_printer        If true, then modifications in printer will be ignored
 * @param ignore_print          If true, then modifications in print will be ignored
 * @param ignore_tool_print     If true, then modifications in tool_print will be ignored
 * @param ignore_tool_material  If true, then modifications in tool_print will be ignored
 * @param index                 Index of the intended tool/material for which the preset is checked.
 *                              Check all tool/materials, when index isn't set
 */
static Domain::ConfigPack original_config(
    const PresetInteractor& preset_interactor,
    bool ignore_printer         = false,
    bool ignore_print           = false,
    bool ignore_tool_print      = false,
    bool ignore_tool_material   = false,
    std::optional<size_t> index = std::nullopt
)
{
    auto& selected_preset = preset_interactor.selected_printer_preset();

    const auto& [printer_preset_ref, printer_is_runtime] = preset_interactor.get_printer_preset(
        selected_preset.hw_config.id,
        selected_preset.printer.id
    );
    const auto& [print_preset_ref, print_is_runtime] = preset_interactor.get_print_preset(
        selected_preset.hw_config.id,
        selected_preset.printer.id,
        selected_preset.print.id
    );

    std::vector<const Domain::Preset::EvaluatedToolPrintPreset::Preset*> tools;
    if (ignore_tool_print) {
        for (const auto& tool : selected_preset.tools)
            tools.emplace_back(&tool);
    } else {
        for (size_t i = 0, n = selected_preset.tools.size(); i < n; i++) {
            if (index && i != *index) {
                tools.emplace_back(&selected_preset.tools[i]);
                continue;
            }
            const auto& [ref, is_runtime] = preset_interactor.get_tool_print_preset(
                selected_preset.hw_config.id,
                selected_preset.printer.id,
                selected_preset.print.id,
                i,
                selected_preset.tools[i].id
            );
            tools.emplace_back(&ref.get());
        }
    }

    std::vector<const Domain::Preset::EvaluatedMaterialPreset::Preset*> materials;
    if (ignore_tool_material) {
        for (const auto& material : selected_preset.materials)
            materials.emplace_back(&material);
    } else {
        for (size_t i = 0, n = selected_preset.materials.size(); i < n; i++) {
            if (index && i != *index) {
                materials.emplace_back(&selected_preset.materials[i]);
                continue;
            }
            const auto& [ref, is_runtime] = preset_interactor.get_material_preset(
                selected_preset.hw_config.id,
                selected_preset.printer.id,
                selected_preset.print.id,
                i,
                selected_preset.materials[i].id
            );
            materials.emplace_back(&ref.get());
        }
    }

    return config(
        ignore_printer ? selected_preset.printer : printer_preset_ref.get(),
        ignore_print ? selected_preset.print : print_preset_ref.get(),
        tools,
        materials
    );
}

void filter_diff_keys(const Domain::ConfigBox& cbox, std::vector<std::string>& diff_keys)
{
    std::erase_if(
        diff_keys,
        [cbox](const std::string& key)
        { return cbox.find(key).item->def().category == Domain::ConfigItemDef::Category::Hidden; }
    );
}

static std::vector<std::string>
config_pack_diff(const Domain::ConfigPack& a, const Domain::ConfigPack& b)
{
    const Domain::BoxOrBoxesVector boxes_a{
        std::visit([](const auto& pack) { return Domain::as_boxes(pack); }, a)
    };
    const Domain::BoxOrBoxesVector boxes_b{
        std::visit([](const auto& pack) { return Domain::as_boxes(pack); }, b)
    };

    ASSERT(boxes_a.size() == boxes_b.size());

    std::vector<std::string> result;
    for (std::size_t outter_index{}; outter_index < boxes_a.size(); ++outter_index) {
        const auto& box_or_boxes_a{boxes_a[outter_index]};
        const auto& box_or_boxes_b{boxes_b[outter_index]};
        std::visit(
            Domain::overloaded{
                [&](const Domain::BoxRef& box_ref_a)
                {
                    ASSERT(std::holds_alternative<Domain::BoxRef>(box_or_boxes_b));
                    const auto& box_ref_b{std::get<Domain::BoxRef>(box_or_boxes_b)};
                    std::vector<std::string> diff_keys{box_ref_a.get().diff_keys(box_ref_b.get())};
                    filter_diff_keys(box_ref_a.get(), diff_keys);
                    result.insert(result.end(), diff_keys.begin(), diff_keys.end());
                },
                [&](const Domain::BoxRefs& box_refs_a)
                {
                    ASSERT(std::holds_alternative<Domain::BoxRefs>(box_or_boxes_b));
                    const auto& box_refs_b{std::get<Domain::BoxRefs>(box_or_boxes_b)};
                    for (std::size_t inner_index{}; inner_index < box_refs_a.size();
                         ++inner_index) {
                        const Domain::BoxRef& box_ref_a{box_refs_a[inner_index]};
                        const Domain::BoxRef& box_ref_b{box_refs_b[inner_index]};
                        std::vector<std::string> diff_keys{
                            box_ref_a.get().diff_keys(box_ref_b.get())
                        };
                        filter_diff_keys(box_ref_a.get(), diff_keys);
                        result.insert(result.end(), diff_keys.begin(), diff_keys.end());
                    }
                }
            },
            box_or_boxes_a
        );
    }

    std::ranges::sort(result);
    const auto [first, last]{std::ranges::unique(result)};
    result.erase(first, last);

    return result;
}

/** @brief return list of keys of dirty options in selected_preset.
 *
 * @param ignore_printer        If true, then modifications in printer will be ignored
 * @param ignore_print          If true, then modifications in print will be ignored
 * @param ignore_tool_print     If true, then modifications in tool_print will be ignored
 * @param ignore_tool_material  If true, then modifications in tool_print will be ignored
 * @param index                 Index of the intended tool/material for which the preset is checked.
 *                              Check all tool/materials, when index isn't set
 */
static std::vector<std::string> dirty_options_in_selected_preset(
    const PresetInteractor& preset_interactor,
    bool ignore_printer         = false,
    bool ignore_print           = false,
    bool ignore_tool_print      = false,
    bool ignore_tool_material   = false,
    std::optional<size_t> index = std::nullopt
)
{
    std::vector<std::string> diff_keys;

    Domain::ConfigPack config_selected = preset_interactor.selected_printer_preset().config();
    Domain::ConfigPack config_initial  = original_config(
        preset_interactor,
        ignore_printer,
        ignore_print,
        ignore_tool_print
            || preset_interactor.selected_printer_preset().technology()
                == Domain::PrinterTechnology::SLA,
        ignore_tool_material
            || preset_interactor.selected_printer_preset().technology()
                == Domain::PrinterTechnology::SLA,
        index
    );
    diff_keys = config_pack_diff(config_initial, config_selected);

    return diff_keys;
}

/** @brief Detect if selected_preset is dirty.
 *
 * @param ignore_printer        If true, then modifications in printer will be ignored
 * @param ignore_print          If true, then modifications in print will be ignored
 * @param ignore_tool_print     If true, then modifications in tool_print will be ignored
 * @param ignore_tool_material  If true, then modifications in tool_print will be ignored
 * @param index                 Index of the intended tool/material for which the preset is checked.
 *                              Check all tool/materials, when index isn't set
 */
static bool is_dirty_selected_preset(
    const PresetInteractor& preset_interactor,
    bool ignore_printer         = false,
    bool ignore_print           = false,
    bool ignore_tool_print      = false,
    bool ignore_tool_material   = false,
    std::optional<size_t> index = std::nullopt
)
{
    std::vector<std::string> diff_keys{dirty_options_in_selected_preset(
        preset_interactor,
        ignore_printer,
        ignore_print,
        ignore_tool_print,
        ignore_tool_material,
        index
    )};
    SPDLOG_INFO("Diffs count: {} ", diff_keys.size());
    return !diff_keys.empty();
}

bool is_selected_printer_dirty(const PresetInteractor& preset_interactor)
{
    return is_dirty_selected_preset(preset_interactor, false, true, true, true);
}

// kust a wrappers for is_dirty_selected_preset()
bool is_selected_print_dirty(const PresetInteractor& preset_interactor)
{
    return is_dirty_selected_preset(preset_interactor, true, false, true, true);
}

bool is_selected_tool_print_dirty(const PresetInteractor& preset_interactor, size_t index)
{
    return is_dirty_selected_preset(preset_interactor, true, true, false, true, index);
}

bool is_selected_material_dirty(const PresetInteractor& preset_interactor, size_t index)
{
    return is_dirty_selected_preset(preset_interactor, true, true, true, false, index);
}

static PresetSelectionNames selected_preset_names(const PresetInteractor& preset_interactor)
{
    auto& selected_preset                         = preset_interactor.selected_printer_preset();
    const auto& [printer_ref, printer_is_runtime] = preset_interactor.get_printer_preset(
        selected_preset.hw_config.id,
        selected_preset.printer.id
    );
    const auto& [print_ref, print_is_runtime] = preset_interactor.get_print_preset(
        selected_preset.hw_config.id,
        selected_preset.printer.id,
        selected_preset.print.id
    );

    PresetSelectionNames names = {
        .printer = {std::string{printer_ref.get().short_name()}, printer_ref.get().origin},
        .print   = {std::string{print_ref.get().short_name()}, print_ref.get().origin},
    };

    size_t tool_index = 0;
    for (const auto& tool : selected_preset.tools) {
        const auto& [ref, is_runtime] = preset_interactor.get_tool_print_preset(
            selected_preset.hw_config.id,
            selected_preset.printer.id,
            selected_preset.print.id,
            tool_index++,
            tool.id
        );
        names.tools.emplace_back(
            PresetSelectionNames::PresetName{std::string{ref.get().short_name()}, ref.get().origin}
        );
    }

    size_t material_index = 0;
    for (const auto& material : selected_preset.materials) {
        const auto& [ref, is_runtime] = preset_interactor.get_material_preset(
            selected_preset.hw_config.id,
            selected_preset.printer.id,
            selected_preset.print.id,
            material_index++,
            material.id
        );
        names.materials.emplace_back(
            PresetSelectionNames::PresetName{std::string{ref.get().short_name()}, ref.get().origin}
        );
    }

    return names;
}

static std::tuple<
    const Domain::Preset::EvaluatedPrinterPreset::Preset&,
    const Domain::Preset::EvaluatedPrintPreset::Preset&,
    const Domain::Preset::HwPrinterConfig&,
    PresetSelectionNames>
get_printer_and_print_infos(
    const PresetInteractor& preset_interactor,
    const std::string& printer_hw_config_id,
    const std::string& printer_preset_id,
    const std::string& print_preset_id = std::string()
)
{
    const auto& [printer_ref, printer_is_runtime] =
        preset_interactor.get_printer_preset(printer_hw_config_id, printer_preset_id);
    const Domain::Preset::EvaluatedPrinterPreset::Preset& printer_preset = printer_ref.get();

    const auto& [print_ref, print_is_runtime]                        = print_preset_id.empty() ?
                               preset_interactor.get_print_presets(printer_hw_config_id, printer_preset_id)[0] :
                               preset_interactor
            .get_print_preset(printer_hw_config_id, printer_preset_id, print_preset_id);
    const Domain::Preset::EvaluatedPrintPreset::Preset& print_preset = print_ref.get();

    PresetSelectionNames names = {
        .printer = {std::string{printer_preset.short_name()}, printer_preset.origin},
        .print   = {std::string{print_preset.short_name()}, print_preset.origin}
    };

    const auto [hw_printer_config_ref, is_runtime] =
        preset_interactor.get_printer_config(printer_hw_config_id);

    return {printer_preset, print_preset, hw_printer_config_ref.get(), names};
}

static size_t tool_count(const Domain::Preset::HwPrinterConfig& hw_printer_config)
{
    return hw_printer_config.technology == Domain::PrinterTechnology::SLA ?
        0 :
        hw_printer_config.tool_count;
}

static std::string dialog_name()
{
    return Biz::_u8L("Switching Presets: Unsaved Changes");
}

bool can_select_printer_preset(
    PresetInteractor& preset_interactor,
    const std::string& printer_hw_config_id,
    const std::string& printer_preset_id
)
{
    if (!is_dirty_selected_preset(preset_interactor))
        return true;

    auto [printer_preset, print_preset, hw_printer_config, names_new] =
        get_printer_and_print_infos(preset_interactor, printer_hw_config_id, printer_preset_id);

    auto& selected_preset = preset_interactor.selected_printer_preset();

    IPresetDialogManager* dlg_manager = preset_interactor.dialog_manager();

    if (selected_preset.technology() != hw_printer_config.technology) {
        // if printer technology is changed, there is no need to get new selected configuration and show it in dialog
        PresetsSwitchStates exit_states = dlg_manager->show_unsaved_changes_dialog(
            dialog_name(),
            original_config(preset_interactor),
            selected_preset.config(),
            nullptr,
            selected_preset_names(preset_interactor),
            PresetSelectionNames(),
            preset_interactor,
            Domain::Preset::get_feature<bool>(hw_printer_config.features, "multi_extruder")
                .value_or(false)
        );

        const bool ret = !exit_states.empty();
        preset_interactor.set_unsaved_changes(std::move(exit_states));
        return ret;
    }

    std::vector<const Domain::Preset::EvaluatedToolPrintPreset::Preset*> tools;
    for (size_t tool_index = 0; tool_index < tool_count(hw_printer_config); tool_index++) {
        const auto& [ref, is_runtime] = preset_interactor.get_tool_print_presets(
            printer_hw_config_id,
            printer_preset_id,
            print_preset.id,
            tool_index
        )[0];
        const Domain::Preset::EvaluatedToolPrintPreset::Preset& tool_print = ref.get();
        tools.emplace_back(&tool_print);
        names_new.tools.emplace_back(
            PresetSelectionNames::PresetName{std::string{tool_print.short_name()}, tool_print.origin}
        );
    }

    std::vector<const Domain::Preset::EvaluatedMaterialPreset::Preset*> materials;
    size_t tool_cnt = hw_printer_config.technology == Domain::PrinterTechnology::SLA ?
        1 :
        tool_count(hw_printer_config);
    for (size_t tool_index = 0; tool_index < tool_cnt; tool_index++) {
        const auto& [ref, is_runtime] = preset_interactor.get_material_presets(
            printer_hw_config_id,
            printer_preset_id,
            print_preset.id,
            tool_index
        )[0];
        const Domain::Preset::EvaluatedMaterialPreset::Preset& material_preset = ref.get();
        materials.emplace_back(&material_preset);
        names_new.materials.emplace_back(
            PresetSelectionNames::PresetName{
                std::string{material_preset.short_name()},
                material_preset.origin
            }
        );
    }

    Domain::ConfigPack config_new = config(printer_preset, print_preset, tools, materials);

    PresetsSwitchStates exit_states = dlg_manager->show_unsaved_changes_dialog(
        dialog_name(),
        original_config(preset_interactor),
        selected_preset.config(),
        &config_new,
        selected_preset_names(preset_interactor),
        names_new,
        preset_interactor,
        Domain::Preset::get_feature<bool>(hw_printer_config.features, "multi_extruder")
        .value_or(false)
    );

    const bool ret = !exit_states.empty();
    preset_interactor.set_unsaved_changes(std::move(exit_states));
    return ret;
}

bool can_select_print_preset(PresetInteractor& preset_interactor, const std::string& print_id)
{
    if (!is_selected_print_dirty(preset_interactor))
        return true;

    auto& selected_preset = preset_interactor.selected_printer_preset();

    const std::string& printer_hw_config_id = selected_preset.hw_config.id;
    const std::string& printer_preset_id    = selected_preset.printer.id;

    auto [printer_preset, print_preset, hw_printer_config, names_new] = get_printer_and_print_infos(
        preset_interactor,
        printer_hw_config_id,
        printer_preset_id,
        print_id
    );

    std::vector<const Domain::Preset::EvaluatedToolPrintPreset::Preset*> tools;
    for (size_t tool_index = 0, n = tool_count(hw_printer_config); tool_index < n; tool_index++) {
        const auto& [ref, is_runtime] = preset_interactor.get_tool_print_presets(
            printer_hw_config_id,
            printer_preset_id,
            print_preset.id,
            tool_index
        )[0];
        const Domain::Preset::EvaluatedToolPrintPreset::Preset& tool_print = ref.get();
        tools.emplace_back(&tool_print);
        names_new.tools.emplace_back(
            PresetSelectionNames::PresetName{
                std::string{tool_print.short_name()},
                tool_print.origin
            }
        );
    }

    std::vector<const Domain::Preset::EvaluatedMaterialPreset::Preset*> materials;
    size_t tool_cnt = hw_printer_config.technology == Domain::PrinterTechnology::SLA ?
        1 :
        tool_count(hw_printer_config);
    for (size_t tool_index = 0; tool_index < tool_cnt; tool_index++) {
        const auto& [ref, is_runtime] = preset_interactor.get_material_presets(
            printer_hw_config_id,
            printer_preset_id,
            print_preset.id,
            0
        )[0];
        const Domain::Preset::EvaluatedMaterialPreset::Preset& material_preset = ref.get();
        materials.emplace_back(&material_preset);
        names_new.materials.emplace_back(
            PresetSelectionNames::PresetName{
                std::string{material_preset.short_name()},
                material_preset.origin
            }
        );
    }

    Domain::ConfigPack config_new = config(printer_preset, print_preset, tools, materials);

    IPresetDialogManager* dlg_manager = preset_interactor.dialog_manager();
    PresetsSwitchStates exit_states   = dlg_manager->show_unsaved_changes_dialog(
        dialog_name(),
        original_config(preset_interactor, true),
        selected_preset.config(),
        &config_new,
        selected_preset_names(preset_interactor),
        names_new,
        preset_interactor
    );

    const bool ret = !exit_states.empty();
    preset_interactor.set_unsaved_changes(std::move(exit_states));
    return ret;
}

bool can_select_tool_print_preset(
    PresetInteractor& preset_interactor,
    size_t tool_index,
    const std::string& tool_print_id
)
{
    if (!is_selected_tool_print_dirty(preset_interactor, tool_index))
        return true;

    auto& selected_preset = preset_interactor.selected_printer_preset();

    const std::string& printer_hw_config_id = selected_preset.hw_config.id;
    const std::string& printer_preset_id    = selected_preset.printer.id;
    const std::string& print_preset_id      = selected_preset.print.id;

    auto [printer_preset, print_preset, hw_printer_config, names_new] = get_printer_and_print_infos(
        preset_interactor,
        printer_hw_config_id,
        printer_preset_id,
        print_preset_id
    );

    std::vector<const Domain::Preset::EvaluatedToolPrintPreset::Preset*> tools;
    for (size_t i = 0, n = tool_count(hw_printer_config); i < n; i++) {
        const auto& [ref, is_runtime] = preset_interactor.get_tool_print_preset(
            printer_hw_config_id,
            printer_preset_id,
            print_preset.id,
            i,
            i == tool_index ? tool_print_id : selected_preset.tools[i].id
        );
        const Domain::Preset::EvaluatedToolPrintPreset::Preset& tool_print = ref.get();
        tools.emplace_back(&tool_print);
        names_new.tools.emplace_back(
            PresetSelectionNames::PresetName{
                std::string{tool_print.short_name()},
                tool_print.origin
            }
        );
    }

    std::vector<const Domain::Preset::EvaluatedMaterialPreset::Preset*> materials;
    for (size_t i = 0, n = selected_preset.materials.size(); i < n; i++) {
        if (i == tool_index) {
            const auto& [ref, is_runtime] = preset_interactor.get_material_presets(
                printer_hw_config_id,
                printer_preset_id,
                print_preset.id,
                i
            )[0];
            const Domain::Preset::EvaluatedMaterialPreset::Preset& material_preset = ref.get();
            materials.emplace_back(&material_preset);
            names_new.materials.emplace_back(
                PresetSelectionNames::PresetName{
                    std::string{material_preset.short_name()},
                    material_preset.origin
                }
            );
        } else {
            const auto& [ref, is_runtime] = preset_interactor.get_material_preset(
                printer_hw_config_id,
                printer_preset_id,
                print_preset.id,
                i,
                selected_preset.materials[i].id
            );
            const Domain::Preset::EvaluatedMaterialPreset::Preset& material_preset = ref.get();
            materials.emplace_back(&material_preset);
            names_new.materials.emplace_back(
                PresetSelectionNames::PresetName{
                    std::string{material_preset.short_name()},
                    material_preset.origin
                }
            );
        }
    }

    Domain::ConfigPack config_new = config(printer_preset, print_preset, tools, materials);

    IPresetDialogManager* dlg_manager = preset_interactor.dialog_manager();
    PresetsSwitchStates exit_states   = dlg_manager->show_unsaved_changes_dialog(
        dialog_name(),
        original_config(preset_interactor, true, true, false, false, tool_index),
        selected_preset.config(),
        &config_new,
        selected_preset_names(preset_interactor),
        names_new,
        preset_interactor
    );

    const bool ret = !exit_states.empty();
    preset_interactor.set_unsaved_changes(std::move(exit_states));
    return ret;
}

bool can_select_material_preset(
    PresetInteractor& preset_interactor,
    size_t material_index,
    const std::string& material_id
)
{
    if (!is_selected_material_dirty(preset_interactor, material_index))
        return true;

    auto& selected_preset = preset_interactor.selected_printer_preset();

    const std::string& printer_hw_config_id = selected_preset.hw_config.id;
    const std::string& printer_preset_id    = selected_preset.printer.id;
    const std::string& print_preset_id      = selected_preset.print.id;

    auto [printer_preset, print_preset, hw_printer_config, names_new] = get_printer_and_print_infos(
        preset_interactor,
        printer_hw_config_id,
        printer_preset_id,
        print_preset_id
    );

    std::vector<const Domain::Preset::EvaluatedToolPrintPreset::Preset*> tools;
    for (size_t i = 0, n = tool_count(hw_printer_config); i < n; i++) {
        const auto& [ref, is_runtime] = preset_interactor.get_tool_print_preset(
            printer_hw_config_id,
            printer_preset_id,
            print_preset.id,
            i,
            selected_preset.tools[i].id
        );
        const Domain::Preset::EvaluatedToolPrintPreset::Preset& tool_print = ref.get();
        tools.emplace_back(&tool_print);
        names_new.tools.emplace_back(
            PresetSelectionNames::PresetName{
                std::string{tool_print.short_name()},
                tool_print.origin
            }
        );
    }

    std::vector<const Domain::Preset::EvaluatedMaterialPreset::Preset*> materials;
    for (size_t i = 0, n = selected_preset.materials.size(); i < n; i++) {
        const auto& [ref, is_runtime] = preset_interactor.get_material_preset(
            printer_hw_config_id,
            printer_preset_id,
            print_preset.id,
            i,
            i == material_index ? material_id : selected_preset.materials[i].id
        );
        const Domain::Preset::EvaluatedMaterialPreset::Preset& material_preset = ref.get();
        materials.emplace_back(&material_preset);
        names_new.materials.emplace_back(
            PresetSelectionNames::PresetName{
                std::string{material_preset.short_name()},
                material_preset.origin
            }
        );
    }

    Domain::ConfigPack config_new = config(printer_preset, print_preset, tools, materials);

    IPresetDialogManager* dlg_manager = preset_interactor.dialog_manager();
    PresetsSwitchStates exit_states   = dlg_manager->show_unsaved_changes_dialog(
        dialog_name(),
        original_config(preset_interactor, true, true, true, false, material_index),
        selected_preset.config(),
        &config_new,
        selected_preset_names(preset_interactor),
        names_new,
        preset_interactor
    );

    const bool ret = !exit_states.empty();
    preset_interactor.set_unsaved_changes(std::move(exit_states));
    return ret;
}

} // namespace Slic3r::Biz::Preset::PresetSelectionCheck
