#pragma once

#include "Slic3r/App/Yoga/Window.hpp"
#include "Slic3r/App/Plater/GizmoHelpFactory.hpp"
#include "Slic3r/App/Yoga/ButtonGroup.hpp"

namespace Slic3r::App::Yoga {
class Item;
class Icon;
class Text;
class LayoutButton;
class ToggleButton;
class Separator;
class InputTextWithSpin;
class SliderWithInput;
class ComboBox;
class ScrollArea;
} // namespace Slic3r::App::Yoga

namespace Slic3r::App {
class WarningPanel;
}

namespace Slic3r::App::Plater {

class GizmoWindow : public Yoga::Window
{
public:
    struct GizmoCallbacks
    {
        std::function<void()> close_requested{nullptr};
        std::function<void()> revert_requested{nullptr};
    };

    GizmoWindow();

    void set_title(const std::string& title);
    void set_shortcut(const std::string& shortcut);
    void set_icon(Render::Icon icon);

    GizmoCallbacks& gizmo_callbacks();

protected:
    /*
     * @brief Add the separator into the specified item rather than into Dialog::context()
     */
    Yoga::Separator* add_separator(Item* item);

    float gap_size() const;
    float preffered_max_width() const;
    Item* add_new_row(
        const std::string& title,
        Yoga::ItemPtr controls,
        YGAlign label_align = YGAlignCenter
    );

    Item* content() const;

    Yoga::LayoutButton* close_button() const;
    Yoga::LayoutButton* revert_button() const;
    Yoga::Item* top_bar() const;
    Yoga::Item* bottom_bar() const;

    /**
     * @brief Adds a revert button to the given parent item.
     *
     * @param parent  Parent UI item.
     * @param tooltip Tooltip text for the revert button.
     * @return Pointer to the created LayoutButton.
     */
    Yoga::LayoutButton* add_revert_btn(Item* parent, const std::string& tooltip);

    /**
     * @brief Adds a wrapper item with flex-shrink enabled.
     *
     * @param parent Parent UI item.
     * @return Pointer to the created Item.
     */
    Item* add_flex_shrinked_wrap(Item* parent);

    Item* add_non_shrinked_wrap(
        Item* parent,
        Yoga::Orientation orientation,
        const Yoga::Unit& gap
    );

    /**
     * @brief Creates a row item with a bold text label.
     *
     * @param parent Parent UI item.
     * @param label  Text label displayed in the row.
     * @return Pointer to the created row Item.
     */
    Item* add_labeled_row(Item* parent, const std::string& label);

    /**
     * @brief Adds a row with an integer spin input.
     *
     * Optionally adds a revert button if @p revert_button_tooltip is not empty.
     *
     * @param title                   Row title.
     * @param parent                  Parent UI item.
     * @param input                   Output pointer to the created input control.
     * @param unit                    Unit label displayed next to the input.
     * @param revert_button_tooltip   Tooltip for the revert button (if empty, button is not created).
     * @param min                     Minimum allowed value.
     * @param max                     Maximum allowed value.
     * @return Pointer to the created row Item.
     */
    Item* add_row_with_spin_int(
        const std::string& title,
        Yoga::Item* parent,
        Yoga::InputTextWithSpin** input,
        const std::string& unit,
        const std::string& revert_button_tooltip,
        int min,
        int max
    );

    /**
     * @brief Adds a row with a double-precision spin input.
     *
     * Optionally adds a revert button if @p revert_button_tooltip is not empty.
     *
     * @param title                   Row title.
     * @param parent                  Parent UI item.
     * @param input                   Output pointer to the created input control.
     * @param unit                    Unit label displayed next to the input.
     * @param revert_button_tooltip   Tooltip for the revert button (if empty, button is not created).
     * @param min                     Minimum allowed value.
     * @param max                     Maximum allowed value.
     * @param step                    Increment step.
     * @param step_fast               Fast increment step.
     * @return Pointer to the created row Item.
     */
    Item* add_row_with_spin_double(
        const std::string& title,
        Yoga::Item* parent,
        Yoga::InputTextWithSpin** input,
        const std::string& unit,
        const std::string& revert_button_tooltip,
        double min,
        double max,
        double step,
        double step_fast
    );

