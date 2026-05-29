#pragma once

#include <vector>

#include <wx/colour.h>

#include "libslic3r/Feature/FullSpectrum/VirtualExtruder.hpp"

#include "GUI_Utils.hpp"

class wxStaticText;
class wxButton;
class wxPanel;
class wxBoxSizer;
class wxWrapSizer;
class wxScrolledWindow;
class wxSpinCtrlDouble;
class StaticBox;
class Button;

namespace Slic3r {
class Model;
class DynamicPrintConfig;
} // namespace Slic3r

namespace Slic3r::GUI {

class GradientRatioBar;
class BarycentricRatioPicker;
class LayerSequenceBar;
class VirtualExtruderListBox;
class BitmapComboBox;

class FullSpectrumDialog : public DPIDialog
{
public:
    FullSpectrumDialog(wxWindow* parent, const Model& model, const DynamicPrintConfig& full_config);
    ~FullSpectrumDialog() override = default;

    const std::vector<FullSpectrum::VirtualExtruder>& result_virtual_extruders() const
    {
        return m_working_list;
    }

    const std::vector<unsigned int>& removed_ids() const
    {
        return m_removed_ids;
    }

protected:
    void on_dpi_changed(const wxRect& suggested_rect) override;
    void on_sys_color_changed() override;

private:
    void apply_colors();

    struct ComponentRow
    {
        wxBoxSizer* row_sizer{nullptr};
        wxStaticText* label{nullptr};
        BitmapComboBox* extruder{nullptr};
        wxStaticText* weight_label{nullptr};
        wxButton* remove_btn{nullptr};
    };

    struct StopRow
    {
        wxBoxSizer* row_sizer{nullptr};
        wxStaticText* label{nullptr};
        BitmapComboBox* extruder{nullptr};
        wxSpinCtrlDouble* position{nullptr};
        wxButton* remove_btn{nullptr};
    };

    struct RecommendationPreset
    {
        std::vector<unsigned int> extruder_ids;
        std::vector<int> ratios_pct;
    };

    const Model& m_model;
    unsigned int m_num_physical = 0;
    std::vector<std::string> m_physical_colors;
    std::vector<std::string> m_physical_types;
    std::vector<FullSpectrum::VirtualExtruder> m_working_list;
    std::vector<unsigned int> m_original_ids;
    std::vector<unsigned int> m_removed_ids;

    VirtualExtruderListBox* m_virtual_extruder_list{nullptr};
    Button* m_btn_add_blend{nullptr};
    Button* m_btn_add_gradient{nullptr};

    wxPanel* m_editor_box{nullptr};
    wxStaticText* m_editor_title{nullptr};
    wxStaticText* m_editor_description{nullptr};
    wxPanel* m_color_swatch{nullptr};

    wxBoxSizer* m_rows_sizer{nullptr};
    std::vector<ComponentRow> m_component_rows;
    std::vector<StopRow> m_stop_rows;
    wxButton* m_btn_add_component{nullptr};

    GradientRatioBar* m_permanent_ratio_bar{nullptr};
    wxStaticText* m_permanent_ratio_title{nullptr};
    GradientRatioBar* m_inline_blend_ratio_bar{nullptr};

    BarycentricRatioPicker* m_permanent_ternary_picker{nullptr};
    BarycentricRatioPicker* m_inline_ternary_picker{nullptr};

    LayerSequenceBar* m_sequence_bar{nullptr};

    wxWindow* m_left_panel{nullptr};
    wxWindow* m_right_panel{nullptr};
    wxBoxSizer* m_right_sizer{nullptr};
    wxPanel* m_presets_pane{nullptr};
    wxPanel* m_editor_pane{nullptr};
    bool m_intro_wrap_guard{false};

    wxScrolledWindow* m_presets_scrolled_window{nullptr};
    wxWrapSizer* m_two_color_presets_wrap{nullptr};
    wxWrapSizer* m_three_color_presets_wrap{nullptr};
    wxStaticText* m_two_color_presets_title{nullptr};
    wxStaticText* m_three_color_presets_title{nullptr};
    wxStaticText* m_presets_empty_hint{nullptr};

    std::vector<bool> m_preset_extruders_enabled;
    wxWrapSizer* m_extruder_filter_wrap{nullptr};
    std::vector<wxWindow*> m_preset_filter_buttons;

    void build_layout();
    void refresh_virtual_extruder_list(int select_index = -1);
    void populate_editor_from_selection();
    void update_preview_and_validation();
    void update_right_panel_visibility();
    void rebuild_component_rows();
    void rebuild_stop_rows();
    void populate_extruder_choices(BitmapComboBox* choice) const;

    void on_list_selected(wxCommandEvent& event);
    void on_add_blend_clicked(wxCommandEvent& event);
    void on_add_gradient(wxCommandEvent& event);
    void on_remove_recipe_clicked(wxCommandEvent& event);
    void on_color_click(wxCommandEvent& event);

    void on_component_changed(wxCommandEvent& event);
    void on_ratio_bar_changed(wxCommandEvent& event);
    void on_add_or_remove_component_clicked(wxCommandEvent& event);
    void on_remove_component(size_t row_index);
    void on_triangle_changed(wxCommandEvent& event);

    void on_add_stop();
    void on_remove_stop(size_t row_index);
    void on_stop_extruder_changed(wxCommandEvent& event);
    void on_stop_position_changed(wxCommandEvent& event);

    void on_preset_clicked(const RecommendationPreset& preset);
    std::vector<RecommendationPreset> build_two_color_presets() const;
    std::vector<RecommendationPreset> build_three_color_presets() const;
    void rebuild_preset_grid();

    wxColour physical_color(unsigned int ext_id_1based) const;
    void clear_color_override();

    unsigned int next_free_virtual_id() const;
    int selected_index() const;
    int count_objects_using(unsigned int virtual_extruder_id) const;
    std::string current_effective_color_for(const FullSpectrum::VirtualExtruder& ve) const;
    unsigned int pick_unused_physical_id(const FullSpectrum::VirtualExtruder& ve) const;
    unsigned int pick_unused_physical_id_for_gradient(
        const FullSpectrum::VirtualExtruder& ve
    ) const;
};

} // namespace Slic3r::GUI
