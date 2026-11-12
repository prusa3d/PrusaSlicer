#pragma once

#include "Slic3r/Domain/Preset/Types.hpp"
#include "Slic3r/Domain/Preset/EvaluatedPreset.hpp"
#include "Slic3r/Biz/Preset/PresetDiffOperation.hpp"
#include "Slic3r/Biz/Preset/PresetSelectionNames.hpp"
#include "Slic3r/Biz/Preset/IPresetDialogManager.hpp"

#include <wx/dialog.h>

class wxStaticText;
class wxBoxSizer;
class wxButton;

namespace Slic3r::App::WX {
class DiffViewCtrl;

class UnsavedChangesDialog : public wxDialog
{
public:
    using PresetKind          = Domain::Preset::PresetKind;
    using PresetDiffOperation = Biz::Preset::PresetDiffOperation;
    using PresetsSwitchStates = Biz::Preset::IPresetDialogManager::PresetsSwitchStates;
    using PresetSwitchKindId  = Biz::Preset::PresetSwitchKindId;

    UnsavedChangesDialog(
        const std::string& dialog_name,
        const Domain::ConfigPack& config_original,
        const Domain::ConfigPack& config_selected,
        Domain::ConfigPack* config_new_selected,
        const Biz::Preset::PresetSelectionNames& preset_names,
        const Biz::Preset::PresetSelectionNames& preset_names_new,
        const Biz::Preset::PresetInteractor& preset_interactor,
        bool new_printer_has_multiple_extruders = false
    );
    ~UnsavedChangesDialog() = default;

    PresetsSwitchStates exit_states() const
    {
        return m_exit_states;
    }

private:
    void create_tree();
    void add_buttons(wxBoxSizer* sizer);
    void compare();
    void show_current_diffs();
    void update_transfer_button(PresetSwitchKindId kind_id);
    void update_tree(PresetSwitchKindId kind_id, const std::vector<std::string>& diff_keys);
    void append_diff_keys(
        PresetKind kind,
        const std::string& preset_name,
        const std::string& new_preset_name,
        const Domain::ConfigBox* config_left,
        const Domain::ConfigBox* config_mid,
        const Domain::ConfigBox* config_right,
        const std::vector<std::string>& diff_keys
    );
    void show_info_line(PresetDiffOperation operation, std::string preset_name = "");

    void process_button_click(PresetDiffOperation operation);

private:
    wxStaticText* m_top_info_line{nullptr};
    wxStaticText* m_bottom_info_line{nullptr};

    DiffViewCtrl* m_tree{nullptr};

    wxButton* m_back_btn{nullptr};
    wxButton* m_save_btn{nullptr};
    wxButton* m_transfer_btn{nullptr};
    wxButton* m_discard_btn{nullptr};

    Biz::Preset::PresetSelectionNames m_preset_names;
    Biz::Preset::PresetSelectionNames m_preset_names_new;
    Domain::PrinterTechnology m_printer_technology{Domain::PrinterTechnology::FFF};

    const Domain::ConfigPack m_config_original;
    const Domain::ConfigPack m_config_selected;
    Domain::ConfigPack* m_config_new{nullptr};

    using DiffsPerKind = std::map<PresetSwitchKindId, std::vector<std::string>>;
    DiffsPerKind m_diffs_per_kind;

    PresetsSwitchStates m_exit_states;
    const Biz::Preset::PresetInteractor& m_preset_interactor;
    // Indicates a count of preset checkes in queue before close the dialog
    int m_exit_queue{0};
    bool m_is_enabled_transfer{false};
    bool m_new_printer_has_multiple_extruders{false};

    std::vector<std::string> m_reserved_material_preset_names;
    std::vector<std::string> m_reserved_tool_preset_names;
};

} // namespace Slic3r::App::WX
