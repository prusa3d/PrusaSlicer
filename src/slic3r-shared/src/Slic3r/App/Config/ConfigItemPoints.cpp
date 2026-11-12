#include "Slic3r/App/Config/ConfigItemPoints.hpp"

#include "Slic3r/App/Yoga/InputTextField.hpp"
#include "Slic3r/App/Yoga/Validator.hpp"
#include "Slic3r/App/Yoga/Text.hpp"

#include "Slic3r/Biz/IConfigBoxSetter.hpp"

#include <fmt/format.h>

using namespace Slic3r::App::Yoga;

namespace Slic3r::App {

ConfigItemPoints::ConfigItemPoints(
    size_t index,
    const Domain::ConfigItem& data,
    Biz::IConfigBoxSetter& cb_setter,
    std::vector<size_t> cbi_index
) :
    ConfigItemControl(index, data, cb_setter, cbi_index)
{
    set_orientation(Orientation::Vertical);
    set_gap(5);
    set_width(150);

    on_data_update();
}

void ConfigItemPoints::on_data_update()
{
    const std::vector<Domain::Vec2d> data = m_state->get<std::vector<Domain::Vec2d>>();

    if (data.size() != m_points.size()) {
        construct_points();
    }

    for (size_t i = 0; i < data.size(); ++i) {
        const Domain::Vec2d& data_point = data.at(i);
        const Point& point              = m_points.at(i);
        point.input_x->set_text(fmt::format("{:.10g}", data_point.x()));
        point.input_y->set_text(fmt::format("{:.10g}", data_point.y()));
    }
}

void ConfigItemPoints::send_data()
{
    std::vector<Domain::Vec2d> data;
    data.reserve(m_points.size());
    for (const Point& point : std::as_const(m_points)) {
        data.push_back({point.validator_x->value(), point.validator_y->value()});
    }

    set_item_value(Domain::ConfigValue{data});
}

void ConfigItemPoints::construct_points()
{
    for (const Point& point : std::as_const(m_points)) {
        remove(point.container);
    }
    m_points.clear();

    const size_t size = m_state->get<std::vector<Domain::Vec2d>>().size();
    for (size_t i = 0; i < size; ++i) {
        Point& point    = m_points.emplace_back();
        point.container = emplace_back<Item>();
        point.container->set_gap(5);
        point.container->set_margin(Margins(0, 0, 5, 0));
        Text* text = point.container->emplace_back<Text>(std::to_string(i + 1));
        text->set_align({AlignH::Center, AlignV::Center});
        point.validator_x = std::make_unique<DoubleValidator>(
            m_state->def().min.value_or(std::numeric_limits<double>::lowest()),
            m_state->def().max.value_or(std::numeric_limits<double>::max())
        );
        Text* x = point.container->emplace_back<Text>("X");
        x->set_align({AlignH::Center, AlignV::Center});
        point.input_x = point.container->emplace_back<InputTextField>("ConfigItemPointX");
        point.input_x->set_flex_grow(1);
        point.input_x->set_validator(point.validator_x.release());
        point.input_x->callbacks().text_edited = [this]() { send_data(); };

        point.validator_y = std::make_unique<DoubleValidator>(
            m_state->def().min.value_or(std::numeric_limits<double>::lowest()),
            m_state->def().max.value_or(std::numeric_limits<double>::max())
        );
        Text* y = point.container->emplace_back<Text>("Y");
        y->set_align({AlignH::Center, AlignV::Center});
        point.input_y = point.container->emplace_back<InputTextField>("ConfigItemPointY");
        point.input_y->set_flex_grow(1);
        point.input_y->set_validator(point.validator_y.release());
    }
}

} // namespace Slic3r::App
