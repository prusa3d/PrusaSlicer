#pragma once

#include "Slic3r/Domain/Preset/Types.hpp"
#include "Slic3r/Domain/Preset/EvaluatedPreset.hpp"

#include <wx/dialog.h>

class wxStaticText;

namespace Slic3r::Biz::Preset {
class PresetInteractor;
struct PresetItem;
} // namespace Slic3r::Biz::Preset

namespace Slic3r::App::WX {
class DiffViewCtrl;

namespace Diff {
enum class Location : int;
struct Row;
} // namespace Diff

class DiffDialog : public wxDialog
{
public:
    using PresetKind = Domain::Preset::PresetKind;

    // diff per preset kind
    DiffDialog(
        const Slic3r::Biz::Preset::PresetInteractor& preset_interactor,
        std::optional<PresetKind> kind = std::nullopt
    );

    ~DiffDialog() = default;

private:
    void init_from_selection();

    const Biz::Preset::PresetItem& printer_item(int selected_printer);

    void select_printer(
        int selection,
        Diff::Location location,
        int print_selection      = 0,
        int tool_print_selection = 0,
        int material_selection   = 0
    );
    void select_print(
        int selection,
        Diff::Location location,
        int tool_print_selection = 0,
        int material_selection   = 0
    );

    // imptement for vector
    void select_tool_print(
        size_t tool_id,
        size_t tool_print_id,
        Diff::Location location,
        int material_selection = 0
    );
    void select_material(int selection, Diff::Location location);

    void create_tree();
    void compare();

    void update_tree();

    bool show_printers() const;
    bool show_prints() const;
    bool show_tool_prints() const;
    bool show_materials() const;

private:
    const Slic3r::Biz::Preset::PresetInteractor& m_preset_interactor;

    wxStaticText* m_top_info_line{nullptr};
    wxStaticText* m_bottom_info_line{nullptr};

    Diff::Row* m_printers{nullptr};
    Diff::Row* m_prints{nullptr};
    Diff::Row* m_tools_prints{nullptr};
    Diff::Row* m_tool_materials{nullptr};

    DiffViewCtrl* m_tree{nullptr};

    bool m_can_compare{true};
    std::optional<PresetKind> m_kind;

    using DiffsPerKind = std::map<PresetKind, std::vector<std::string>>;
    DiffsPerKind m_diffs_per_kind;
};

} // namespace Slic3r::App::WX
