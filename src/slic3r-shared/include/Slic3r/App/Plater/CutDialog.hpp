#pragma once

#include "Slic3r/App/Plater/GizmoWindow.hpp"
#include "Slic3r/App/Yoga/ButtonGroup.hpp"
#include "Slic3r/App/Yoga/Rectangle.hpp"
#include "Slic3r/Domain/CutConnector.hpp"
#include "Slic3r/Biz/Utils/CutUtils.hpp"
#include "Slic3r/Biz/IUndoProvider.hpp"

#include "Slic3r/App/Undo/ToolState.hpp"

namespace Slic3r::App::Yoga {
class LayoutButton;
class ToggleButton;
class RadioButton;
class InputTextField;
class InputTextWithSpin;
class Text;
class SliderWithInput;
class ComboBox;
class ScrollArea;

class PartProcessingItem : public Item
{
public:
    explicit PartProcessingItem(const std::string& part_name, ImColor color);
    virtual ~PartProcessingItem();

    struct Callbacks
    {
        std::function<void()> checked_changed{nullptr};
        std::function<void()> part_action_changed{nullptr};
    };

    Callbacks& callbacks();
    void set_as_part(bool is_part);
    void set_enabled_buttons(bool enabled);
    bool is_checked() const;
    void set_enabled_toggler(bool enabled);
    const Undo::CutGizmoState::PartState& state() const;
    void set_state(const Undo::CutGizmoState::PartState& state);

private:
    Yoga::Text* m_label{nullptr};
    Yoga::ToggleButton* m_part_toggler{nullptr};
    Yoga::ButtonGroup m_group;
    Yoga::RadioButton* m_keep_btn{nullptr};
    Yoga::RadioButton* m_place_on_cut_btn{nullptr};
    Yoga::RadioButton* m_flip_btn{nullptr};

    Callbacks m_callbacks;

    std::string m_name;
    Undo::CutGizmoState::PartState m_state;
};

} // namespace Slic3r::App::Yoga

namespace Slic3r::App::Plater {

class CutDialog : public GizmoWindow
{
public:
    CutDialog();

    struct OutState
    {
        size_t connectors_outside_cut_contour{0};
        size_t connectors_outside_object{0};
        bool connectors_overlap{false};
        bool plane_outside_object{false};
        bool invalid_groove{false};
        bool has_connectors{false};
    };

    struct Callbacks
    {
        std::function<void(double value)> z_changed{nullptr};
        std::function<void()> mode_changed{nullptr};
        std::function<void()> reset_connectors{nullptr};
        std::function<void()> reset_cut_plane{nullptr};
        std::function<void()> flip_cut_plane{nullptr};
        std::function<void()> perform{nullptr};

        std::function<void(double value)> groove_depth_value_changed{nullptr};
        std::function<void(double value)> groove_depth_tolerance_changed{nullptr};
        std::function<void(double value)> groove_width_value_changed{nullptr};
        std::function<void(double value)> groove_width_tolerance_changed{nullptr};
        std::function<void(double value)> flap_angle_changed{nullptr};
        std::function<void(double value)> groove_angle_changed{nullptr};

        std::function<void(bool connectors_editing)> connectors_editing_changed{nullptr};

        std::function<void()> snap_settings_changed{nullptr};
        std::function<void(Biz::UndoSnapshotType undo_snapshot_type)> connector_attributes_changed{
            nullptr
        };
        std::function<void()> connector_transformations_changed{nullptr};

        std::function<void(bool take_snapshot)> cut_settings_changed{nullptr};
    };

    Callbacks& callbacks();

    void set_build_size(Domain::Vec3d tbb_size, double default_z);
    void set_cut_z_position(double cut_z_position);
    void set_planar_mode(bool is_planar);
    void set_connector_type(Domain::CutConnectorType type);
    void set_connector_style(Domain::CutConnectorStyle style);
    void set_connector_shape(Domain::CutConnectorShape shape);
    void set_groove_values(const Biz::Cut::Groove& groove, double max_elements_size);
    void set_connector_defaults(double max_size);
    void set_connector_values(
        std::optional<double> depth,
        std::optional<double> depth_tolerance,
        std::optional<double> half_size,
        std::optional<double> half_size_tolerance,
        std::optional<double> angle
    );
    void force_connectors_editing();

    // check state of teh cut settings and show warning line or "Perform" button
    void update_state(OutState state);

    void update_from_gizmo_state(const Undo::CutGizmoState& state);
    const Undo::CutGizmoState::PartState& part_state_upper() const;
    const Undo::CutGizmoState::PartState& part_state_lower() const;

public:
    bool is_planar_cut_mode{true};
    bool keep_as_parts{false};
    bool connectors_editing{false};

    Domain::CutConnectorType connector_type{Domain::CutConnectorType::Plug};
    Domain::CutConnectorStyle connector_style{Domain::CutConnectorStyle::Prism};
    Domain::CutConnectorShape connector_shape{Domain::CutConnectorShape::Circle};

