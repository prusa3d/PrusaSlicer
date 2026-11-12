
#include "Slic3r/App/Yoga/RevertableControl.hpp"
#include "Slic3r/App/Yoga/LayoutButton.hpp"

namespace Slic3r::App::Yoga {

RevertableControl::~RevertableControl() {}

void RevertableControl::set_revert_button(LayoutButton* button)
{
    m_revert_button                     = button;
    m_revert_button->callbacks().action = [this]() {
        reset();
        update_revert_button();
    };
}

LayoutButton* RevertableControl::revert_button() const
{
    return m_revert_button;
}

bool RevertableControl::has_valid_default() const
{
    return m_valid_default;
}

void RevertableControl::validate_default(bool is_valid)
{
    if (m_valid_default != is_valid) {
        m_valid_default = is_valid;
        update_revert_button();
    }
}

bool RevertableControl::is_changed_value() const
{
    return false;
}

void RevertableControl::update_revert_button()
{
    if (m_revert_button) {
        m_revert_button->set_visible(m_valid_default && is_changed_value());
    }
}
} // namespace Slic3r::App::Yoga
