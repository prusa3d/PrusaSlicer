#pragma once

#include "Slic3r/App/Yoga/Item.hpp"
#include "Slic3r/App/Yoga/Tooltip.hpp"
#include "Slic3r/App/Yoga/DnDPayload.hpp"

#include <unordered_set>

namespace Slic3r::App::Yoga {

class AbstractButton : public Item
{
public:
    /**
     * @brief The Callbacks class - All actions are always consumed user-side
     */
    struct Callbacks
    {
        std::function<void()> action{nullptr};
        std::function<void()> secondary_action{nullptr};
        std::function<void(bool hovered)> hovered_changed{nullptr};
        std::function<void(bool pressed)> pressed_primary_changed{nullptr};
        std::function<void(bool pressed)> pressed_secondary_changed{nullptr};
        std::function<void(bool checked)> checked_changed{nullptr};
        /**
         * @brief DragNDrop payload has been dropped onto this button and accepted
         */
        std::function<void(const DnDPayload& dnd_payload)> dnd_accepted{nullptr};
        /**
         * @brief There is ongoing DnDPayload (somewhere on the screen) which has compatible key
         */
        std::function<void(bool could_accept)> dnd_key_accepted_changed{nullptr};
        /**
         * @brief There is ongoing DnDPayload (directly on top of this button) which has compatible key
         */
        std::function<void(bool could_accept)> dnd_could_accept_changed{nullptr};
    };

    AbstractButton(const std::string& tooltip = {}, const std::string& name = {});

    void style_node() override;

    void render(const Vec2f& pos, const Vec2f& size) override;

    Callbacks& callbacks();

    const std::string& shortcut() const;
    void set_shortcut(const std::string& shortcut);

    void set_tooltip(const std::string& tooltip);
    void set_tooltip_position(Yoga::Position position);

    bool has_arrow() const;
    void set_has_arrow(bool has_arrow);

    bool checkable() const;
    void set_checkable(bool checkable);

    bool checked() const;
    void set_checked(bool checked);

    bool hovered() const;

    bool pressed() const;

    bool allow_overlap() const;
    void set_allow_overlap(bool allow_overlap);

    ImGuiMouseCursor cursor() const;
    void set_cursor(ImGuiMouseCursor cursor);

    ImGuiMouseButton primary_button() const;
    void set_primary_button(ImGuiMouseButton primary_button);

    ImGuiMouseButton secondary_button() const;
    void set_secondary_button(ImGuiMouseButton secondary_button);

    bool draggable() const;
    void set_draggable(bool draggable);

    bool droppable() const;
    void set_droppable(bool droppable);

    const std::unordered_set<std::string>& dnd_key() const;
    void set_accepted_keys(const std::unordered_set<std::string>& accepted_keys);

    const std::optional<DnDPayload>& dnd_payload() const;
    void set_dnd_payload(const DnDPayload& dnd_payload);

protected:
    virtual void checked_updated_internal();
    virtual void hovered_updated_internal();
    virtual void pressed_primary_updated_internal();
    virtual void pressed_secondary_updated_internal();
    virtual void action_internal();
    virtual void secondary_action_internal();
    virtual void dnd_accepted_internal(const DnDPayload& dnd_payload);
    virtual void dnd_key_accepted_changed_internal(bool could_accept);
    virtual void dnd_could_accept_changed_internal(bool could_accept);
    virtual void shortcut_updated_internal();

    void enabled_updated_internal() override;
    void visible_updated_internal() override;

    Platform::ColorGroup button_color_group() const;

private:

    void set_hovered(bool hovered);
    void set_pressed_primary(bool pressed);
    void set_pressed_secondary(bool pressed);
    void update_flags();

protected:
    Tooltip* m_tooltip = nullptr;

private:
    bool m_has_arrow         = false;
    bool m_checkable         = false;
    bool m_checked           = false;
    bool m_hovered           = false;
    bool m_pressed_primary   = false;
    bool m_pressed_secondary = false;
    bool m_allow_overlap     = false;

    // DnD
    bool m_draggable          = false;
    bool m_droppable          = false;
    bool m_drag_source_active = false;
    std::unordered_set<std::string> m_accepted_keys;
    std::optional<DnDPayload> m_dnd_payload;

    bool m_accept_dnd_key    = false;
    bool m_could_accept_drop = false;

    std::string m_shortcut;
    Callbacks m_callbacks;
    ImGuiButtonFlags m_flags = ImGuiButtonFlags_MouseButtonLeft
        | ImGuiButtonFlags_MouseButtonRight
        | ImGuiButtonFlags_EnableNav;
    ImGuiMouseButton m_primary_button   = ImGuiMouseButton_Left;
    ImGuiMouseButton m_secondary_button = ImGuiMouseButton_Right;
    ImGuiMouseCursor m_cursor           = ImGuiMouseCursor_Arrow;
};

} // namespace Slic3r::App::Yoga
