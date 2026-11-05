#pragma once

#include "Slic3r/App/Yoga/Popup.hpp"
#include "Slic3r/App/Yoga/ButtonGroup.hpp"
#include "Slic3r/App/Yoga/DialogGeometry.hpp"

namespace Slic3r::App::Yoga {

class Text;
class LayoutButton;
class AbstractButton;
class Separator;

class Dialog : public Popup
{
public:
    struct DialogCallbacks
    {
        std::function<void(size_t current_tab)> tab_selected{nullptr};
        std::function<void()> close_requested{nullptr};
    };

    explicit Dialog(const std::string& name = {});
    explicit Dialog(std::initializer_list<std::string> tabs, const std::string& name = {});

    DialogCallbacks& dialog_callbacks();

    bool closable() const;
    void set_closable(bool closable);

    size_t current_tab_index() const;
    void set_current_tab(size_t current_index);
    void set_tab_enabled(size_t index, bool enabled);

    float tab_flex_grow() const;
    /**
     * @brief Tabs with flex grow fill the whole top row, the button area shrinks to its buttons
     */
    void set_tab_flex_grow(float flex_grow);

    bool resizable() const;
    /**
     * @brief Resizable dialog can be resized by its edges, implies movable and adds maximize button
     * @note Resizable dialog has to set min width and min height of its content_item()
     */
    void set_resizable(bool resizable);

    bool movable() const;
    /**
     * @brief Movable dialog can be dragged by its header
     */
    void set_movable(bool movable);

    bool pinnable() const;
    /**
     * @brief Pinnable (and movable) dialog shows pin button which persists its geometry
     * in the RootItem dialog geometry store under the dialog base object name
     */
    void set_pinnable(bool pinnable);

    /**
     * @brief Maximized dialog fills the whole available screen
     */
    bool maximized() const;
    /**
     * @brief Pinned dialog persists its geometry in the RootItem dialog geometry store
     */
    bool pinned() const;

    void render(const Vec2f& pos, const Vec2f& size) override;

protected:
    Item* content() const;
    LayoutButton* close_button() const;
    LayoutButton* maximize_button() const;
    LayoutButton* pin_button() const;
    Separator* add_separator();
    LayoutButton* append_tab(const std::string& tab);
    /**
     * @brief Size of the dialog when it opens without pinned geometry, std::nullopt sizes by content
     * @note Resizable dialog returns to its implicit size when closed
     */
    void set_implicit_size(const std::optional<Unit>& width, const std::optional<Unit>& height);
    void remove_tab(size_t index);

    virtual void on_tab_selected(int current_index);

    virtual void close_action();

private:
    void load_geometry() override;
    void reset_geometry() override;
    void set_current_tab_internal(size_t current_index);
    IDialogGeometryStore* geometry_store() const;
    void set_maximized(bool maximized);
    void set_pinned_internal(bool pinned);
    void update_pin_state(bool pinned);
    void update_geometry_controls();
    void user_geometry_changed();
    DialogGeometry current_geometry() const;
    void apply_geometry(const DialogGeometry& geometry);
    void set_window_geometry(const DialogGeometry& geometry);
    void apply_implicit_size();
    void set_maximized_internal(bool maximized, const DialogGeometry& restore_geometry);

private:
    bool m_closable  = true;
    float m_tab_flex_grow = 0.f;
    bool m_resizable = false;
    bool m_movable   = false;
    bool m_pinnable  = false;
    bool m_maximized = false;
    bool m_pinned    = false;
    bool m_geometry_dirty = false;
    DialogGeometry m_restore_geometry;

    struct ImplicitSize
    {
        std::optional<Unit> width;
        std::optional<Unit> height;
    };
    std::optional<ImplicitSize> m_implicit_size;

    DialogCallbacks m_callbacks;

    LayoutButton* m_close_button    = nullptr;
    LayoutButton* m_maximize_button = nullptr;
    LayoutButton* m_pin_button      = nullptr;
    AbstractButton* m_drag_handle   = nullptr;
    Item* m_content              = nullptr;
    Item* m_top_row              = nullptr;
    Item* m_tab_container        = nullptr;
    Item* m_buttons_rect         = nullptr;
    size_t m_current_tab_index   = 0;

    std::vector<LayoutButton*> m_tab_buttons;
    ButtonGroup m_tab_button_group;
};

} // namespace Slic3r::App::Yoga