    /**
     * @brief Adds a row with a slider and input field.
     *
     * Optionally adds a revert button if @p revert_tooltip is not empty.
     *
     * @param parent         Parent UI item.
     * @param slider         Output pointer to the created slider control.
     * @param name           Name/label of the slider.
     * @param unit           Unit label displayed next to the slider.
     * @param revert_tooltip Tooltip for the revert button (if empty, button is not created).
     * @return Pointer to the created row Item.
     */
    Item* add_row_with_slider(
        Item* parent,
        Yoga::SliderWithInput** slider,
        const std::string& name,
        const std::string& unit = std::string(),
        const std::string& revert_tooltip = std::string()
    );

    /**
     * @brief Adds a row with a combobox.
     *
     * Optionally adds a revert button if @p revert_tooltip is not empty.
     *
     * @param title          Row title.
     * @param parent         Parent UI item.
     * @param combo          Output pointer to the created ComboBox control.
     * @param revert_tooltip Tooltip for the revert button (if empty, button is not created).
     * @return Pointer to the created row Item.
     */
    Item* add_row_with_combo_box(
        const std::string& title,
        Item* parent,
        Yoga::ComboBox** combo,
        const std::string& revert_tooltip = std::string()
    );

    /**
     * @brief Adds a row with a toggle.
     *
     * Optionally adds a revert button if @p revert_tooltip is not empty.
     *
     * @param title          Row title.
     * @param parent         Parent UI item.
     * @param toggle         Output pointer to the created ToggleButton control.
     * @param revert_tooltip Tooltip for the revert button (if empty, button is not created).
     * @return Pointer to the created row Item.
     */
    Item* add_row_with_toggle_button(
        const std::string& title,
        Item* parent,
        Yoga::ToggleButton** toggle,
        const std::string& revert_tooltip = std::string()
    );

    /**
     * @brief Adds row with LayoutButtom aligned to the left
     *
     * @param parent Parent UI item
     * @param button Output pointer to the created button
     * @param label Button label
     * @param tooltip Button tooltip
     * @param icon Button icon
     * 
     * @return Pointer to the created row item
     */
    Item* add_row_with_button(
        Item* parent,
        Yoga::LayoutButton** button,
        const std::string& label,
        const std::string& tooltip = std::string(),
        Render::Icon icon          = Render::Icon::None
    );

    /**
     * @brief Adds square icon button
     *
     * @param parent Parent UI item
     * @param icon Button icon
     * @parem tooltip Button Tooltip
     */
    Yoga::LayoutButton*
    add_icon_button(Item* parent, Render::Icon icon, const std::string& tooltip = std::string());

    void set_warning(const std::string& title, const std::string& text);
    void set_warning(const std::string& title, const std::vector<std::string>& errors);
    void clear_warning();

protected:
    GizmoHelpFactory m_help_factory;

    float m_label_width = 85.f;

private:
    GizmoCallbacks m_gizmo_callback;

    Yoga::Icon* m_icon{nullptr};
    Yoga::Text* m_title_text{nullptr};
    Yoga::Text* m_shortcut_text{nullptr};

    Yoga::LayoutButton* m_close_button  = nullptr;
    Yoga::LayoutButton* m_revert_button = nullptr;
    Yoga::Item* m_top_bar               = nullptr;
    Yoga::Item* m_bottom_bar            = nullptr;
    WarningPanel* m_warning_panel       = nullptr;
    Yoga::ScrollArea* m_content         = nullptr;
    size_t m_current_tab_index          = 0;

    std::vector<Yoga::LayoutButton*> m_tab_buttons;
    Yoga::ButtonGroup m_tab_button_group;
};

using GizmoWindowPtr = std::unique_ptr<GizmoWindow>;

} // namespace Slic3r::App::Plater
