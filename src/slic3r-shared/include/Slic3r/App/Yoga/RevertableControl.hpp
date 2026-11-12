#pragma once

namespace Slic3r::App::Yoga {

class LayoutButton;

class RevertableControl
{
public:
    virtual ~RevertableControl();

    void set_revert_button(LayoutButton* button);
    LayoutButton* revert_button() const;

    /**
     * @note A revertible control can have a state in which we don't want to 
     * compare it with the default value, and as a result, the revert button 
     * should not be shown.
     * Each derived control can define its own type of default value. 
     * Therefore, a flag is used to indicate whether the default value is valid.
     *
     * @return if default value is valid
     */
    bool has_valid_default() const;
    /**
     * Set state for the default value
     */
    virtual void validate_default(bool is_valid);

protected:
    virtual bool is_changed_value() const;

    virtual void reset() {}

    void update_revert_button();

private:
    LayoutButton* m_revert_button{nullptr};
    bool m_valid_default{true};
};

} // namespace Slic3r::App::Yoga
