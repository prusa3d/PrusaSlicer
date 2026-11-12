#pragma once

#include <string>
#include "Slic3r/App/Yoga/AbstractButton.hpp"
#include "Slic3r/App/Yoga/StrikethroughText.hpp"

namespace Slic3r::App {

class ConfigRowItemLabel : public Yoga::AbstractButton
{
public:
    ConfigRowItemLabel(const std::string& label);
    Yoga::StrikethroughText* label_object() const;
    void set_label_color(const ImColor& color);

protected:
    void hovered_updated_internal();

private:
    Yoga::StrikethroughText* m_text;
    ImColor m_label_color;
};

} // namespace Slic3r::App