    double connector_depth{3.};
    double connector_depth_tolerance{0.1};
    double connector_size{2.5};
    double connector_size_tolerance{0.};
    double connector_angle{0.};
    int snap_bulge_proportion{15};
    int snap_space_proportion{30};

private:
    void init_action_buttons();
    void init_connectors_header();
    void init_connectors_input_panel();
    void init_cut_plane_input_panel();
    void add_connectors_help_panel();
    void add_cut_plane_help_panel();
    void add_groove_input_panel();
    void add_cut_settings();
    void add_connectors_editing_buttons();
    void update_panels_visibility();
    void update_keep_object_warning();
    void update_warnings();
    void update_cut_into_states();

    void confirm_connectors();

    bool keep_upper() const;
    bool keep_lower() const;
    bool place_on_cut_upper() const;
    bool place_on_cut_lower() const;
    bool flip_upper() const;
    bool flip_lower() const;

    // return an Item(box) where some control can be placed
    Yoga::Item* add_row(const std::string& title, Yoga::Item* parent);

    void add_tolerances_row(
        Yoga::Item* parent,
        Yoga::InputTextField** first_input,
        const std::string& first_input_tooltip,
        Yoga::InputTextField** second_input,
        const std::string& second_input_tooltip
    );
    void add_angles_row(
        Yoga::Item* parent,
        Yoga::InputTextWithSpin** first_input,
        const std::string& first_input_tooltip,
        const std::string& first_revert_tooltip,
        Yoga::InputTextWithSpin** second_input,
        const std::string& second_input_tooltip,
        const std::string& second_revert_tooltip
    );

private:
    Yoga::Item* m_connectors_input_panel{nullptr};
    Yoga::LayoutButton* m_add_connectors_btn{nullptr};
    Yoga::LayoutButton* m_remove_connectors_btn{nullptr};
    Yoga::ButtonGroup m_connector_type_group;
    Yoga::LayoutButton* m_plug_btn{nullptr};
    Yoga::LayoutButton* m_dowel_btn{nullptr};
    Yoga::LayoutButton* m_snap_btn{nullptr};
    Yoga::ButtonGroup m_connector_style_group;
    Yoga::LayoutButton* m_prism_btn{nullptr};
    Yoga::LayoutButton* m_frustum_btn{nullptr};
    Yoga::ButtonGroup m_connector_shape_group;
    Yoga::LayoutButton* m_triangle_btn{nullptr};
    Yoga::LayoutButton* m_square_btn{nullptr};
    Yoga::LayoutButton* m_hexagon_btn{nullptr};
    Yoga::LayoutButton* m_circle_btn{nullptr};
    Yoga::SliderWithInput* m_connector_depth_value{nullptr};
    Yoga::InputTextField* m_connector_depth_tolerance{nullptr};
    Yoga::SliderWithInput* m_connector_size_value{nullptr};
    Yoga::InputTextField* m_connector_size_tolerance{nullptr};
    Yoga::InputTextWithSpin* m_connector_rotation{nullptr};
    Yoga::InputTextWithSpin* m_snap_bulge_proportion{nullptr};
    Yoga::InputTextWithSpin* m_snap_space_proportion{nullptr};

    Yoga::Item* m_cut_plane_input_panel{nullptr};
    Yoga::ButtonGroup m_mode_group;
    Yoga::LayoutButton* m_planar_mode_btn{nullptr};
    Yoga::LayoutButton* m_dovetail_mode_btn{nullptr};
    Yoga::Text* m_build_volume_x{nullptr};
    Yoga::Text* m_build_volume_y{nullptr};
    Yoga::Text* m_build_volume_z{nullptr};
    Yoga::InputTextField* m_cut_position{nullptr};

    Yoga::Item* m_groove_input_panel{nullptr};
    Yoga::SliderWithInput* m_groove_depth_value{nullptr};
    Yoga::InputTextField* m_groove_depth_tolerance{nullptr};
    Yoga::SliderWithInput* m_groove_width_value{nullptr};
    Yoga::InputTextField* m_groove_width_tolerance{nullptr};
    Yoga::InputTextWithSpin* m_flap_angle{nullptr};
    Yoga::InputTextWithSpin* m_groove_angle{nullptr};

    Yoga::ComboBox* m_cut_into_combo{nullptr};
    Yoga::PartProcessingItem* m_part_A{nullptr};
    Yoga::PartProcessingItem* m_part_B{nullptr};

    Yoga::LayoutButton* m_perform_btn{nullptr};
    Yoga::LayoutButton* m_confirm_connectors_btn{nullptr};
    Yoga::LayoutButton* m_cancel_connectors_btn{nullptr};

    Yoga::Item* m_mode_row{nullptr};
    Yoga::Item* m_cut_into_row{nullptr};
    Yoga::Item* m_connectors_editing_buttons{nullptr};
    Yoga::Item* m_type_row{nullptr};
    Yoga::Item* m_style_row{nullptr};
    Yoga::Item* m_shape_row{nullptr};
    Yoga::Item* m_snap_bulge_row{nullptr};
    Yoga::Item* m_snap_space_row{nullptr};

    Callbacks m_callbacks;

    // vector of Text items used for mm/inch units
    // Will be updated on units sweetching
    std::vector<Yoga::Text*> m_units;
    std::vector<std::string> m_connectors_warnings;
    bool m_has_keep_object_warning{false};

    bool m_imperial_units{false};
};
} // namespace Slic3r::App::Plater
