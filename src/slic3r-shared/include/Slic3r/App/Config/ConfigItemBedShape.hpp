#pragma once

#include "Slic3r/App/Yoga/Item.hpp"
#include "Slic3r/App/Config/ConfigItemControl.hpp"
#include "Slic3r/Biz/Config/BedShape.hpp"
#include <boost/filesystem.hpp>

namespace Slic3r::App {

class BedShapePreview;

namespace Yoga {
class ComboBox;
class InputTextField;
class LayoutButton;
class Rectangle;
class StackLayout;
} // namespace Yoga

class ConfigItemBedShape : public ConfigItemControl, public Yoga::Item
{
public:
    ConfigItemBedShape(
        size_t index,
        const Domain::ConfigItem& data,
        Biz::IConfigBoxSetter& cb_setter,
        std::vector<size_t> cbi_index
    );

protected:
    void on_data_update() override;

private:
    void load_stl(const boost::filesystem::path& input_file_path);
    void send_data();

    void update_shape();
    void create_preview();
    void update_preview();

private:
    Biz::Config::BedShape m_bed_shape;
    std::vector<Domain::Vec2d> m_last_loaded_custom_contour{};

    Yoga::ComboBox* m_shape_combo{nullptr};
    Yoga::InputTextField* m_size_x{nullptr};
    Yoga::InputTextField* m_size_y{nullptr};
    Yoga::InputTextField* m_origin_x{nullptr};
    Yoga::InputTextField* m_origin_y{nullptr};
    Yoga::InputTextField* m_diameter{nullptr};
    Yoga::LayoutButton* m_load_btn{nullptr};

    Yoga::StackLayout* m_ui_layout{nullptr};

    BedShapePreview* m_shape_preview{nullptr};
};

} // namespace Slic3r::App
