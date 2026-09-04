#pragma once

#include "Slic3r/App/Yoga/Item.hpp"

namespace Slic3r::App::Yoga {

class Window : public Item
{
public:
    struct Callbacks
    {
        std::function<void(bool hovered)> hovered_changed{nullptr};
    };

    explicit Window(const std::string& name);

    float rounding() const;
    void set_rounding(float rounding);

    int flags() const;
    void set_flags(int flags);

    float alpha() const;
    void set_alpha(float alpha);

    float border_size() const;
    void set_border_size(float border_size);
    void set_border_color(const std::optional<ImColor>& color);

    void render(const Vec2f& pos, const Vec2f& size) override final;
    /**
     * @brief render_body by default will render all Window children
     * but you can override it and process the children yourself
     */
    virtual void render_body(const Vec2f& pos, const Vec2f& size);

    bool is_in_window() const override;

    bool position_by_yoga() const;
    void set_position_by_yoga(bool position_by_yoga);
    void request_position(const Vec2f& position);
    void bring_to_front();

    Callbacks& callbacks();

    bool hovered() const;

    bool is_modal() const;
    void set_modal(bool is_modal);

private:
    Callbacks m_callbacks;

    int m_flags = ImGuiWindowFlags_NoDecoration
        | ImGuiWindowFlags_NoMove
        | ImGuiWindowFlags_NoBringToFrontOnFocus
        | ImGuiWindowFlags_NoFocusOnAppearing;
    float m_rounding = 5;

    float m_alpha = 1.f;

    float m_border_size = 0.f;
    std::optional<ImColor> m_border_color;

    bool m_position_by_yoga = true;
    std::optional<Vec2f> m_requested_position;
    Vec2f m_last_pos;
    bool m_hovered = false;
    bool m_requested_bring_to_front = false;

    bool m_is_modal{ false };
};

} // namespace Slic3r::App::Yoga
