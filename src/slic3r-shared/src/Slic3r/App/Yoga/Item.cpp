#include "Slic3r/App/Yoga/Item.hpp"

#include <Slic3r/Assert.hpp>
#include <Slic3r/Log.hpp>

#include "Slic3r/App/Render/ImguiRender.hpp"
#include "Slic3r/App/Yoga/ItemEvents.hpp"
#include "Slic3r/App/Yoga/ScrollArea.hpp"

#include <imgui_internal.h>
#include <fmt/format.h>
#include <cmath>

namespace Slic3r::App::Yoga {

Render::ImguiRender* Item::m_imgui_render = nullptr;

Item::Item()
{
    set_object_name("Item");

    m_node = YGNodeNew();

    YGNodeStyleSetFlexDirection(m_node, m_flex_direction);
    YGNodeStyleSetDirection(m_node, m_direction);
    YGNodeStyleSetFlexShrink(m_node, m_flex_shrink.result);
}

Item::~Item()
{
    if (m_node) {
        YGNodeFree(m_node);
    }
}

Item::Callbacks& Item::item_callbacks()
{
    return m_callbacks;
}

void Item::resize(const SizeInfo& size_info)
{
    if (size_info != m_size_info) {
        m_size_info           = size_info;
        m_min_size_calculated = false;

        if (m_width.has_value() && std::holds_alternative<EvaluatedUnit>(*m_width)) {
            auto& eu = std::get<EvaluatedUnit>(*m_width);
            eu.evaluate(size_info);
            YGNodeStyleSetWidth(m_node, eu.result);
        }
        if (m_height.has_value() && std::holds_alternative<EvaluatedUnit>(*m_height)) {
            auto& eu = std::get<EvaluatedUnit>(*m_height);
            eu.evaluate(size_info);
            YGNodeStyleSetHeight(m_node, eu.result);
        }
        if (m_left.has_value()) {
            m_left->evaluate(size_info);
            YGNodeStyleSetPosition(m_node, YGEdgeLeft, m_left->result);
        }
        if (m_right.has_value()) {
            m_right->evaluate(size_info);
            YGNodeStyleSetPosition(m_node, YGEdgeRight, m_right->result);
        }
        if (m_top.has_value()) {
            m_top->evaluate(size_info);
            YGNodeStyleSetPosition(m_node, YGEdgeTop, m_top->result);
        }
        if (m_bottom.has_value()) {
            m_bottom->evaluate(size_info);
            YGNodeStyleSetPosition(m_node, YGEdgeBottom, m_bottom->result);
        }

        m_min_width.evaluate(size_info);
        YGNodeStyleSetMinWidth(m_node, m_min_width.result);
        m_max_width.evaluate(size_info);
        YGNodeStyleSetMaxWidth(m_node, m_max_width.result);
        m_min_height.evaluate(size_info);
        YGNodeStyleSetMinHeight(m_node, m_min_height.result);
        m_max_height.evaluate(size_info);
        YGNodeStyleSetMaxHeight(m_node, m_max_height.result);

        m_flex_grow.evaluate(size_info);
        YGNodeStyleSetFlexGrow(m_node, m_flex_grow.result);
        m_flex_shrink.evaluate(size_info);
        YGNodeStyleSetFlexShrink(m_node, m_flex_shrink.result);

        m_gap.evaluate(size_info);
        YGNodeStyleSetGap(m_node, YGGutterAll, m_gap.result);

        m_margins.evaluate(size_info);
        YGNodeStyleSetMargin(m_node, YGEdgeLeft, m_margins.left);
        YGNodeStyleSetMargin(m_node, YGEdgeRight, m_margins.right);
        YGNodeStyleSetMargin(m_node, YGEdgeTop, m_margins.top);
        YGNodeStyleSetMargin(m_node, YGEdgeBottom, m_margins.bottom);

        m_paddings.evaluate(size_info);
        YGNodeStyleSetPadding(m_node, YGEdgeLeft, m_paddings.left);
        YGNodeStyleSetPadding(m_node, YGEdgeRight, m_paddings.right);
        YGNodeStyleSetPadding(m_node, YGEdgeTop, m_paddings.top);
        YGNodeStyleSetPadding(m_node, YGEdgeBottom, m_paddings.bottom);

        size_info_changed(size_info);

        set_style_dirty();
    }

    std::ranges::for_each(m_children, [&](ObjectPtr& child) { child->resize(size_info); });
}

void Item::render(const Vec2f& pos, const Vec2f& size)
{
    render_item_begin(pos, size);

    render_item_end(pos, size);
}

YGNodeRef Item::node() const
{
    return m_node;
}

float Item::z() const
{
    return m_z;
}

float Item::width() const
{
    return YGNodeLayoutGetWidth(m_node);
}

float Item::height() const
{
    return YGNodeLayoutGetHeight(m_node);
}

float Item::left() const
{
    return YGNodeLayoutGetLeft(m_node);
}

float Item::right() const
{
    return YGNodeLayoutGetRight(m_node);
}

float Item::top() const
{
    return YGNodeLayoutGetTop(m_node);
}

float Item::bottom() const
{
    return YGNodeLayoutGetBottom(m_node);
}

const Unit& Item::min_width() const
{
    return m_min_width.source;
}

const Unit& Item::min_heigth() const
{
    return m_min_height.source;
}

const Unit& Item::max_width() const
{
    return m_max_width.source;
}

const Unit& Item::max_height() const
{
    return m_max_height.source;
}

bool Item::is_visible() const
{
    return is_node_visible(m_node);
}

bool Item::is_self_visible() const
{
    return m_visible;
}

bool Item::debug_border() const
{
    return m_debug_border;
}

const Unit& Item::flex_grow() const
{
    return m_flex_grow.source;
}

const Unit& Item::flex_shrink() const
{
    return m_flex_shrink.source;
}

YGDirection Item::direction() const
{
    return m_direction;
}

float Item::aspect_ratio() const
{
    return m_aspect_ratio;
}

YGJustify Item::justify_content() const
{
    return m_justify_content;
}

YGPositionType Item::position_type() const
{
    return m_position_type;
}

YGAlign Item::align_items() const
{
    return m_align_items;
}

YGAlign Item::align_content() const
{
    return m_align_content;
}

const EvaluatedMargins& Item::margin() const
{
    return m_margins;
}

const EvaluatedPaddings& Item::padding() const
{
    return m_paddings;
}

const Unit& Item::gap() const
{
    return m_gap.source;
}

Orientation Item::orientation() const
{
    return m_orientation;
}

YGWrap Item::flex_wrap() const
{
    return YGNodeStyleGetFlexWrap(m_node);
}

bool Item::is_node_dirty() const
{
    return YGNodeIsDirty(m_node);
}

bool Item::is_in_window() const
{
    return m_parent_item ? m_parent_item->is_in_window() : false;
}

bool Item::enabled() const
{
    const Item* item = this;
    while (item) {
        if (!item->m_enabled) {
            return false;
        }

        item = item->m_parent_item;
    }

    return true;
}

void Item::set_enabled(bool enabled)
{
    if (m_enabled != enabled) {
        m_enabled = enabled;
        update_enabled();
    }
}

void Item::set_visible(bool visible)
{
    if (m_visible != visible) {
        m_visible = visible;
        YGNodeStyleSetDisplay(m_node, visible ? YGDisplayFlex : YGDisplayNone);
        set_style_dirty();
        update_visible();
    }
}

void Item::set_width(const Unit& width)
{
    if (!m_width.has_value()
        || !std::holds_alternative<EvaluatedUnit>(m_width.value())
        || std::get<EvaluatedUnit>(m_width.value()).source != width)
    {
        EvaluatedUnit new_width{width};
        new_width.evaluate(m_size_info);
        YGNodeStyleSetWidth(m_node, new_width.result);
        m_width = new_width;
        set_style_dirty();
    }
}

void Item::set_height(const Unit& height)
{
    if (!m_height.has_value()
        || !std::holds_alternative<EvaluatedUnit>(m_height.value())
        || std::get<EvaluatedUnit>(m_height.value()).source != height)
    {
        EvaluatedUnit new_height{height};
        new_height.evaluate(m_size_info);
        YGNodeStyleSetHeight(m_node, new_height.result);
        m_height = new_height;
        set_style_dirty();
    }
}

void Item::set_width_percent(float width_percent)
{
    if (!m_width.has_value()
        || !std::holds_alternative<float>(m_width.value())
        || !Domain::fuzzy_compare(std::get<float>(m_width.value()), width_percent))
    {
        m_width = width_percent;
        YGNodeStyleSetWidthPercent(m_node, width_percent);
        set_style_dirty();
    }
}

void Item::set_height_percent(float height_percent)
{
    if (!m_height.has_value()
        || !std::holds_alternative<float>(m_height.value())
        || !Domain::fuzzy_compare(std::get<float>(m_height.value()), height_percent))
    {
        m_height = height_percent;
        YGNodeStyleSetHeightPercent(m_node, height_percent);
        set_style_dirty();
    }
}

void Item::set_left(const Unit& left)
{
    if (!m_left.has_value() || m_left.value().source != left) {
        EvaluatedUnit new_left{left};
        new_left.evaluate(m_size_info);
        YGNodeStyleSetPosition(m_node, YGEdgeLeft, new_left.result);
        m_left = new_left;
        set_style_dirty();
    }
}

void Item::set_right(const Unit& right)
{
    if (!m_right.has_value() || m_right.value().source != right) {
        EvaluatedUnit new_right{right};
        new_right.evaluate(m_size_info);
        YGNodeStyleSetPosition(m_node, YGEdgeRight, new_right.result);
        m_right = new_right;
        set_style_dirty();
    }
}

void Item::set_top(const Unit& top)
{
    if (!m_top.has_value() || m_top.value().source != top) {
        EvaluatedUnit new_top{top};
        new_top.evaluate(m_size_info);
        YGNodeStyleSetPosition(m_node, YGEdgeTop, new_top.result);
        m_top = new_top;
        set_style_dirty();
    }
}

void Item::set_bottom(const Unit& bottom)
{
    if (!m_bottom.has_value() || m_bottom.value().source != bottom) {
        EvaluatedUnit new_bottom{bottom};
        new_bottom.evaluate(m_size_info);
        YGNodeStyleSetPosition(m_node, YGEdgeBottom, new_bottom.result);
        m_bottom = new_bottom;
        set_style_dirty();
    }
}

void Item::set_flex_wrap(YGWrap wrap)
{
    YGNodeStyleSetFlexWrap(m_node, wrap);
    set_style_dirty();
}

void Item::remove_later(Item* child)
{
    ASSERT(child);
    ASSERT(index_of(child).has_value());
    push_event(std::make_unique<RemoveEvent>(child));
}

void Item::move_later(Item* target, size_t index)
{
    push_event(std::make_unique<MoveEvent>(this, target, index));
}

const std::vector<Item*>& Item::items() const
{
    return m_children_items;
}

Item* Item::get_item(size_t index) const
{
    return m_children_items.at(index);
}

void Item::set_debug_border(bool show_debug_border)
{
    m_debug_border = show_debug_border;
}

void Item::invalidate_min_size_calculation()
{
    m_min_size_calculated = false;
    set_style_dirty();
}

void Item::update_enabled()
{
    enabled_updated_internal();
    for (Item* child : m_children_items) {
        if (child) {
            child->update_enabled();
        }
    }
}

void Item::update_visible()
{
    visible_updated_internal();
    for (Item* child : m_children_items) {
        if (child) {
            child->update_visible();
        }
    }
}

void Item::size_info_changed(const SizeInfo& info_size) {}

ImVec2 Item::to_im(const Vec2f& val)
{
    return {val.x(), val.y()};
}

Vec2f Item::from_im(const ImVec2& val)
{
    return {val.x, val.y};
}

bool Item::is_node_visible(YGNodeRef node)
{
    while (node) {
        if (YGNodeStyleGetDisplay(node) == YGDisplayNone) {
            return false;
        }
        node = YGNodeGetParent(node);
    }
    return true;
}

Vec2f Item::get_item_size()
{
    return {};
}

void Item::enabled_updated_internal() {}

void Item::visible_updated_internal() {}

void Item::on_resized()
{
    if (m_callbacks.size_changed) {
        m_callbacks.size_changed();
    }
}

Vec2f Item::get_global_pos() const
{
    Vec2f pos{left(), top()};

    if (m_parent_item) {
        ScrollArea* scroll_area = dynamic_cast<ScrollArea*>(m_parent_item);
        if (scroll_area) {
            pos -= scroll_area->scroll_pos();
        }
        pos += m_parent_item->get_global_pos();
    }

    return pos;
};

void Item::set_imgui_render(Render::ImguiRender* imgui_render)
{
    m_imgui_render = imgui_render;
}

void Object::set_theme(Theme* theme)
{
    m_theme = theme;
}

void Object::set_scale_factor(float scale_factor)
{
    YGConfigSetPointScaleFactor(m_config, scale_factor);
}

float Object::scale_factor()
{
    return YGConfigGetPointScaleFactor(m_config);
}

float Object::pixel_round(float value)
{
    const auto scale = YGConfigGetPointScaleFactor(m_config);
    return YGRoundValueToPixelGrid(value, scale, false, false);
}

ImVec2 Object::pixel_round(const ImVec2& value)
{
    return {pixel_round(value.x), pixel_round(value.y)};
}

Vec2f Object::pixel_round(const Vec2f& value)
{
    return {pixel_round(value.x()), pixel_round(value.y())};
}

void Item::update_children_render_order()
{
    m_children_render_order.clear();
    std::ranges::copy_if(
        m_children_items,
        std::back_inserter(m_children_render_order),
        [](Item* child) { return child; }
    );
    // sort children by Z layer, higher Z shall be rendered first
    std::ranges::sort(
        m_children_render_order,
        [](Item* left, Item* right) { return left->z() > right->z(); }
    );

    set_style_dirty();
}

Item* Item::parent_item() const
{
    return m_parent_item;
}

void Item::set_self_align(YGAlign align)
{
    if (m_self_align != align) {
        m_self_align = align;
        YGNodeStyleSetAlignSelf(m_node, m_self_align);
        set_style_dirty();
    }
}

void Item::set_margin(const Margins& margin)
{
    if (m_margins.source != margin) {
        m_margins.source = margin;
        m_margins.evaluate(m_size_info);
        YGNodeStyleSetMargin(m_node, YGEdgeLeft, m_margins.left);
        YGNodeStyleSetMargin(m_node, YGEdgeRight, m_margins.right);
        YGNodeStyleSetMargin(m_node, YGEdgeTop, m_margins.top);
        YGNodeStyleSetMargin(m_node, YGEdgeBottom, m_margins.bottom);
        set_style_dirty();
    }
}

void Item::set_padding(const Paddings& padding)
{
    if (m_paddings.source != padding) {
        m_paddings.source = padding;
        m_paddings.evaluate(m_size_info);
        YGNodeStyleSetPadding(m_node, YGEdgeLeft, m_paddings.left);
        YGNodeStyleSetPadding(m_node, YGEdgeRight, m_paddings.right);
        YGNodeStyleSetPadding(m_node, YGEdgeTop, m_paddings.top);
        YGNodeStyleSetPadding(m_node, YGEdgeBottom, m_paddings.bottom);
        set_style_dirty();
    }
}

void Item::set_min_width(const Unit& min_width)
{
    if (min_width != m_min_width.source) {
        m_min_width.source = min_width;
        m_min_width.evaluate(m_size_info);
        YGNodeStyleSetMinWidth(m_node, m_min_width.result);
        set_style_dirty();
    }
}

void Item::set_max_width(const Unit& max_width)
{
    if (max_width != m_max_width.source) {
        m_max_width.source = max_width;
        m_max_width.evaluate(m_size_info);
        YGNodeStyleSetMaxWidth(m_node, m_max_width.result);
        set_style_dirty();
    }
}

void Item::set_min_height(const Unit& min_height)
{
    if (min_height != m_min_height.source) {
        m_min_height.source = min_height;
        m_min_height.evaluate(m_size_info);
        YGNodeStyleSetMinHeight(m_node, m_min_height.result);
        set_style_dirty();
    }
}

void Item::set_max_height(const Unit& max_height)
{
    if (max_height != m_max_height.source) {
        m_max_height.source = max_height;
        m_max_height.evaluate(m_size_info);
        YGNodeStyleSetMaxHeight(m_node, m_max_height.result);
        set_style_dirty();
    }
}

void Item::set_direction(YGDirection direction)
{
    if (m_direction != direction) {
        m_direction = direction;
        YGNodeStyleSetDirection(m_node, m_direction);
        set_style_dirty();
    }
}

void Item::set_aspect_ratio(float aspect_ratio)
{
    if (!Domain::fuzzy_compare(m_aspect_ratio, aspect_ratio)) {
        m_aspect_ratio = aspect_ratio;
        YGNodeStyleSetAspectRatio(m_node, m_aspect_ratio);
        set_style_dirty();
    }
}

void Item::set_justify_content(YGJustify justify_content)
{
    if (m_justify_content != justify_content) {
        m_justify_content = justify_content;
        YGNodeStyleSetJustifyContent(m_node, m_justify_content);
        set_style_dirty();
    }
}

void Item::set_position_type(YGPositionType position_type)
{
    if (m_position_type != position_type) {
        m_position_type = position_type;
        YGNodeStyleSetPositionType(m_node, m_position_type);
        set_style_dirty();
    }
}

void Item::set_orientation(Orientation orientation)
{
    if (m_orientation != orientation) {
        m_orientation = orientation;
        switch (orientation) {
        case Orientation::Horizontal:
            m_flex_direction = YGFlexDirectionRow;
            break;
        case Orientation::Vertical:
            m_flex_direction = YGFlexDirectionColumn;
            break;
        case Orientation::VerticalReverse:
            m_flex_direction = YGFlexDirectionColumnReverse;
            break;
        }
        YGNodeStyleSetFlexDirection(m_node, m_flex_direction);
        set_style_dirty();
    }
}

void Item::set_flex_grow(const Unit& flex_grow)
{
    if (flex_grow != m_flex_grow.source) {
        m_flex_grow.source = flex_grow;
        m_flex_grow.evaluate(m_size_info);
        YGNodeStyleSetFlexGrow(m_node, m_flex_grow.result);
        set_style_dirty();
    }
}

void Item::set_flex_shrink(const Unit& flex_shrink)
{
    if (flex_shrink != m_flex_shrink.source) {
        m_flex_shrink.source = flex_shrink;
        m_flex_shrink.evaluate(m_size_info);
        YGNodeStyleSetFlexShrink(m_node, m_flex_shrink.result);
        set_style_dirty();
    }
}

void Item::set_gap(const Unit& gap)
{
    if (m_gap.source != gap) {
        m_gap.source = gap;
        m_gap.evaluate(m_size_info);
        YGNodeStyleSetGap(m_node, YGGutter::YGGutterAll, m_gap.result);
        set_style_dirty();
    }
}

void Item::set_align_items(YGAlign align_items)
{
    if (m_align_items != align_items) {
        m_align_items = align_items;
        YGNodeStyleSetAlignItems(m_node, m_align_items);
        set_style_dirty();
    }
}

void Item::set_align_content(YGAlign align_content)
{
    if (m_align_content != align_content) {
        m_align_content = align_content;
        YGNodeStyleSetAlignContent(m_node, m_align_content);
        set_style_dirty();
    }
}

void Item::set_z(float z)
{
    if (!Domain::fuzzy_compare(m_z, z)) {
        m_z = z;
        if (m_parent_item) {
            m_parent_item->update_children_render_order();
        }
    }
}

void Item::insert(ObjectPtr child, size_t index)
{
    Item* item = dynamic_cast<Item*>(child.get());

    Object::insert(std::move(child), index);

    if (item) {
        item->m_parent_item = this;
        YGNodeInsertChild(m_node, item->node(), std::min(index, YGNodeGetChildCount(m_node)));
        m_children_items.insert(m_children_items.cbegin() + index, item);
        update_children_render_order();
        m_has_items = true;
    } else {
        m_children_items.insert(m_children_items.cbegin() + index, nullptr);
    }
}

ObjectPtr Item::remove(Object* child)
{
    std::optional<size_t> index = index_of(child);
    ASSERT(index.has_value());

    Item* item = m_children_items.at(index.value());
    m_children_items.erase(m_children_items.cbegin() + index.value());
    if (item) {
        item->m_parent_item = nullptr;
        YGNodeRemoveChild(m_node, item->node());
        update_children_render_order();

        m_has_items = std::any_of(
            m_children_items.cbegin(),
            m_children_items.cend(),
            [](Item* item) { return item; }
        );
    }

    return Object::remove(child);
}

void Item::style_node()
{
    ASSERT(m_node);
    if (!m_visible) {
        return;
    }

    if (!m_has_items) {
        // If we are a leaf node, calculate our min size
        if (!m_min_size_calculated) {
            m_min_size_calculated = true;
            Vec2f sz              = get_item_size();
            sz                    = sz.cwiseMax(Vec2f{m_min_width.result, m_min_height.result});
            YGNodeStyleSetMinWidth(m_node, sz.x());
            YGNodeStyleSetMinHeight(m_node, sz.y());
        }
    } else {
        // Otherwise style all our children
        for (const ObjectPtr& object : std::as_const(m_children)) {
            object->style_node();
        }
    }
}

void Item::render_item_begin(const Vec2f& pos, const Vec2f& size) {}

void Item::render_item_end(const Vec2f& pos, const Vec2f& size)
{
    render_debug(pos, size);

    for (size_t render_index = 0; render_index < m_children_render_order.size(); ++render_index) {
        render_node(pos, m_children_render_order.at(render_index));
    }
}

void Item::render_node(const Vec2f& pos, Item* child)
{
    YGNodeRef child_node = child->node();
    if (!is_node_visible(child_node)) {
        return;
    }

    Vec2f cell_pos  = pos + Vec2f(child->left(), child->top());
    Vec2f cell_size = Vec2f(child->width(), child->height());

    if (isnan(cell_size.x()) || isnan(cell_size.y())) {
        return;
    }

    child->render(cell_pos, cell_size);
}

void Item::render_debug(const Vec2f& pos, const Vec2f& size)
{
#ifdef DEBUG
    {
        ImRect rect(to_im(pos), to_im(pos + size));

        if (ImGui::IsMouseHoveringRect(rect.Min, rect.Max, false)) {
            if (ImGui::IsKeyDown(ImGuiMod_Alt) && ImGui::IsKeyDown(ImGuiMod_Ctrl)) {
                ImDrawList* draw_list = ImGui::GetForegroundDrawList();
                draw_list->AddRect(rect.Min, rect.Max, IM_COL32(255, 0, 0, 128));
            } else if (ImGui::IsKeyDown(ImGuiMod_Alt)) {
                m_debug_item = this;
            }
        }
    }
#endif

    if (m_debug_border) {
        ImDrawList* draw_list = ImGui::GetForegroundDrawList();
        ImRect rect(to_im(pos), to_im(pos + size));
        // SPDLOG_INFO(
        // "debug {}:{} -> {}:{} pos {}:{} size {}:{}", rect.Min.x, rect.Min.y, rect.Max.x,
        // rect.Max.y, pos.x(), pos.y(), size.x(), size.y()
        // );
        draw_list->AddRectFilled(
            rect.Min,
            rect.Max,
            enabled() ? IM_COL32(255, 0, 0, 128) : IM_COL32(0, 0, 255, 128),
            false
        );
    }
}

void Item::render_image(
    const Render::TexturePtr& texture,
    const ImVec2& image_size,
    const ImVec2& uv0,
    const ImVec2& uv1,
    const ImVec4& background_col,
    const ImVec4& tint_col,
    float rounding
)
{
    ImGui::PushStyleVar(ImGuiStyleVar_ImageRounding, rounding);
    ImGui::ImageWithBg(
        (ImTextureID) (intptr_t) texture.get(),
        image_size,
        uv0,
        uv1,
        background_col,
        tint_col
    );
    m_imgui_render->use_texture(texture);
    ImGui::PopStyleVar();
}

void Item::check_resized()
{
    if (!m_visible) {
        return;
    }

    float w = width();
    float h = height();
    if (!Domain::fuzzy_compare(w, m_last_width) || !Domain::fuzzy_compare(h, m_last_height)) {
        m_last_width  = w;
        m_last_height = h;
        on_resized();
    }

    const std::vector<Item*> children_items = m_children_items;
    for (Item* child : children_items) {
        if (child) {
            child->check_resized();
        }
    }
}

} // namespace Slic3r::App::Yoga
