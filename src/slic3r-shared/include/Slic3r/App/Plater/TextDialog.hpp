#pragma once

#include "Slic3r/App/Plater/GizmoWindow.hpp"
#include "Slic3r/App/Yoga/ToggleButton.hpp"
#include "Slic3r/App/Yoga/ComboBox.hpp"
#include "Slic3r/App/Yoga/SliderWithInput.hpp"
#include "Slic3r/App/Yoga/InputTextWithSpin.hpp"
#include "Slic3r/App/Yoga/AlignmentButtons.hpp"
#include "Slic3r/Domain/TextConfiguration.hpp"
#include "Slic3r/Domain/ModelVolume.hpp" // ModelVolumeType
#include <vector>

namespace Slic3r {
// Limits for inputs
template <typename T>
struct MinMax
{
    T min;
    T max;
};

template <typename T>
static bool apply(std::optional<T>& val, const MinMax<T>& limit)
{
    if (!val.has_value())
        return false;
    return apply<T>(*val, limit);
}

template <typename T>
static bool apply(T& val, const MinMax<T>& limit)
{
    if (val > limit.max) {
        val = limit.max;
        return true;
    }
    if (val < limit.min) {
        val = limit.min;
        return true;
    }
    return false;
}
} // namespace Slic3r

namespace Slic3r::App::Yoga {
class LayoutButton;
class InputTextField;
class ScrollArea;
} // namespace Slic3r::App::Yoga

namespace Slic3r::App::Plater {

class TextDialog : public GizmoWindow
{
public:
    TextDialog();

    struct Callbacks
    {
        std::function<void(const std::string&)> text_changed{nullptr};
        std::function<void(const Domain::FontDescriptor&)> font_selection_changed{nullptr};

        std::function<void(double height_in_mm)> height_changed{nullptr};
        std::function<void(double depth_in_mm)> depth_changed{nullptr};

        std::function<void(bool checked)> use_surface_checked{nullptr};
        std::function<void(bool checked)> per_glyph_checked{nullptr};
        std::function<void(const Domain::FontProp::Align& align)> align_changed{nullptr};
        std::function<void(double value)> char_gap_changed{nullptr};
        std::function<void(double value)> line_gap_changed{nullptr};
        std::function<void(double value)> boldness_changed{nullptr};
        std::function<void(double value)> skew_ratio_changed{nullptr};
        std::function<void(double distance_in_mm)> surface_distance_changed{nullptr};
        std::function<void(double angle_in_rad)> rotation_changed{nullptr};
        std::function<void(bool unlocked)> unlock_rotation{nullptr};
        std::function<void()> set_on_face_camera{nullptr};

        std::function<void(int index)> preset_selection_changed{nullptr};
        std::function<void()> save_preset_as{nullptr};
        std::function<void()> save_preset{nullptr};
        std::function<void()> rename_preset{nullptr};
        std::function<void()> delete_preset{nullptr};

        std::function<void(Domain::ModelVolumeType type)> operation_selection_changed{nullptr};
    };

    Callbacks& callbacks();

    void update_units(bool use_inches);
    void update_angle(bool use_radians);

    void set_editor(const std::string& text);
    void set_presets(const std::vector<std::string>& presets, int selected_preset_id);
    void set_fonts(const Domain::FontList& fonts);
    void set_font(const Domain::FontDescriptor& font, bool set_as_default);
    void set_text_height(double height_in_mm, double default_height_in_mm);
    void set_depth(double depth_in_mm, double default_depth_in_mm);

    void set_use_surface(bool checked, bool default_checked);
    void set_per_glyph(bool checked, bool default_checked);

    void
    set_align(const Domain::FontProp::Align& align, const Domain::FontProp::Align& align_default);

    void set_char_gap(double char_gap_in_mm, double default_char_gap_in_mm);
    void set_line_gap(double line_gap_in_mm, double default_line_gap_in_mm);
    void set_boldness(double boldness_in_mm, double default_boldness_in_mm);
    void set_skew_ratio(double value, double default_value);
    void set_surface_distance(
        double maximal_value_in_mm,
        double surface_distance_in_mm,
        double default_surface_distance_in_mm
    );
    void set_rotation(
        const std::optional<float>& angle_in_rad,
        const std::optional<float>& default_angle_in_rad
    );
    void set_rotation_lock(bool lock);

    void set_enable_use_surface(bool enable);
    void set_enable_per_glyph(bool enable);
    void set_enable_line_gap(bool enable);
    void set_enable_surface_distance(bool enable);
    void set_warning(const std::string& warning);

    void set_operation(Domain::ModelVolumeType type);
    void show_part_specific_panel(bool show);

    void set_enable_all_except_font(bool enable);

private:
    void add_advanced_panel();
    void add_part_specific_panel();

private:
    Yoga::InputTextField* m_editor{nullptr};
    Yoga::LayoutButton* m_editor_warning{nullptr};

    Yoga::Item* m_font_row{nullptr};
    Yoga::ComboBox* m_font{nullptr};
    // current OS enumerated fonts with styles(italic/bold) sorted alphanumericaly
    Domain::FontList m_fonts;
    Yoga::ComboBox* m_style{nullptr};

    Yoga::InputTextWithSpin* m_height{nullptr};
    Yoga::InputTextWithSpin* m_depth{nullptr};

    // vector of Text items used for mm/inch units
    // Will be updated on units sweetching
    std::vector<Yoga::Text*> m_units;
    Yoga::Text* m_angle_unit{nullptr};
    Yoga::ToggleButton* m_advanced{nullptr};

    Yoga::Item* m_advanced_panel{nullptr};
    Yoga::Item* m_use_surface_row{nullptr};
    Yoga::ToggleButton* m_use_surface{nullptr};
    Yoga::Item* m_per_glyph_row{nullptr};
    Yoga::ToggleButton* m_per_glyph{nullptr};
    Yoga::AlignmentButtons* m_align{nullptr};
    Yoga::SliderWithInput* m_char_gap{nullptr};
    Yoga::Item* m_line_gap_row{nullptr};
    Yoga::SliderWithInput* m_line_gap{nullptr};
    Yoga::SliderWithInput* m_boldness{nullptr};
    Yoga::SliderWithInput* m_skew_ratio{nullptr};
    Yoga::Item* m_surface_distance_row{nullptr};
    Yoga::SliderWithInput* m_surface_distance{nullptr};
    Yoga::SliderWithInput* m_rotation{nullptr};
    Yoga::LayoutButton* m_lock_offset_btn{nullptr};

    Yoga::LayoutButton* m_set_on_face_camera_btn{nullptr};

    Yoga::ComboBox* m_preset{nullptr};
    Yoga::LayoutButton* m_save_as_new_btn{nullptr};
    Yoga::LayoutButton* m_save_btn{nullptr};
    Yoga::LayoutButton* m_rename_btn{nullptr};
    Yoga::LayoutButton* m_delete_btn{nullptr};

    Yoga::Item* m_part_specific_panel{nullptr};
    Yoga::ComboBox* m_operation{nullptr};

    bool m_use_inches{false}; // use milimeters
    bool m_use_radians{false}; // use degrees

    Callbacks m_callbacks;
};

} // namespace Slic3r::App::Plater
