#pragma once


#include "Slic3r/App/Yoga/Item.hpp"

namespace Slic3r::App::Yoga {
    class InputTextField;
    class DoubleValidator;
    class Text;
}

namespace Slic3r::App::Plater {

class InputWithLabel;

class TripleInput : public Yoga::Item
{
public:
    TripleInput(const std::string& suffix, const std::optional<ImColor>& color_override = {});
    std::function<void(const Domain::Vec3d&, int)> on_change = [](const Domain::Vec3d&, int index) {};

    Domain::Vec3d get_value() const;
    void set_value(const Domain::Vec3d& value);
    void set_visible(const std::array<bool, 3>& is_visible);

private:
    std::array<InputWithLabel*, 3> m_input;
    std::array<Yoga::DoubleValidator*, 3> m_validator;
};
} // namespace Slic3r::App::Plater
