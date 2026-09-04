
#include "Slic3r/App/Yoga/Icon.hpp"

#include <Slic3r/App/Render/Texture.hpp>
#include <Slic3r/App/Render/ImguiRender.hpp>
#include "Slic3r/Domain/Size.hpp"

namespace Slic3r::App::Yoga {

std::unordered_map<std::string, std::string> Icon::s_replace_strings;

Icon::Icon(Render::Icon icon, PreferredSize explicit_max_size) :
    Icon(icon, static_cast<int>(explicit_max_size))
{}

Icon::Icon(Render::Icon icon, int explicit_max_size) :
    m_auto_resize(false),
    m_max_texture_size(explicit_max_size)
{
    set_icon(icon);
}

Icon::Icon(Render::Icon icon) : m_icon(icon) {}

void Icon::render(const Vec2f& pos, const Vec2f& size)
{
    render_item_begin(pos, size);

    if (((m_icon_type == IconType::Icon && m_icon != Render::Icon::None)
         || (m_icon_type == IconType::Image && !m_image.empty()))
        && (!Domain::fuzzy_compare(m_cached_size.x(), size.x())
            || !Domain::fuzzy_compare(m_cached_size.y(), size.y())))
    {
        m_cached_size = size;
        if (m_auto_resize) {
            int new_max_size = std::max(size.x(), size.y());
            if (m_max_texture_size != new_max_size) {
                m_max_texture_size = new_max_size;
                update_texture();
            }
        }
        update_draw_sizes();
    }

    ImGui::SetCursorScreenPos(pixel_round(to_im(pos)));
    if (m_texture) {
        constexpr ImVec2 uv0{0, 0};
        constexpr ImVec2 uv1{1, 1};
        constexpr ImVec4 bg_color{0, 0, 0, 0};

        ImGui::SetCursorScreenPos(pixel_round(to_im(pos) + m_offset));

        if (enabled()) {
            render_image(m_texture, m_draw_size, uv0, uv1, bg_color, tint(), m_rounding.result);
        } else {
            render_image(
                m_disabled_texture ? m_disabled_texture : m_texture,
                m_draw_size,
                uv0,
                uv1,
                bg_color,
                m_theme->color_imgui(Platform::Color::Text, Platform::ColorGroup::Disabled),
                m_rounding.result
            );
        }
    }

    render_item_end(pos, size);
}

Icon::IconType Icon::icon_type() const
{
    return m_icon_type;
}

Render::Icon Icon::icon() const
{
    return m_icon_type == IconType::Icon ? m_icon : Render::Icon::None;
}

void Icon::set_icon(Render::Icon icon)
{
    if (m_icon == icon && m_icon_type == IconType::Icon) {
        return;
    }

    m_icon      = icon;
    m_icon_type = IconType::Icon;
    update_texture();
    update_draw_sizes();
    set_style_dirty();
}

std::string Icon::image() const
{
    return m_icon_type == IconType::Image ? m_image : std::string();
}

void Icon::set_image(const std::string& image, bool force_reload)
{
    if (!force_reload && m_image == image && m_icon_type == IconType::Image) {
        return;
    }

    if (force_reload && !image.empty() && m_imgui_render) {
        m_imgui_render->invalidate_image_texture(image);
    }

    m_image     = image;
    m_icon_type = IconType::Image;
    update_texture();
    update_draw_sizes();
}

const Vec2f& Icon::source_size() const
{
    return m_source_size;
}

void Icon::set_source_size(const Vec2f& source_size)
{
    m_source_size = source_size;
}

Icon::FillMode Icon::fill_mode() const
{
    return m_fill_mode;
}

void Icon::set_fill_mode(FillMode fill_mode)
{
    if (m_fill_mode != fill_mode) {
        m_fill_mode = fill_mode;
        if (m_texture) {
            update_draw_sizes();
        }
    }
}

ImColor Icon::tint() const
{
    return {m_tint};
}

void Icon::set_tint(const ImColor& new_tint)
{
    if (ImU32(tint()) != ImU32(new_tint)) {
        m_tint = new_tint;
        if (m_tint != ImVec4(1, 1, 1, 1)) {
            update_texture();
        }
        set_style_dirty();
    }
}

void Icon::set_replace_strings(const std::unordered_map<std::string, std::string>& replace_strings)
{
    s_replace_strings = replace_strings;
}

void Icon::update_draw_sizes()
{
    if (!m_texture) {
        return;
    }

    if (m_cached_size.isZero()) {
        return;
    }

    if (m_fill_mode == FillMode::Stretch) {
        m_draw_size = to_im(m_cached_size);
    } else {
        Domain::Size size(m_texture->width(), m_texture->height());
        size.scale(Domain::Size(m_cached_size.x(), m_cached_size.y()));
        m_draw_size.x = size.width;
        m_draw_size.y = size.height;
        if (m_fill_mode == FillMode::PreservedAspectCentered) {
            m_offset.x = pixel_round((m_cached_size.x() - size.width) * 0.5f);
            m_offset.y = pixel_round((m_cached_size.y() - size.height) * 0.5f);
        } else { // FillMode::PreservedAspect
            m_offset.x = 0;
            m_offset.y = 0;
        }
    }
}

void Icon::update_texture()
{
    if (m_icon_type == IconType::Icon) {
        if (m_icon == Render::Icon::None || m_max_texture_size == 0 || !m_imgui_render) {
            m_texture = nullptr;
        } else {
            constexpr ImVec4 WhiteColor{1, 1, 1, 1};
            const bool colors_preserved = m_preserve_colors || m_tint != WhiteColor;
            m_texture                   = m_imgui_render->icon_texture(
                m_icon,
                m_max_texture_size,
                colors_preserved ? std::unordered_map<std::string, std::string>{} :
                                   s_replace_strings
            );
            m_disabled_texture = colors_preserved ?
                m_texture :
                m_imgui_render->icon_texture(m_icon, m_max_texture_size);
        }
    } else if (m_icon_type == IconType::Image) {
        if (m_image.empty() || m_max_texture_size == 0) {
            m_texture = nullptr;
        } else {
            m_texture = m_imgui_render->image_texture(m_image, m_max_texture_size);
        }
    }
    if (m_texture) {
        // There is a chance that Icon::set_icon would be called several times in a single
        // loop before Icon::render, in those cases, let us cache all images that we are
        // creating, all unused ones will be cleared in the next frame.
        m_imgui_render->use_texture(m_texture);
    }
    if (m_disabled_texture) {
        m_imgui_render->use_texture(m_disabled_texture);
    }
}

void Icon::size_info_changed(const SizeInfo& info_size)
{
    m_rounding.evaluate(info_size);
    update_texture();
}

bool Icon::preserve_colors() const
{
    return m_preserve_colors;
}

void Icon::set_preserve_colors(bool preserve_colors)
{
    if (m_preserve_colors != preserve_colors) {
        m_preserve_colors = preserve_colors;
        update_texture();
        set_style_dirty();
    }
}

void Icon::set_rounding(Unit rounding)
{
    if (m_rounding.source != rounding) {
        m_rounding = EvaluatedUnit{rounding};
        set_style_dirty();
    }
}
} // namespace Slic3r::App::Yoga
