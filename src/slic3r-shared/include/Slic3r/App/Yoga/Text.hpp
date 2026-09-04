#pragma once

#include "Slic3r/App/Yoga/Item.hpp"
#include "Slic3r/App/Render/ImguiTypes.hpp"

namespace Slic3r::App::Yoga {

class Text : public Item
{
public:
    enum class WrapMode
    {
        NoWrap, ///< No limitations for rendered text, min_size is defined by text itself
        Wrap, ///< Text is wrapped by words, limited by width, sets min_height
        WrapElide ///< Text is wrapped by words, limited by width & height, doesnt set anything
    };

    explicit Text(
        const std::string& text,
        Render::ImguiFontType font_type = Render::ImguiFontType::Regular
    );

    void render(const Vec2f& pos, const Vec2f& size) override;

    const std::string& text() const;
    void set_text(const std::string& text);

    const ImColor& text_color() const;
    void set_text_color(const ImColor& text_color);

    Render::ImguiFontType font_type() const;
    void set_font_type(Render::ImguiFontType font_type);

    Unit font_size() const;
    /**
     * @brief Sets explicit font size (in pixels) otherwise Root font size is used
     */
    void set_font_size(const Unit& font_size);

    WrapMode wrap_mode() const;
    void set_wrap_mode(WrapMode wrap_mode);

    const Align& align() const;
    void set_align(const Align& align);

protected:
    void on_resized() override;

    Vec2f get_item_size() override;

    float available_width() const;
    float available_height() const;

    float used_font_size() const;

private:
    void size_info_changed(const SizeInfo& size_info) override;

private:
    Align m_align;
    std::string m_source_text;
    std::string m_rendered_text;
    Text::WrapMode m_wrap_mode = Text::WrapMode::NoWrap;
    ImColor m_text_color;
    Render::ImguiFontType m_font_type = Render::ImguiFontType::Regular;
    std::optional<EvaluatedUnit> m_font_size;
    ImVec2 m_taken_size;
};

} // namespace Slic3r::App::Yoga
