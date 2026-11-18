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
                },
                [&](const Domain::NamedBoxRefs& named_box_refs_a)
                {
                    ASSERT(std::holds_alternative<Domain::NamedBoxRefs>(box_or_boxes_b));
                    const auto& named_box_refs_b{std::get<Domain::NamedBoxRefs>(box_or_boxes_b)};
                    ASSERT(named_box_refs_a.size() == named_box_refs_b.size());
                    for (std::size_t inner_index{}; inner_index < named_box_refs_a.size();
                         ++inner_index) {
                        const auto& [name_a, box_ref_a]{named_box_refs_a[inner_index]};
                        const auto& [name_b, box_ref_b]{named_box_refs_b[inner_index]};
                        ASSERT(name_a == name_b);
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
        ignore_tool_material,
        index
    );
    std::visit([](auto& config_pack) { config_pack.macro_overrides = {}; }, config_initial);
    std::visit([](auto& config_pack) { config_pack.macro_overrides = {}; }, config_selected);
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
    return is_dirty_selected_preset(preset_interactor, true, false, false, false);
}

bool is_selected_tool_print_dirty(const PresetInteractor& preset_interactor, size_t index)
{
    return is_dirty_selected_preset(preset_interactor, true, true, false, true, index);
}

bool is_selected_material_dirty(const PresetInteractor& preset_interactor, size_t index)
{
    return is_dirty_selected_preset(preset_interactor, true, true, true, false, index);
}

/** @brief Presets (without modifications) given by SelectedPresetIds. Non-owning. */
struct SelectedPresetPtrs
{
    const Domain::Preset::EvaluatedPrinterPreset::Preset* printer{nullptr};
    const Domain::Preset::EvaluatedPrintPreset::Preset* print{nullptr};
    std::vector<const Domain::Preset::EvaluatedToolPrintPreset::Preset*> tools;
    std::vector<const Domain::Preset::EvaluatedMaterialPreset::Preset*> materials;
};

/** @brief Returns presets from @p evaluated_presets and @p runtime given by @p ids. */
static SelectedPresetPtrs get_presets_by_ids(
    const Domain::Preset::EvaluatedPrinterPresets& evaluated_presets,
    const RuntimePresets& runtime,
    const SelectedPresetIds& ids
)
{
    SelectedPresetPtrs presets{
        .printer = find_in_view(
            PrinterPresetProjectView{evaluated_presets, runtime, ids.hw_config_id},
            ids.printer_id
        ),
        .print = find_in_view(
            PrintPresetProjectView{evaluated_presets, runtime, ids.hw_config_id, ids.printer_id},
            ids.print_id
        )
    };
    ASSERT(presets.printer != nullptr, std::tuple{ids.hw_config_id, ids.printer_id});
    ASSERT(presets.print != nullptr, std::tuple{ids.hw_config_id, ids.printer_id, ids.print_id});

    for (size_t tool_index = 0, n = ids.tool_ids.size(); tool_index < n; ++tool_index) {
        const ToolPrintPresetProjectView view{
            evaluated_presets, runtime, ids.hw_config_id, ids.printer_id, ids.print_id, tool_index
        };
        presets.tools.emplace_back(find_in_view(view, ids.tool_ids[tool_index]));
        ASSERT(presets.tools.back() != nullptr, ids.tool_ids[tool_index]);
    }

    for (size_t slot_index = 0, n = ids.material_ids.size(); slot_index < n; ++slot_index) {
        const MaterialPresetProjectView view{
            evaluated_presets, runtime, ids.hw_config_id, ids.printer_id, ids.print_id, slot_index
        };
        presets.materials.emplace_back(find_in_view(view, ids.material_ids[slot_index]));
        ASSERT(presets.materials.back() != nullptr, ids.material_ids[slot_index]);
    }

    return presets;
}

/** @brief Returns presets of the selected project given by @p ids. */
static SelectedPresetPtrs
get_presets_by_ids(const PresetInteractor& preset_interactor, const SelectedPresetIds& ids)
{
    return get_presets_by_ids(
        preset_interactor.evaluated_presets(),
        preset_interactor.selected_runtime_presets(),
        ids
    );
}

static PresetSelectionNames preset_names(const SelectedPresetPtrs& presets)
{
    const auto name = [](const auto& preset)
    { return PresetSelectionNames::PresetName{std::string{preset.short_name()}, preset.origin}; };

    PresetSelectionNames names = {.printer = name(*presets.printer), .print = name(*presets.print)};
    for (const auto* tool : presets.tools)
        names.tools.emplace_back(name(*tool));
    for (const auto* material : presets.materials)
        names.materials.emplace_back(name(*material));

    return names;
}

