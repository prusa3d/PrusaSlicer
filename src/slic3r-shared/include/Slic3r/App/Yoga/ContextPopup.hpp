#pragma once

#include "Slic3r/App/Yoga/Item.hpp"

namespace Slic3r::App::Yoga {

/**
 * @warning This class contains a content_item() where all
 * children are parented
 */
class ContextPopup : public Item
{
public:
    struct Callbacks
    {
        std::function<void()> opened{nullptr};
        std::function<void()> closed{nullptr};
    };

    explicit ContextPopup(const std::string& name = {});

    void insert(ObjectPtr child, size_t index) override;
    void append(ObjectPtr child) override;
    ObjectPtr remove(Object* child) override;

    Callbacks& callbacks();

    void style_node() override;

    void render(const Vec2f& pos, const Vec2f& size) override;

    float offset() const;
    void set_offset(float offset);

    std::optional<Vec2f> open_pos() const;
    void set_open_pos(std::optional<Vec2f> pos);

    Position position() const;
    void set_position(Position position);

    float rounding() const;
    void set_rounding(float rounding);

    void open();
    void open(Vec2f pos);
    void close();
    bool opened() const;

    ImGuiWindowFlags flags() const;
    void set_flags(ImGuiWindowFlags flags);

    const ImColor& background_color() const;
    void set_background_color(const ImColor& background_color);

    Item* content_item() const;

private:
    void invalidate_style();

private:
    Callbacks m_callbacks;

    Item* m_content_item = nullptr;

    float m_offset      = 10;
    Position m_position = Position::Right;
    float m_rounding    = 5;

    ImGuiWindowFlags m_flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove;
    bool m_request_close     = false;
    bool m_opened            = false;

    ImColor m_background_color = IM_COL32_WHITE;

    std::optional<Vec2f> m_open_pos   = std::nullopt;
    bool m_force_open_popup_in_render = false;
};

} // namespace Slic3r::App::Yoga
