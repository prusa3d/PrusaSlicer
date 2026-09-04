#pragma once

#include "Slic3r/App/Yoga/AbstractButton.hpp"
#include "Slic3r/App/Render/ImguiTypes.hpp"
#include "Slic3r/App/Yoga/RevertableControl.hpp"

#include <string>

namespace Slic3r::App::Yoga {

class Toggler;
class Text;

/**
 * @brief The ToggleButton class is a composite button combining a toggler and a text.
 * Use set_direction(YGDirectionLTR) to change the alignment of the internal controls.
 */
class ToggleButton : public AbstractButton, public Yoga::RevertableControl
{
public:
    explicit ToggleButton(const std::string& label = {}, const std::string& tooltip = {});

    void set_label(const std::string& label);
    const std::string& get_label() const;
    void set_font_type(Render::ImguiFontType font_type);

    void set_default(bool default_checked);
    bool is_changed_value() const override;
    void reset() override;

    bool third_state() const;
    void set_third_state(bool third_state);

protected:
    void checked_updated_internal() override;
    void hovered_updated_internal() override;

private:
    Toggler* m_toggler{nullptr};
    Text* m_label{nullptr};
    bool m_default_checked{false};
};

} // namespace Slic3r::App::Yoga
