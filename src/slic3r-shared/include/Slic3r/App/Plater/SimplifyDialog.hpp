#pragma once
#pragma once

#include "Slic3r/App/Plater/GizmoWindow.hpp"
#include "Slic3r/App/Yoga/Text.hpp"
#include "Slic3r/App/Yoga/RadioButton.hpp"
#include "Slic3r/App/Yoga/ComboBox.hpp"
#include "Slic3r/App/Yoga/SliderWithInput.hpp"
#include "Slic3r/App/Yoga/ButtonGroup.hpp"

namespace Slic3r::App::Yoga {
class LayoutButton;
class ToggleButton;
class ProgressBar;
} // namespace Slic3r::App::Yoga

namespace Slic3r::App::Plater {

enum class SimplifyLevelDetail;
class SimplifyDialog : public GizmoWindow
{
public:
    SimplifyDialog();

    struct Callbacks
    {
        std::function<void(SimplifyLevelDetail detail)> detail_level_changed{nullptr};
        std::function<void(double value)> decimate_ratio_changed{nullptr};
        std::function<void(bool use_count)> use_count_changed{nullptr};
        std::function<void(bool checked)> show_wireframe_checked{nullptr};
        std::function<void()> apply{nullptr};
        std::function<void()> close{nullptr};
    };

    Callbacks& callbacks();

    void set_mesh_name(const std::string& name);
    void set_triangles(size_t triangles);
    void set_use_count(bool use_count);
    void set_detail_level(SimplifyLevelDetail detail);
    void set_decimate_ratio_step(double step);
    void set_decimate_ratio(double ratio);
    void set_info_line(const int wanted_triangles);
    void set_show_wireframe(bool checked);
    void set_progress(int progress);
    void set_enabled_by_use_count(bool use_count);
    void set_enable_apply_button(bool enable);
    void set_enable_close_button(bool enable);
    void set_multipart(bool is_multipart);

private:
    void add_text_row(const std::string& title, std::unique_ptr<Yoga::Text> text);
    void add_radio_row(
        std::unique_ptr<Yoga::RadioButton> radio,
        Yoga::ItemPtr control,
        const std::string& unit = std::string()
    );

private:
    Yoga::Passthrough<Yoga::Text> m_mesh_name;
    Yoga::Passthrough<Yoga::Text> m_triangles;

    Yoga::Passthrough<Yoga::RadioButton> m_detail_level_btn;
    Yoga::Passthrough<Yoga::RadioButton> m_decimate_ratio_btn;
    Yoga::ButtonGroup m_radio_group;
    Yoga::Passthrough<Yoga::ComboBox> m_detail_level;
    Yoga::Passthrough<Yoga::SliderWithInput> m_decimate_ratio;

    Yoga::Text* m_info_line              = nullptr;
    Yoga::ToggleButton* m_show_wireframe = nullptr;
    Yoga::LayoutButton* m_apply_btn      = nullptr;
    Yoga::ProgressBar* m_progress        = nullptr;

    Callbacks m_callbacks;
};

enum class SimplifyLevelDetail : int {
    // Order must match the registration m_detail_level->set_items(
    ExtraHigh = 0,
    High,
    Medium,
    Low,
    ExtraLow
};

} // namespace Slic3r::App::Plater
