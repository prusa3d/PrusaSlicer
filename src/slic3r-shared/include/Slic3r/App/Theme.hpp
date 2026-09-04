#pragma once

#include <Slic3r/Domain/Color.hpp>

#include "Slic3r/App/ThemeTypes.hpp"

#include <imgui.h>

#include <unordered_map>
#include <memory>

namespace Slic3r::App {

class Theme
{
public:
    enum class Style
    {
        Dark,
        Light,
    };

    explicit Theme(Style style);

    const Domain::ColorRGBA& color(
        Platform::Color color_id,
        Platform::ColorGroup group_id = Platform::ColorGroup::Default
    ) const;
    const ImColor& color_imgui(
        Platform::Color color_id,
        Platform::ColorGroup group_id = Platform::ColorGroup::Default
    ) const;

    void initialize_imgui_style();

private:
    struct ColorEntry
    {
        const Domain::ColorRGBA& color(Platform::ColorGroup group) const;
        const ImColor& color_imgui(Platform::ColorGroup group) const;

        ImColor color_default;
        std::unique_ptr<ImColor> color_disabled; ///< may be null
        std::unique_ptr<ImColor> color_hovered; ///< may be null
        std::unique_ptr<ImColor> color_active; ///< may be null
        std::unique_ptr<ImColor> color_active_disabled; ///< may be null
    };

    /**
     * @note Consider this helper only temporary, we should aim to declare all colors manually.
     * For dark backgrounds: hover/active are brighter (factor > 1).
     */
    ColorEntry auto_entry(const ImColor& color) const;

    /**
     * @note Variant of auto_entry for light backgrounds: hover/active are darker (factor < 1).
     */
    ColorEntry auto_entry_light(const ImColor& color) const;

    void initialize_dark_colors();
    void initialize_light_colors();

    void initialize_imgui_dark_style();
    void initialize_imgui_light_style();

    void set_style(Style style);

private:
    Style m_style{Style::Dark};
    std::unordered_map<Platform::Color, ColorEntry> m_colors;
};

} // namespace Slic3r::App
