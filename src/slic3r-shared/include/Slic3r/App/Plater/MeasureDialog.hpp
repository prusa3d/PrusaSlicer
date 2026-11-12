#pragma once

#include "Slic3r/App/Plater/GizmoWindow.hpp"
#include "Slic3r/App/Plater/Measure.hpp"
#include "Slic3r/App/Plater/MeasureGizmoHelper.hpp"

namespace Slic3rc::App::Yoga {
class Text;
class LayoutButton;
} // namespace Slic3rc::App::Yoga

namespace Slic3r::App::Plater {

class MeasureDialog : public GizmoWindow
{
public:
    MeasureDialog();

    enum class MeasureType
    {
        Angle,
        Distance,
    };

    struct SpotDescription : public Yoga::Item
    {
        SpotDescription();

        void reset();
        void set_from(const Measure::FeatureItem& feature);

    private:
        Yoga::Text* m_name  = nullptr;
        Yoga::Text* m_value = nullptr;
    };

    SpotDescription& spot1();
    SpotDescription& spot2();

    void update(const Measure::MeasurementResult& result, const Measure::FeatureCache& features);
    void show_measure(bool show);

private:
    void add_measure_rows();
    void add_spot_row(const ImColor& marker, const std::string& title, Yoga::ItemPtr controls);
    void set_measure(const Measure::MeasurementResult& result);
    void set_measure_row(size_t id, const std::string& name, const std::string& value);

private:
    struct MeasureRowItem
    {
        Yoga::Item* row{nullptr};
        Yoga::Text* name{nullptr};
        Yoga::Text* value{nullptr};
        std::string clipboard_text;
        Yoga::LayoutButton* copy_btn{nullptr};
    };

    std::array<MeasureRowItem, 2> m_measure_rows;

    SpotDescription* m_spot1 = nullptr;
    SpotDescription* m_spot2 = nullptr;

    Yoga::Item* m_helper_panel = nullptr;
    Yoga::Item* m_main_panel   = nullptr;
    Yoga::Item* m_toggle_separator = nullptr;
    GizmoHelpFactory m_extra_help;
    Yoga::Icon* m_help_select_icon   = nullptr;
    Yoga::Text* m_help_select_text   = nullptr;
    Yoga::Icon* m_help_unselect_icon = nullptr;
    Yoga::Text* m_help_unselect_text = nullptr;
};

} // namespace Slic3r::App::Plater
