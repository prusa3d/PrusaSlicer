#pragma once

#include "Slic3r/App/Plater/GizmoWindow.hpp"
#include "Slic3r/App/Yoga/ToggleButton.hpp"
#include "Slic3r/App/Yoga/ComboBox.hpp"
#include "Slic3r/App/Yoga/SliderWithInput.hpp"
#include "Slic3r/App/Yoga/InputTextWithSpin.hpp"
#include "Slic3r/App/Yoga/LayoutButton.hpp"
#include "Slic3r/Domain/ModelVolume.hpp" // ModelVolumeType
#include <vector>

namespace Slic3r::Domain {
struct EmbossShape;
} // namespace Slic3r::Domain

namespace Slic3r::App::Yoga {
class Menu;
} // namespace Slic3r::App::Yoga

namespace Slic3r::App::Plater {

class SvgDialog : public GizmoWindow
{
public:
    // embossing depth input limits [in mm] prevent negative and zero value
    static const double MIN_DEPTH;
    static const double MAX_DEPTH;
    // height limits for embossed object [in mm] prevent negative and zero value
    static const double MIN_HEIGHT;
    static const double MAX_HEIGHT;
    // width limits for embossed object [in mm] prevent negative and zero value
    static const double MIN_WIDTH;
    static const double MAX_WIDTH;

    SvgDialog();

    struct Callbacks
    {
        std::function<void(double value)> depth_changed{nullptr};
        std::function<void(const Domain::Vec2d& size)> size_changed{nullptr};
        std::function<void(bool unlocked)> unlock_size{nullptr};
        std::function<void(bool checked)> use_surface_checked{nullptr};
        std::function<void(double distance_in_mm)> surface_distance_changed{nullptr};
        std::function<void(double angle_in_rad)> rotation_changed{nullptr};
        std::function<void(bool unlocked)> unlock_rotation{nullptr};
        std::function<void()> mirror_x{nullptr};
        std::function<void()> mirror_y{nullptr};
        std::function<void()> face_the_camera{nullptr};

        std::function<void()> reload_file{nullptr};
        std::function<void()> change_file{nullptr};
        std::function<void()> forgot_filepath{nullptr};
        std::function<void()> bake{nullptr};
        std::function<void()> save_as{nullptr};

        std::function<void(Domain::ModelVolumeType type)> operation_selection_changed{nullptr};
    };

    Callbacks& callbacks();

    void set_warning(const std::string& warning);
    void set_shape(const Domain::EmbossShape& shape);
    void set_enable_reload_from_disk(bool enable);

    void update_units(bool use_inch);
    void update_angle(bool use_deg);

    void set_depth(double depth_in_mm);
    void set_size(const Domain::Vec2d& size, const Domain::Vec2d& size_original);
    void set_size_lock(bool lock);

    void set_use_surface(bool checked);
    void set_surface_distance(double distance_in_mm, double max_distance_in_mm);
    void set_rotation(double angle_in_rad);
    void set_rotation_lock(bool lock);

    void set_enable_use_surface(bool enable);
    void set_enable_surface_distance(bool enable);

    void set_operation(Domain::ModelVolumeType type);
    void show_part_specific_panel(bool show);

private:
    void add_part_specific_panel();

private:
    Yoga::Icon* m_preview{nullptr};
    Yoga::LayoutButton* m_warning{nullptr};
    Yoga::Text* m_filename{nullptr};
    Yoga::LayoutButton* m_reload{nullptr};
    Yoga::Menu* m_options_menu{nullptr};
    Yoga::InputTextWithSpin* m_depth{nullptr};
    Yoga::InputTextWithSpin* m_width{nullptr};
    Yoga::InputTextWithSpin* m_height{nullptr};
    Yoga::LayoutButton* m_lock_size_btn{nullptr};
    Item* m_use_surface_row{nullptr};
    Yoga::ToggleButton* m_use_surface;
    Yoga::SliderWithInput* m_surface_distance{nullptr};
    Yoga::SliderWithInput* m_rotation{nullptr};
    Yoga::LayoutButton* m_lock_rotation_btn{nullptr};

    Yoga::LayoutButton* m_mirror_x{nullptr};
    Yoga::LayoutButton* m_mirror_y{nullptr};

    // vector of Text items used for mm/inch units
    // Will be updated on units switch
    std::vector<Yoga::Text*> m_units;

    Yoga::LayoutButton* m_face_the_camera_btn{nullptr};

    Yoga::Item* m_part_specific_panel{nullptr};
    Yoga::ComboBox* m_operation{nullptr};

    bool m_use_inch = false;
    bool m_use_deg  = true;

    Callbacks m_callbacks;

    bool m_set_debug = false;
};

} // namespace Slic3r::App::Plater