static PresetSelectionNames selected_preset_names(const PresetInteractor& preset_interactor)
{
    return preset_names(
        get_presets_by_ids(preset_interactor, preset_interactor.selected_preset_ids())
    );
}

/** @brief Returns configuration and names of @p presets. */
static std::pair<Domain::ConfigPack, PresetSelectionNames>
config_and_names(const SelectedPresetPtrs& presets)
{
    return {
        config(*presets.printer, *presets.print, presets.tools, presets.materials),
        preset_names(presets)
    };
}

static std::string dialog_name()
{
    return Biz::_u8L("Switching presets: unsaved changes");
}

/**
 * @brief Shows unsaved changes dialog for switching to @p presets_new
 * and stores the user's decision in @p preset_interactor.
 *
 * @param config_original   Initial configuration of the presets which are going to be switched.
 * @param presets_new       Presets which are going to be selected. When nullptr (e.g. printer
 *                          technology is changed), new configuration isn't shown in the dialog.
 * @return False if the user canceled the switch.
 */
static bool resolve_unsaved_changes(
    PresetInteractor& preset_interactor,
    const Domain::ConfigPack& config_original,
    const SelectedPresetPtrs* presets_new,
    bool new_printer_has_multiple_extruders = false
)
{
    std::optional<std::pair<Domain::ConfigPack, PresetSelectionNames>> config_and_names_new;
    if (presets_new != nullptr)
        config_and_names_new = config_and_names(*presets_new);

    PresetsSwitchStates exit_states =
        preset_interactor.dialog_manager()->show_unsaved_changes_dialog(
            dialog_name(),
            config_original,
            preset_interactor.selected_printer_preset().config(),
            config_and_names_new ? &config_and_names_new->first : nullptr,
            selected_preset_names(preset_interactor),
            config_and_names_new ? config_and_names_new->second : PresetSelectionNames(),
            preset_interactor,
            new_printer_has_multiple_extruders
        );

    const bool ret = !exit_states.empty();
    preset_interactor.set_unsaved_changes(std::move(exit_states));
    return ret;
}

static bool has_multiple_extruders(const Domain::Preset::HwPrinterConfig& hw_config)
{
    return Domain::Preset::get_feature<bool>(hw_config.features, "multi_extruder").value_or(false);
}

/**
 * @brief Shows unsaved changes dialog for switching to a printer with @p hw_config.
 * @param get_presets_new  Returns presets which are going to be selected. It isn't called
 *                         if printer technology is changed (new configuration isn't shown then).
 */
template <typename GetPresetsFn>
static bool resolve_printer_switch(
    PresetInteractor& preset_interactor,
    const Domain::Preset::HwPrinterConfig& hw_config,
    GetPresetsFn&& get_presets_new
)
{
    const bool multi_extruder = has_multiple_extruders(hw_config);

    // if printer technology is changed, there is no need to get new selected configuration
    // and show it in dialog
    if (preset_interactor.selected_printer_preset().technology() != hw_config.technology) {
        return resolve_unsaved_changes(
            preset_interactor,
            original_config(preset_interactor),
            nullptr,
            multi_extruder
        );
    }

    const SelectedPresetPtrs presets_new = get_presets_new();
    return resolve_unsaved_changes(
        preset_interactor,
        original_config(preset_interactor),
        &presets_new,
        multi_extruder
    );
}

/** @brief Same as can_select_printer_preset(), but without the check of the dirty state. */
static bool resolve_printer_preset_switch(
    PresetInteractor& preset_interactor,
    const std::string& printer_hw_config_id,
    const std::string& printer_preset_id,
    const PreferredMaterialTypes& material_types = {}
)
{
    return resolve_printer_switch(
        preset_interactor,
        preset_interactor.get_printer_config(printer_hw_config_id).first.get(),
        [&]
        {
            return get_presets_by_ids(
                preset_interactor,
                preset_interactor.get_preset_ids_after_selection(
                    printer_hw_config_id,
                    printer_preset_id,
                    {},
                    material_types
                )
            );
        }
    );
}

bool can_select_printer_preset(
    PresetInteractor& preset_interactor,
    const std::string& printer_hw_config_id,
    const std::string& printer_preset_id
)
{
    if (!is_dirty_selected_preset(preset_interactor))
        return true;

    return resolve_printer_preset_switch(
        preset_interactor,
        printer_hw_config_id,
        printer_preset_id
    );
}

