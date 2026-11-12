#include "Slic3r/App/Plater/TripleInput.hpp"
#include "Slic3r/App/Yoga/InputTextField.hpp"
#include "Slic3r/App/Yoga/Text.hpp"
#include "Slic3r/App/Yoga/Validator.hpp"
#include <fmt/format.h>

using namespace Slic3r::App::Yoga;

namespace Slic3r::App::Plater {

class InputWithLabel : public Yoga::Rectangle {
public:
    InputWithLabel(const std::string& label, const ImColor& color)
    {
        set_align_items(YGAlignCenter);
        set_fill(m_theme->color_imgui(Platform::Color::Button));
        set_padding({2_fpx, 0, 2_fpx, 0});
        auto label_container{emplace_back<Item>()};
        label_container->set_justify_content(YGJustifyCenter);
        m_label = label_container->emplace_back<Text>(label);
        m_label->set_text_color(color);
        label_container->set_flex_grow(0.2);
        input   = emplace_back<InputTextField>();
        input->set_flex_grow(1);
    }

    InputTextField* input;
private:
    Text* m_label;
};

static ItemPtr
small_label(const std::string& label)
{
    auto label_item{std::make_unique<Item>()};
    label_item->set_orientation(Orientation::Horizontal);
    label_item->set_width(20_fpx);
    label_item->set_height_percent(100);
    label_item->set_align_items(YGAlignCenter);
    label_item->set_justify_content(YGJustifyFlexStart);
    label_item->emplace_back<Text>(label);
    return label_item;
}

static std::pair<InputWithLabel*, DoubleValidator*>
emplace_coordinate_input(Item* item, const std::string& label, const ImColor& color)
{
    auto validator{std::make_unique<DoubleValidator>()};
    validator->set_precision(2);
    DoubleValidator* validator_ptr{validator.get()};

    auto* input_with_label{item->emplace_back<InputWithLabel>(label, color)};
    input_with_label->input->set_validator(std::move(validator));
    input_with_label->set_flex_grow(1);
    input_with_label->input->set_default(0);

    return {input_with_label, validator_ptr};
}

TripleInput::TripleInput(
    const std::string& suffix,
    const std::optional<ImColor>& color_override
)
{
    set_min_width(128_fpx);

    const std::array<std::pair<std::string, ImColor>, 3> labels{
        std::pair{"X", color_override.value_or(ImColor{220, 63, 63})},
        std::pair{"Y", color_override.value_or(ImColor{101, 201, 0})},
        std::pair{"Z", color_override.value_or(ImColor{64, 200, 232})}
    };

    set_gap(5_fpx);
    for (int i{}; i < 3; ++i) {
        const auto& [label, color]{labels[i]};
        std::tie(m_input[i], m_validator[i]) = emplace_coordinate_input(this, label, color);
    }
    append(small_label(suffix));

    for (int i{}; i < 3; ++i) {
        InputTextField* input{m_input[i]->input};
        input->callbacks().text_edited = [this, i]() { on_change(get_value(), i); };
    }
}

Domain::Vec3d TripleInput::get_value() const {
    return {
        m_validator[0]->value(),
        m_validator[1]->value(),
        m_validator[2]->value(),
    };
}

void TripleInput::set_value(const Domain::Vec3d& value)
{
    m_input[0]->input->set_text(fmt::format("{:.2f}", value.x()));
    m_input[1]->input->set_text(fmt::format("{:.2f}", value.y()));
    m_input[2]->input->set_text(fmt::format("{:.2f}", value.z()));
}

void TripleInput::set_visible(const std::array<bool, 3>& is_visible) {
    for (int i{}; i < 3; ++i) {
        m_input[i]->set_visible(is_visible[i]);
    }
}

} // namespace Slic3r::App::Plater
