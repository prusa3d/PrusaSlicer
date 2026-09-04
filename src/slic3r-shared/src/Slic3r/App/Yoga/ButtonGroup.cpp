#include "Slic3r/App/Yoga/ButtonGroup.hpp"

#include "Slic3r/App/Yoga/AbstractButton.hpp"

namespace Slic3r::App::Yoga {

ButtonGroup::~ButtonGroup()
{
    for (AbstractButton* button : std::as_const(m_buttons)) {
        button->callbacks().checked_changed = nullptr;
    }
}

ButtonGroup::ButtonGroup(std::initializer_list<AbstractButton*> initializer_list)
{
    set_buttons(initializer_list);
}

ButtonGroup::Callbacks& ButtonGroup::callbacks()
{
    return m_callbacks;
}

void ButtonGroup::set_buttons(std::initializer_list<AbstractButton*> initializer_list)
{
    m_checked_button = nullptr;
    for (AbstractButton* button : std::as_const(m_buttons)) {
        unset_button_callbacks(button);
    }

    m_buttons = initializer_list;

    for (AbstractButton* button : std::as_const(m_buttons)) {
        ASSERT(button);
        ASSERT(button->checkable());

        set_button_callbacks(button);
        if (button->checked()) {
            ASSERT(m_checked_button == nullptr, "Multiple checked buttons provided!");
            button->set_checked(true);
        }
    }

    if (!m_buttons.empty() && (m_checked_button == nullptr && m_always_checked)) {
        (*initializer_list.begin())->set_checked(true);
    }
}

AbstractButton* ButtonGroup::checked_button() const
{
    return m_checked_button;
}

void ButtonGroup::insert_button(AbstractButton* button)
{
    ASSERT(button);
    ASSERT(button->checkable());
    if (m_buttons.contains(button)) {
        return;
    }

    m_buttons.insert(button);

    set_button_callbacks(button);

    if ((m_checked_button == nullptr && m_always_checked) || button->checked()) {
        check_one_button(button);
    }
}

bool ButtonGroup::remove_button(AbstractButton* button)
{
    if (!m_buttons.contains(button)) {
        return false;
    }

    const bool last_button{m_buttons.size() == 1 && *m_buttons.begin() == button};

    button->callbacks().action          = nullptr;
    button->callbacks().checked_changed = nullptr;

    m_buttons.erase(button);

    if (last_button) {
        m_checked_button = nullptr;
    } else if (button == m_checked_button) {
        ASSERT(button->checked());
        check_one_button(m_always_checked ? *m_buttons.begin() : nullptr);
    }

    return true;
}

size_t ButtonGroup::button_count() const
{
    return m_buttons.size();
}

const ButtonGroup::Buttons& ButtonGroup::buttons() const
{
    return m_buttons;
}

void ButtonGroup::on_button_action(AbstractButton* button)
{
    if (m_callbacks.action) {
        m_callbacks.action(button);
    }
}

void ButtonGroup::on_button_secondary_action(AbstractButton* button)
{
    if (m_callbacks.secondary_action) {
        m_callbacks.secondary_action(button);
    }
}

void ButtonGroup::on_button_primary_pressed(AbstractButton* button, bool pressed)
{
    if (m_callbacks.pressed_primary) {
        m_callbacks.pressed_primary(button, pressed);
    }
}

void ButtonGroup::on_button_secondary_pressed(AbstractButton* button, bool pressed)
{
    if (m_callbacks.pressed_secondary) {
        m_callbacks.pressed_secondary(button, pressed);
    }
}

void ButtonGroup::check_one_button(AbstractButton* button)
{
    // If state is not changed, do not go further
    if (m_always_checked && m_checked_button == button && button->checked()) {
        return;
    }

    // Special unchecked state
    if (!m_always_checked && button->checked() == false) {
        button = nullptr;
    }

    m_checked_blocker = true;

    AbstractButton* last = m_checked_button;
    m_checked_button     = button;
    for (AbstractButton* owned_button : std::as_const(m_buttons)) {
        if (owned_button != button) {
            owned_button->set_checked(false);
        } else {
            owned_button->set_checked(true);
        }
    }
    m_checked_blocker = false;

    if (m_callbacks.checked_changed) {
        m_callbacks.checked_changed(button, last);
    }
}

void ButtonGroup::on_button_checked(AbstractButton* button)
{
    if (m_checked_blocker) {
        return;
    }

    check_one_button(button);
}

void ButtonGroup::set_button_callbacks(AbstractButton* button)
{
    button->callbacks().action           = [this, button]() { on_button_action(button); };
    button->callbacks().secondary_action = [this, button]() { on_button_secondary_action(button); };
    button->callbacks().checked_changed  = [this, button](bool checked)
    {
        // Intentionally ignored checked state.
        // The button cannot be unchecked if it is already checked.
        on_button_checked(button);
    };
    button->callbacks().pressed_primary_changed = [this, button](bool pressed)
    { on_button_primary_pressed(button, pressed); };
}

void ButtonGroup::unset_button_callbacks(AbstractButton* button)
{
    button->callbacks().action          = nullptr;
    button->callbacks().checked_changed = nullptr;
}

bool ButtonGroup::always_checked() const
{
    return m_always_checked;
}

void ButtonGroup::set_always_checked(bool always_checked)
{
    m_always_checked = always_checked;
}

} // namespace Slic3r::App::Yoga