/** @brief Same as can_select_printer_config(), but without the check of the dirty state. */
static bool resolve_printer_config_switch(
    PresetInteractor& preset_interactor,
    const std::string& hw_config_id,
    const PreferredMaterialTypes& material_types
)
{
    // Presets are refilled even when the same hw config is selected,
    // so the unsaved changes have to be resolved too.
    return resolve_printer_preset_switch(
        preset_interactor,
        hw_config_id,
        preset_interactor.printer_preset_id_for_hw_config(hw_config_id),
        material_types
    );
}

bool can_select_printer_config(
    PresetInteractor& preset_interactor,
    const std::string& hw_config_id,
    const PreferredMaterialTypes& material_types
)
{
    if (!is_dirty_selected_preset(preset_interactor))
        return true;

    return resolve_printer_config_switch(preset_interactor, hw_config_id, material_types);
}

bool can_select_external_printer_config(
    PresetInteractor& preset_interactor,
    const Domain::Preset::HwPrinterConfig& printer,
    const PreferredMaterialTypes& material_types
)
{
    if (!is_dirty_selected_preset(preset_interactor))
        return true;

    if (const std::optional<std::string> hw_config_id =
            preset_interactor.find_external_printer_config(printer))
    {
        return resolve_printer_config_switch(preset_interactor, *hw_config_id, material_types);
    }

    // A new hw config is created on selection. Its presets are evaluated without storing them
    // to get the new presets for the dialog.
    const tl::expected<Domain::Preset::HwPrinterConfig, std::string> hw_config =
        preset_interactor.evaluate_external_printer_config(printer);
    if (!hw_config.has_value())
        return true; // The selection fails, so presets aren't switched.

    const std::optional<NewHwConfigPresets> new_presets =
        preset_interactor.evaluate_new_hw_config(*hw_config);
    if (!new_presets.has_value())
        return true; // The selection fails, so presets aren't switched.

    return resolve_printer_switch(
        preset_interactor,
        new_presets->hw_config,
        [&]
        {
            // a new hw config has no runtime presets
            const std::string printer_preset_id = preset_interactor.printer_preset_id_for_hw_config(
                PrinterPresetProjectView{
                    new_presets->evaluated_presets,
                    no_runtime_presets(),
                    new_presets->hw_config.id
                }
            );
            // returned presets point into new_presets, which outlives the dialog
            return get_presets_by_ids(
                new_presets->evaluated_presets,
                no_runtime_presets(),
                preset_interactor.get_preset_ids_after_selection(
                    *new_presets,
                    printer_preset_id,
                    {},
                    material_types
                )
            );
        }
    );
}

bool can_select_print_preset(PresetInteractor& preset_interactor, const std::string& print_id)
{
    if (!is_selected_print_dirty(preset_interactor))
        return true;

    const auto& selected_preset = preset_interactor.selected_printer_preset();
    const SelectedPresetPtrs presets_new = get_presets_by_ids(
        preset_interactor,
        preset_interactor.get_preset_ids_after_selection(
            selected_preset.hw_config.id,
            selected_preset.printer.id,
            print_id
        )
    );
    return resolve_unsaved_changes(
        preset_interactor,
        original_config(preset_interactor, true),
        &presets_new
    );
}

bool can_select_tool_print_preset(
    PresetInteractor& preset_interactor,
    size_t tool_index,
    const std::string& tool_print_id
)
{
    if (!is_selected_tool_print_dirty(preset_interactor, tool_index))
        return true;

    // only the tool print preset is switched, other presets stay selected
    SelectedPresetIds ids_new        = preset_interactor.selected_preset_ids();
    ids_new.tool_ids.at(tool_index) = tool_print_id;
    const SelectedPresetPtrs presets_new = get_presets_by_ids(preset_interactor, ids_new);
    return resolve_unsaved_changes(
        preset_interactor,
        original_config(preset_interactor, true, true, false, true, tool_index),
        &presets_new
    );
}

bool can_select_material_preset(
    PresetInteractor& preset_interactor,
    size_t material_index,
    const std::string& material_id
)
{
    if (!is_selected_material_dirty(preset_interactor, material_index))
        return true;

    // only the material preset is switched, other presets stay selected
    SelectedPresetIds ids_new                = preset_interactor.selected_preset_ids();
    ids_new.material_ids.at(material_index) = material_id;
    const SelectedPresetPtrs presets_new     = get_presets_by_ids(preset_interactor, ids_new);
    return resolve_unsaved_changes(
        preset_interactor,
        original_config(preset_interactor, true, true, true, false, material_index),
        &presets_new
    );
}

} // namespace Slic3r::Biz::Preset::PresetSelectionCheck
