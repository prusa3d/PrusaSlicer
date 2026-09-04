#include "Slic3r/App/Yoga/Item.hpp"

#include <functional>
#include <fmt/format.h>

namespace Slic3r::App::Yoga {

#ifdef DEBUG

Item* Item::m_debug_item = nullptr;

namespace {

constexpr float default_font_size       = 14.f;
constexpr float hierarchy_font_size     = 12.f;
constexpr float diagram_label_font_size = 12.f;
constexpr float label_spacing           = 2.f;

constexpr ImVec2 panel_padding     = {6.f, 4.f};
constexpr ImVec2 hierarchy_padding = {6.f, 3.f};
constexpr float section_gap        = 4.f;

constexpr ImU32 header_color           = IM_COL32(255, 180, 60, 255);
constexpr ImU32 text_color             = IM_COL32(220, 220, 220, 255);
constexpr ImU32 margin_color           = IM_COL32(246, 178, 107, 230);
constexpr ImU32 padding_color          = IM_COL32(147, 196, 125, 230);
constexpr ImU32 content_color          = IM_COL32(180, 220, 255, 255);
constexpr ImU32 border_color           = IM_COL32(255, 60, 60, 220);
constexpr ImU32 margin_fill_color      = IM_COL32(246, 178, 107, 90);
constexpr ImU32 padding_fill_color     = IM_COL32(147, 196, 125, 90);
constexpr ImU32 content_fill_color     = IM_COL32(97, 150, 218, 70);
constexpr ImU32 panel_fill_color       = IM_COL32(0, 0, 0, 160);
constexpr ImU32 panel_border_color     = IM_COL32(255, 100, 0, 180);
constexpr ImU32 hierarchy_border_color = IM_COL32(200, 200, 200, 130);
constexpr ImU32 hierarchy_fill_color   = IM_COL32(255, 255, 255, 12);

// One line of overlay text with the color it should be drawn in.
struct TextLine
{
    std::string text;
    ImU32 color;
};

struct TextBlockMetrics
{
    int line_count  = 0;
    float max_width = 0.f;
};

// Evaluated pixel values (or scaled diagram lengths) for the four sides of a box.
struct EdgeMetrics
{
    float left = 0.f, top = 0.f, right = 0.f, bottom = 0.f;
};

enum class HorizontalAlign { Left, Center };

// A self-contained, independently sized piece of the overlay: knows its own footprint up front
// (computed once, while the right font is active) and later draws itself at a position chosen
// by the caller. New overlay content is added by appending another section, never by touching
// the layout math of existing ones.
struct OverlaySection
{
    ImVec2 size;
    HorizontalAlign align = HorizontalAlign::Left;
    std::function<void(ImDrawList*, ImVec2 top_left)> draw;
};

std::string_view unit_type_suffix(Unit::Type type)
{
    switch (type) {
    case Unit::Type::Pixel:
        return "px";
    case Unit::Type::FigmaPixel:
        return "fpx";
    case Unit::Type::Point:
        return "pt";
    case Unit::Type::Rem:
        return "rem";
    case Unit::Type::ViewportWidth:
        return "vw";
    case Unit::Type::ViewportHeight:
        return "vh";
    case Unit::Type::ViewportMin:
        return "vmin";
    case Unit::Type::ViewportMax:
        return "vmax";
    }
    return "?";
}

std::string format_unit_value(const Unit& source, float result)
{
    if (std::isnan(source.value) || std::isnan(result)) {
        return "(unset)";
    } else if (source.type == Unit::Type::Pixel) {
        return fmt::format("{:.4g}px", source.value);
    } else {
        return fmt::format("{:.4g}{}={:.4g}", source.value, unit_type_suffix(source.type), result);
    }
}

std::string format_evaluated_unit(const EvaluatedUnit& evaluated_unit)
{
    return format_unit_value(evaluated_unit.source, evaluated_unit.result);
}

void draw_alive_indicator(ImVec2 position)
{
    static float angle = 0.0f;
    angle += 0.13f; // ~7.5 degrees per frame

    const ImVec2 center{position.x + 8.0f, position.y + 8.0f};
    const auto draw_spoke = [&](float spoke_angle)
    {
        const ImVec2 offset{std::cos(spoke_angle) * 8.0f, std::sin(spoke_angle) * 8.0f};
        ImGui::GetForegroundDrawList()->AddLine(
            {center.x - offset.x, center.y - offset.y},
            {center.x + offset.x, center.y + offset.y},
            IM_COL32(255, 0, 0, 255),
            2.0f
        );
    };

    draw_spoke(angle);
    draw_spoke(angle + 1.5708f);
}

// Calls fn(line_begin, line_end) for every '\n'-separated segment of text, including empty ones.
template <class Fn>
void for_each_text_line(const std::string& text, Fn&& fn)
{
    for (size_t start = 0;;) {
        const size_t end   = text.find('\n', start);
        const char* begin  = text.c_str() + start;
        const char* finish = end == std::string::npos ? text.c_str() + text.size() : text.c_str() + end;

        fn(begin, finish);

        if (end == std::string::npos)
            break;

        start = end + 1;
    }
}

TextBlockMetrics measure_text_lines(const std::vector<TextLine>& lines)
{
    TextBlockMetrics metrics;
    for (const auto& line : lines) {
        for_each_text_line(
            line.text,
            [&](const char* begin, const char* end)
            {
                metrics.max_width = std::max(metrics.max_width, ImGui::CalcTextSize(begin, end).x);
                ++metrics.line_count;
            }
        );
    }
    return metrics;
}

// Draws each (possibly multi-line) entry left-aligned starting at origin, advancing by line_height
// per rendered line.
void draw_text_lines(ImDrawList* draw_list, ImVec2 origin, const std::vector<TextLine>& lines, float line_height)
{
    float cursor_y = origin.y;
    for (const auto& line : lines) {
        for_each_text_line(
            line.text,
            [&](const char* begin, const char* end)
            {
                draw_list->AddText({origin.x, cursor_y}, line.color, begin, end);
                cursor_y += line_height;
            }
        );
    }
}

void draw_bordered_box(ImDrawList* draw_list, ImVec2 box_min, ImVec2 box_max, ImU32 fill_color, ImU32 outline_color)
{
    draw_list->AddRectFilled(box_min, box_max, fill_color);
    draw_list->AddRect(box_min, box_max, outline_color);
}

// Fills the four rectangles that make up the ring between an outer and an inner box
// (i.e. the margin box minus the border box, or the border box minus the content box).
void draw_box_ring(
    ImDrawList* draw_list, ImVec2 outer_min, ImVec2 outer_max, ImVec2 inner_min, ImVec2 inner_max, ImU32 fill_color
)
{
    draw_list->AddRectFilled({outer_min.x, outer_min.y}, {outer_max.x, inner_min.y}, fill_color);
    draw_list->AddRectFilled({outer_min.x, inner_max.y}, {outer_max.x, outer_max.y}, fill_color);
    draw_list->AddRectFilled({outer_min.x, inner_min.y}, {inner_min.x, inner_max.y}, fill_color);
    draw_list->AddRectFilled({inner_max.x, inner_min.y}, {outer_max.x, inner_max.y}, fill_color);
}

float side_label_width(const Unit& source, float result)
{
    if (!(result > 0.f))
        return 0.f;

    const std::string text = format_unit_value(source, result);
    return ImGui::CalcTextSize(text.c_str()).x;
}

void draw_side_label(
    ImDrawList* draw_list, const Unit& source, float result, ImU32 color, ImVec2 anchor, ImVec2 alignment
)
{
    if (!(result > 0.f))
        return;

    const std::string text = format_unit_value(source, result);
    const ImVec2 text_size = ImGui::CalcTextSize(text.c_str());

    draw_list->AddText(
        {anchor.x - text_size.x * alignment.x, anchor.y - text_size.y * alignment.y}, color, text.c_str()
    );
}

// Widest of the four per-side labels (e.g. "4px", "1rem=16px") that would be drawn for one box edge set.
float max_edge_label_width(const Sides& sides, const EdgeMetrics& edges)
{
    return std::max(
        {side_label_width(sides.left, edges.left),
         side_label_width(sides.top, edges.top),
         side_label_width(sides.right, edges.right),
         side_label_width(sides.bottom, edges.bottom)}
    );
}

// Scales evaluated pixel edge values down to diagram space, keeping any non-zero edge readable
// (at least 3px) even when the item itself is much larger than the diagram.
EdgeMetrics scale_diagram_edges(const EdgeMetrics& edges, float scale_x, float scale_y)
{
    const auto scale = [](float value, float factor) { return value > 0.f ? std::max(3.f, value * factor) : 0.f; };
    return {
        scale(edges.left, scale_x), scale(edges.top, scale_y), scale(edges.right, scale_x),
        scale(edges.bottom, scale_y)
    };
}

// Builds a section that renders a plain block of left-aligned text lines. Measuring happens once,
// right here, while font_size is active; the returned closure only replays that already-known
// layout, so measuring and drawing can never disagree.
OverlaySection make_text_section(std::vector<TextLine> lines, float font_size, ImVec2 padding)
{
    ImGui::PushFont(ImGui::GetFont(), font_size);
    const TextBlockMetrics metrics = measure_text_lines(lines);
    const float line_height        = ImGui::GetFontSize() + 2.f;
    ImGui::PopFont();

    const ImVec2 size{
        metrics.max_width + padding.x * 2.f, static_cast<float>(metrics.line_count) * line_height + padding.y * 2.f
    };

    return {
        .size = size,
        .draw = [lines = std::move(lines), font_size, padding, line_height](ImDrawList* draw_list, ImVec2 top_left)
        {
            ImGui::PushFont(ImGui::GetFont(), font_size);
            draw_text_lines(draw_list, {top_left.x + padding.x, top_left.y + padding.y}, lines, line_height);
            ImGui::PopFont();
        }
    };
}

// Same as make_text_section, wrapped in a filled, outlined box sized to exactly fit the text.
OverlaySection make_bordered_text_section(
    std::vector<TextLine> lines, float font_size, ImVec2 padding, ImU32 fill_color, ImU32 outline_color
)
{
    OverlaySection section = make_text_section(std::move(lines), font_size, padding);

    section.draw = [size = section.size, fill_color, outline_color,
                     draw_text = std::move(section.draw)](ImDrawList* draw_list, ImVec2 top_left)
    {
        draw_bordered_box(draw_list, top_left, {top_left.x + size.x, top_left.y + size.y}, fill_color, outline_color);
        draw_text(draw_list, top_left);
    };

    return section;
}

// Ancestor chain from the root down to item itself, item's own entry marked as selected.
std::vector<TextLine> build_hierarchy_lines(const Item& item)
{
    std::vector<const Object*> ancestors_leaf_first;
    for (const Object* node = &item; node != nullptr; node = node->parent())
        ancestors_leaf_first.push_back(node);

    std::vector<TextLine> lines;
    lines.reserve(ancestors_leaf_first.size());

    for (auto it = ancestors_leaf_first.rbegin(); it != ancestors_leaf_first.rend(); ++it) {
        const bool is_selected = (*it == &item);

        std::string name = (*it)->object_name();
        if (name.empty())
            name = "(unnamed)";
        if (is_selected)
            name = "<" + name + ">";

        lines.push_back({std::move(name), is_selected ? header_color : text_color});
    }

    return lines;
}

TextLine build_header_line(const Item& item, Vec2f global_position, float item_width, float item_height)
{
    std::string header = item.object_name().empty() ? "" : item.object_name() + "  ";
    header += fmt::format(
        "{}x{}px  at [{}, {}]", item_width, item_height, global_position.x(), global_position.y()
    );
    return {std::move(header), header_color};
}

// Everything the box-model diagram needs to draw itself, computed once up front.
struct DiagramLayout
{
    float item_width, item_height;
    float model_width, model_height;
    Sides margin_sides, padding_sides;
    EdgeMetrics margin, padding;
    EdgeMetrics diagram_margin, diagram_padding;
    float diagram_label_margin_top;
    float font_height;
};

void draw_box_model_diagram(ImDrawList* draw_list, ImVec2 top_left, const DiagramLayout& layout)
{
    const EdgeMetrics& diagram_margin  = layout.diagram_margin;
    const EdgeMetrics& diagram_padding = layout.diagram_padding;

    const ImVec2 margin_box_min = {top_left.x, top_left.y + layout.diagram_label_margin_top};
    const ImVec2 margin_box_max = {margin_box_min.x + layout.model_width, margin_box_min.y + layout.model_height};

    const ImVec2 border_box_min = {margin_box_min.x + diagram_margin.left, margin_box_min.y + diagram_margin.top};
    const ImVec2 border_box_max = {margin_box_max.x - diagram_margin.right, margin_box_max.y - diagram_margin.bottom};

    const ImVec2 content_box_min = {border_box_min.x + diagram_padding.left, border_box_min.y + diagram_padding.top};
    const ImVec2 content_box_max = {
        border_box_max.x - diagram_padding.right, border_box_max.y - diagram_padding.bottom
    };

    if (diagram_margin.left > 0.f || diagram_margin.top > 0.f || diagram_margin.right > 0.f
        || diagram_margin.bottom > 0.f) {
        draw_box_ring(draw_list, margin_box_min, margin_box_max, border_box_min, border_box_max, margin_fill_color);
        draw_list->AddRect(margin_box_min, margin_box_max, margin_color);
    }

    if (diagram_padding.left > 0.f || diagram_padding.top > 0.f || diagram_padding.right > 0.f
        || diagram_padding.bottom > 0.f) {
        draw_box_ring(draw_list, border_box_min, border_box_max, content_box_min, content_box_max, padding_fill_color);
    }

    if (content_box_min.x < content_box_max.x && content_box_min.y < content_box_max.y)
        draw_list->AddRectFilled(content_box_min, content_box_max, content_fill_color);

    draw_list->AddRect(border_box_min, border_box_max, border_color, 0.f, 0, 1.5f);

    const float border_box_center_x = (border_box_min.x + border_box_max.x) * .5f;
    const float border_box_center_y = (border_box_min.y + border_box_max.y) * .5f;
    const float font_height         = layout.font_height;

    ImGui::PushFont(ImGui::GetFont(), diagram_label_font_size);

    // Margin: outside border box, adjacent to border edge.
    draw_side_label(
        draw_list, layout.margin_sides.top, layout.margin.top, margin_color,
        {border_box_center_x, border_box_min.y - font_height - label_spacing}, {.5f, 0.f}
    );
    draw_side_label(
        draw_list, layout.margin_sides.bottom, layout.margin.bottom, margin_color,
        {border_box_center_x, border_box_max.y + label_spacing}, {.5f, 0.f}
    );
    draw_side_label(
        draw_list, layout.margin_sides.left, layout.margin.left, margin_color,
        {border_box_min.x - label_spacing, border_box_center_y}, {1.f, .5f}
    );
    draw_side_label(
        draw_list, layout.margin_sides.right, layout.margin.right, margin_color,
        {border_box_max.x + label_spacing, border_box_center_y}, {0.f, .5f}
    );

    // Padding: inside border box, adjacent to border edge.
    draw_side_label(
        draw_list, layout.padding_sides.top, layout.padding.top, padding_color,
        {border_box_center_x, border_box_min.y + label_spacing}, {.5f, 0.f}
    );
    draw_side_label(
        draw_list, layout.padding_sides.bottom, layout.padding.bottom, padding_color,
        {border_box_center_x, border_box_max.y - font_height - label_spacing}, {.5f, 0.f}
    );
    draw_side_label(
        draw_list, layout.padding_sides.left, layout.padding.left, padding_color,
        {border_box_min.x + label_spacing, border_box_center_y}, {0.f, .5f}
    );
    draw_side_label(
        draw_list, layout.padding_sides.right, layout.padding.right, padding_color,
        {border_box_max.x - label_spacing, border_box_center_y}, {1.f, .5f}
    );

    ImGui::PopFont();

    if (content_box_min.x < content_box_max.x && content_box_min.y < content_box_max.y) {
        const std::string content_size_text = fmt::format(
            "{:.4g}x{:.4g}", std::max(0.f, layout.item_width - layout.padding.left - layout.padding.right),
            std::max(0.f, layout.item_height - layout.padding.top - layout.padding.bottom)
        );

        ImGui::PushFont(ImGui::GetFont(), default_font_size);
        const ImVec2 text_size = ImGui::CalcTextSize(content_size_text.c_str());

        if (content_box_max.x - content_box_min.x >= text_size.x + 4.f
            && content_box_max.y - content_box_min.y >= text_size.y) {
            draw_list->AddText(
                {(content_box_min.x + content_box_max.x - text_size.x) * .5f,
                 (content_box_min.y + content_box_max.y - text_size.y) * .5f},
                content_color,
                content_size_text.c_str()
            );
        }
        ImGui::PopFont();
    }
}

} // namespace

void Item::render_debug_overlay(ImDrawList* draw_list) const
{
    const Vec2f global_position = get_global_pos();
    const float item_width      = width();
    const float item_height     = height();

    draw_list->AddRect(
        to_im(global_position), to_im(global_position + Vec2f(item_width, item_height)), border_color, 0.f, 0, 1.5f
    );

    // Item::YogaSize is a private nested alias, so this must stay a local lambda rather than a free
    // function (a file-scope function would have no access to it).
    auto format_yoga_size = [](const YogaSize& yoga_size) -> std::string
    {
        if (std::holds_alternative<EvaluatedUnit>(yoga_size))
            return format_evaluated_unit(std::get<EvaluatedUnit>(yoga_size));
        else
            return fmt::format("{:.4g}%", std::get<float>(yoga_size));
    };

    std::vector<TextLine> property_lines;
    property_lines.reserve(6);

    property_lines.push_back(
        {fmt::format(
             "dpi:{}  scale:{:.4g}x  vp:{}x{}  rem:{:.4g}px",
             m_size_info.dpi,
             m_size_info.dpi_scale_factor,
             m_size_info.viewport_size_x,
             m_size_info.viewport_size_y,
             m_size_info.root_font_size
         ),
         text_color}
    );

    if (m_min_width.result > 0 || m_min_height.result > 0) {
        property_lines.push_back(
            {"min_w: " + format_evaluated_unit(m_min_width) + " min_h: " + format_evaluated_unit(m_min_height),
             text_color}
        );
    }
    if (!YGFloatIsUndefined(m_max_width.result) || !YGFloatIsUndefined(m_max_height.result)) {
        property_lines.push_back(
            {"max_w: " + format_evaluated_unit(m_max_width) + " max_h: " + format_evaluated_unit(m_max_height),
             text_color}
        );
    }
    if (m_width.has_value())
        property_lines.push_back({"explicit width: " + format_yoga_size(m_width.value()), text_color});
    if (m_height.has_value())
        property_lines.push_back({"explicit height: " + format_yoga_size(m_height.value()), text_color});

    property_lines.push_back(
        {fmt::format(
             "grow:{:.4g}  shrink:{:.4g}  gap:{}", m_flex_grow.result, m_flex_shrink.result, format_evaluated_unit(m_gap)
         ),
         text_color}
    );

    // Diagram is built from an Item, so it needs member access; captured here rather than declared
    // at file scope to reach m_margins/m_paddings without adding another private API surface.
    auto make_diagram_section = [this, item_width, item_height]() -> OverlaySection
    {
        const EdgeMetrics margin{m_margins.left, m_margins.top, m_margins.right, m_margins.bottom};
        const EdgeMetrics padding{m_paddings.left, m_paddings.top, m_paddings.right, m_paddings.bottom};

        const float aspect_ratio = item_height > 0.f ? item_width / item_height : 1.f;

        float model_width  = 220.f;
        float model_height = 220.f / aspect_ratio;
        if (model_height > 130.f) {
            model_height = 130.f;
            model_width  = 130.f * aspect_ratio;
        }
        model_width  = std::max(model_width, 80.f);
        model_height = std::max(model_height, 55.f);

        const float scale_x = item_width > 0.f ? model_width / item_width : 1.f;
        const float scale_y = item_height > 0.f ? model_height / item_height : 1.f;

        const EdgeMetrics diagram_margin  = scale_diagram_edges(margin, scale_x, scale_y);
        const EdgeMetrics diagram_padding = scale_diagram_edges(padding, scale_x, scale_y);

        ImGui::PushFont(ImGui::GetFont(), diagram_label_font_size);
        const float font_height = ImGui::GetFontSize();
        const float max_label_width =
            std::max(max_edge_label_width(m_margins.source, margin), max_edge_label_width(m_paddings.source, padding));
        ImGui::PopFont();

        const float label_margin_x              = max_label_width + label_spacing;
        const float diagram_label_margin_top    = margin.top > 0.f ? font_height + label_spacing : 0.f;
        const float diagram_label_margin_bottom = margin.bottom > 0.f ? font_height + label_spacing : 0.f;

        const DiagramLayout layout{
            .item_width               = item_width,
            .item_height              = item_height,
            .model_width              = model_width,
            .model_height             = model_height,
            .margin_sides             = m_margins.source,
            .padding_sides            = m_paddings.source,
            .margin                   = margin,
            .padding                  = padding,
            .diagram_margin           = diagram_margin,
            .diagram_padding          = diagram_padding,
            .diagram_label_margin_top = diagram_label_margin_top,
            .font_height              = font_height
        };

        return {
            .size  = {model_width + label_margin_x * 2.f,
                      model_height + diagram_label_margin_top + diagram_label_margin_bottom},
            .align = HorizontalAlign::Center,
            .draw  = [layout](ImDrawList* draw_list, ImVec2 top_left) { draw_box_model_diagram(draw_list, top_left, layout); }
        };
    };

    std::vector<OverlaySection> sections;
    sections.reserve(4);
    sections.push_back(
        make_text_section({build_header_line(*this, global_position, item_width, item_height)}, default_font_size, {0.f, 0.f})
    );
    sections.push_back(
        make_bordered_text_section(
            build_hierarchy_lines(*this), hierarchy_font_size, hierarchy_padding, hierarchy_fill_color,
            hierarchy_border_color
        )
    );
    sections.push_back(make_text_section(std::move(property_lines), default_font_size, {0.f, 0.f}));
    sections.push_back(make_diagram_section());

    float panel_width  = 0.f;
    float panel_height = panel_padding.y * 2.f + section_gap * static_cast<float>(sections.size() - 1);
    for (const OverlaySection& section : sections) {
        panel_width   = std::max(panel_width, section.size.x);
        panel_height += section.size.y;
    }
    panel_width += panel_padding.x * 2.f;

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const ImVec2 panel_min        = {viewport->Pos.x, viewport->Pos.y + viewport->Size.y - panel_height};
    const ImVec2 panel_max        = {viewport->Pos.x + panel_width, viewport->Pos.y + viewport->Size.y};

    draw_bordered_box(draw_list, panel_min, panel_max, panel_fill_color, panel_border_color);

    float cursor_y = panel_min.y + panel_padding.y;
    for (const OverlaySection& section : sections) {
        const float cursor_x = section.align == HorizontalAlign::Center
            ? panel_min.x + (panel_width - section.size.x) * .5f
            : panel_min.x + panel_padding.x;

        section.draw(draw_list, {cursor_x, cursor_y});
        cursor_y += section.size.y + section_gap;
    }

    draw_alive_indicator({0, panel_min.y - 16});
}

#endif

} // namespace Slic3r::App::Yoga
