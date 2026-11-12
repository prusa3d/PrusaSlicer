#pragma once

#include <set>
#include <functional>
#include <initializer_list>

namespace Slic3r::App::Yoga {

class AbstractButton;

/**
 * @brief The ButtonGroup class - for grouping button and creating radio button like
 * behavior. If buttons inserted into ButtonGroup are checkable, they will become
 * mutually exclusive.
 * Several internal callbacks will be available, pointer to event origin will also
 * be passed in those callbacks.
 */
class ButtonGroup
{
public:
    struct Callbacks
    {
        std::function<void(AbstractButton*)> action{nullptr};
        std::function<void(AbstractButton*)> secondary_action{nullptr};
        std::function<void(AbstractButton* button, bool pressed)> pressed_primary{nullptr};
        std::function<void(AbstractButton* button, bool pressed)> pressed_secondary{nullptr};
        std::function<void(AbstractButton* current_checked, AbstractButton* last_checked)>
            checked_changed{nullptr};
    };

    /**
     * @warning action and checked_changed callbacks will be overwritten
     */
    ButtonGroup(std::initializer_list<AbstractButton*> initializer_list = {});
    virtual ~ButtonGroup();

    Callbacks& callbacks();

    /**
     * @warning action and checked_changed callbacks will be overwritten
     */
    void set_buttons(std::initializer_list<AbstractButton*> initializer_list);

    AbstractButton* checked_button() const;

    void insert_button(AbstractButton* button);
    bool remove_button(AbstractButton* button);
    size_t button_count() const;

    const std::set<AbstractButton*>& buttons() const;

    bool always_checked() const;
    void set_always_checked(bool always_checked);

private:
    void on_button_action(AbstractButton* button);
    void on_button_secondary_action(AbstractButton* button);
    void on_button_checked(AbstractButton* button);
    void on_button_primary_pressed(AbstractButton* button, bool pressed);
    void on_button_secondary_pressed(AbstractButton* button, bool pressed);
    void check_one_button(AbstractButton* button);

    void set_button_callbacks(AbstractButton* button);
    void unset_button_callbacks(AbstractButton* button);

private:
    Callbacks m_callbacks;

    using Buttons = std::set<AbstractButton*>;

    Buttons m_buttons;
    AbstractButton* m_checked_button = nullptr;
    bool m_checked_blocker           = false;
    bool m_always_checked = true; ///< if true, at least one button has to be checked at all times
};

} // namespace Slic3r::App::Yoga
