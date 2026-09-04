#pragma once

#include "Slic3r/App/Plater/GizmoWindow.hpp"
#include "Slic3r/Biz/Arrange/Settings.hpp"
#include "Slic3r/Biz/I18N/I18N.hpp"

namespace Slic3r::App::Yoga {
class SliderWithInput;
class ToggleButton;
class Slider;
class SegmentedControl;
class Separator;
} // namespace Slic3r::App::Yoga

namespace Slic3r::App::Plater {

class PivotPicker;

enum class ArrangeTaskStatus
{
    Idle,
    Running
};

class ArrangeDialog : public GizmoWindow
{
public:
    enum ArrangeMode {
        PrinterGroup,
        SelectedBeds
    };

    using OnArrange      = std::function<void(ArrangeMode)>;
    using OnCancel       = std::function<void()>;

    ArrangeDialog(
        OnArrange on_arrange,
        OnCancel on_cancel,
        const Biz::Arrange::Settings& settings
    );

    void update_segments_visibility();

    void set_bed_segments(const std::optional<Domain::BedSegments>& bed_segments);

    void set_auxiliary_travel_anchor(const std::optional<Domain::Vec2d>& auxiliary_travel_anchor);

    void update_status(const ArrangeTaskStatus status);

    Biz::Arrange::Settings get_settings() const;

private:
    OnArrange m_on_arrange;
    OnCancel m_on_cancel;
    Yoga::SegmentedControl* m_mode{nullptr};
    Yoga::SliderWithInput* m_offset_slider{nullptr};
    Yoga::SliderWithInput* m_bed_offset_slider{nullptr};
    Yoga::ComboBox* m_geometry_handling{nullptr};
    Yoga::ToggleButton* m_enable_rotations_toggle{nullptr};
    PivotPicker* m_pivot_picker{nullptr};
    Yoga::Item* m_bed_segments_section{nullptr};
    Yoga::Separator* m_bed_segments_separator{nullptr};
    Yoga::LayoutButton* m_arrange_button{nullptr};
    std::optional<Domain::BedSegments> m_bed_segments;
    std::optional<Domain::Vec2d> m_auxiliary_travel_anchor;

    std::string m_arrange_all_label{Biz::_u8L("Arrange all")};
    std::string m_arrange_beds_label{Biz::_u8L("Arrange beds")};
};

} // namespace Slic3r::App::Plater
