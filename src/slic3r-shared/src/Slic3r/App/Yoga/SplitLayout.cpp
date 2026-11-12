#include "Slic3r/App/Yoga/SplitLayout.hpp"

#include "Slic3r/App/Yoga/AbstractButton.hpp"
#include "Slic3r/App/Yoga/Window.hpp"
#include "Slic3r/App/Yoga/Rectangle.hpp"

#include <Slic3r/Log.hpp>

#include <imgui_internal.h>

#include <ranges>
#include <numeric>

namespace Slic3r::App::Yoga {

class SeparatorButton : public AbstractButton
{
public:
    struct SeparatorCallbacks
    {
        std::function<void(SeparatorButton* button, float requested_pos)> moved{nullptr};
    };

    explicit SeparatorButton(SplitLayout* split_layout) :
        AbstractButton(std::string{}, "SeparatorButton"),
        m_split_layout(split_layout)
    {
        set_flex_shrink(0);
        set_justify_content(YGJustifyCenter);
        set_align_items(YGAlignCenter);

        m_rectangle = emplace_back<Rectangle>();
        m_rectangle->set_visible(false);
        m_rectangle->set_fill(IM_COL32_WHITE);
    }

    void render(const Vec2f& pos, const Vec2f& size) override
    {
        AbstractButton::render(pos, size);

        if (pressed() && m_separator_callbacks.moved) {
            ImVec2 requested_pos = GImGui->IO.MousePos - to_im(m_split_layout->get_global_pos());

            m_separator_callbacks.moved(
                this,
                orientation() == Orientation::Horizontal ? requested_pos.x : requested_pos.y
            );
        }
    }

    Vec2f get_item_size() override
    {
        return orientation() == Orientation::Horizontal ? Vec2f{0, 5} : Vec2f{5, 0};
    }

    void set_orientation(Orientation orientation)
    {
        Item::set_orientation(orientation);
        if (orientation == Orientation::Horizontal) {
            set_max_width(5);
            set_min_width(5);
            m_rectangle->set_width(1);
            m_rectangle->set_height(75);
        } else {
            set_max_height(5);
            set_min_height(5);
            m_rectangle->set_height(1);
            m_rectangle->set_width(75);
        }
        update_cursor();
        invalidate_min_size_calculation();
    }

    SeparatorCallbacks& separator_callbacks()
    {
        return m_separator_callbacks;
    }

    void update_cursor()
    {
        set_cursor(
            orientation() == Orientation::Horizontal ? ImGuiMouseCursor_ResizeEW :
                                                       ImGuiMouseCursor_ResizeNS
        );
    }

protected:
    void pressed_primary_updated_internal() override
    {
        hovered_updated_internal();
    }

    void hovered_updated_internal() override
    {
        m_rectangle->set_visible(pressed() || hovered());
    }

private:
    SeparatorCallbacks m_separator_callbacks;

    SplitLayout* m_split_layout{nullptr};
    Rectangle* m_rectangle{nullptr};
};

class SeparatorWindow : public Window
{
public:
    SeparatorWindow(SplitLayout* split_layout) : Window("SeparatorWindow")
    {
        set_padding(0);
        set_rounding(0);
        set_alpha(0);
        m_button = emplace_back<SeparatorButton>(split_layout);
    }

    SeparatorButton* button() const
    {
        return m_button;
    }

    void set_orientation(Orientation orientation)
    {
        Item::set_orientation(orientation);
        if (orientation == Orientation::Horizontal) {
            set_max_width(5);
            set_min_width(5);
        } else {
            set_max_height(5);
            set_min_height(5);
        }
    }

private:
    SeparatorButton* m_button{nullptr};
};

SplitLayout::SplitLayout()
{
    set_object_name("SplitLayout");
}

void SplitLayout::insert(ObjectPtr child, size_t index)
{
    Item* item = dynamic_cast<Item*>(child.get());
    ASSERT(item, "SplitLayout only supports Items");
    m_needs_windows = !is_in_window();
    item->set_flex_shrink(0);
    if (!object_count()) {
        Item::insert(std::move(child), index);
        return; // early exit we are not adding separator
    }

    // item_count > 0 => we are adding a separator
    if (object_count() > 1) {
        index *= 2;
        if (index > object_count()) {
            index--;
        }
    }

    size_t separator_index = index == object_count() ? index : index + 1;

    Item::insert(std::move(child), index);

    // insert separator
    if (m_needs_windows) {
        std::unique_ptr<SeparatorWindow> window = std::make_unique<SeparatorWindow>(this);
        window->button()->separator_callbacks().moved =
            [this](SeparatorButton* button, float requested_pos)
        { on_separator_moved(button, requested_pos); };
        Item::insert(std::move(window), separator_index);
    } else {
        std::unique_ptr<SeparatorButton> separator = std::make_unique<SeparatorButton>(this);
        separator->separator_callbacks().moved =
            [this](SeparatorButton* button, float requested_pos)
        { on_separator_moved(button, requested_pos); };
        Item::insert(std::move(separator), separator_index);
    }

    // rebuild separator array
    m_separators.clear();
    m_content_items.clear();
    for (const ObjectPtr& child : std::as_const(m_children)) {
        bool is_separator = false;
        if (m_needs_windows) {
            SeparatorWindow* window = dynamic_cast<SeparatorWindow*>(child.get());
            if (window) {
                m_separators.emplace_back(window->button(), window);
                window->set_orientation(orientation());
                window->button()->set_orientation(orientation());
                window->button()->update_cursor();
                is_separator = true;
            }
        } else {
            SeparatorButton* button = dynamic_cast<SeparatorButton*>(child.get());
            if (button) {
                button->set_orientation(orientation());
                m_separators.emplace_back(button, nullptr);
                is_separator = true;
            }
        }

        if (!is_separator) {
            m_content_items.push_back(dynamic_cast<Item*>(child.get()));
        }
    }
}

void SplitLayout::append(ObjectPtr child)
{
    insert(std::move(child), object_count() - m_separators.size());
}

ObjectPtr SplitLayout::remove(Object* child)
{
    // check if we are removing separator
    if (object_count() > 1) {
        std::vector<Item*>::const_iterator content_it = std::find_if(
            m_content_items.cbegin(),
            m_content_items.cend(),
            [child](Item* content_item) { return child == content_item; }
        );
        if (content_it != m_content_items.cend()) {
            m_content_items.erase(content_it);
        }

        // remove separator
        std::optional<size_t> child_index = index_of(child);
        ASSERT(child_index.has_value(), "Item is not present in SplitLayout");
        ASSERT(child_index.value() % 2 == 0, "Child index should always be even in SplitLayout!");
        size_t separator_index = child_index.value() / 2;
        Item::remove(m_separators.at(separator_index).item());
        m_separators.erase(m_separators.cbegin() + separator_index);
    }
    return Item::remove(child);
}

void SplitLayout::set_orientation(Orientation orientation)
{
    for (const SeparatorItem& separator_item : std::as_const(m_separators)) {
        separator_item.button->set_orientation(orientation);
        if (separator_item.window) {
            separator_item.window->set_orientation(orientation);
        }
    }
    Item::set_orientation(orientation);
}

void SplitLayout::on_separator_moved(SeparatorButton* button, float requested_pos)
{
    // Resolve two items which we are resizing
    Item* moving_item                = m_needs_windows ? button->parent_item() : button;
    std::optional<size_t> item_index = index_of(moving_item);
    ASSERT(item_index.has_value());
    moving_item      = get_item(item_index.value() - 1);
    Item* other_item = get_item(item_index.value() + 1);
    ASSERT(moving_item);
    ASSERT(other_item);

    // Resolve both of them
    if (orientation() == Orientation::Horizontal) {
        // compute delta from requested_pos
        // requested_pos represents position of right edge of the moving_item
        float delta = requested_pos - (moving_item->left() + moving_item->width());

        const float allowed_delta_moving = std::clamp(
                                               moving_item->width() + delta,
                                               moving_item->min_width().value,
                                               moving_item->max_width().value
                                           )
            - moving_item->width();

        const float allowed_delta_other = std::clamp(
                                              other_item->width() - delta,
                                              other_item->min_width().value,
                                              other_item->max_width().value
                                          )
            - other_item->width();

        float final_delta = std::min(abs(allowed_delta_moving), abs(allowed_delta_other));

        if (Domain::fuzzy_compare(final_delta, 0.f)) {
            return; // early exit
        }

        if (std::signbit(delta)) {
            final_delta *= -1;
        }

        moving_item->set_width(moving_item->width() + final_delta);
        other_item->set_width(other_item->width() - final_delta);
    } else {
        // compute delta from requested_pos
        // requested_pos represents position of bottom edge of the moving_item
        float delta = requested_pos - (moving_item->top() + moving_item->height());

        const float allowed_delta_moving = std::clamp(
                                               moving_item->height() + delta,
                                               moving_item->min_heigth().value,
                                               moving_item->max_height().value
                                           )
            - moving_item->height();

        const float allowed_delta_other = std::clamp(
                                              other_item->height() - delta,
                                              other_item->min_heigth().value,
                                              other_item->max_height().value
                                          )
            - other_item->height();

        float final_delta = std::min(abs(allowed_delta_moving), abs(allowed_delta_other));

        if (Domain::fuzzy_compare(final_delta, 0.f)) {
            return; // early exit
        }

        if (std::signbit(delta)) {
            final_delta *= -1;
        }

        moving_item->set_height(moving_item->height() + final_delta);
        other_item->set_height(other_item->height() - final_delta);
    }
}

void SplitLayout::set_flex_child(Item* child, bool flex)
{
    if (flex) {
        m_flex_children.insert(child);
    } else {
        m_flex_children.erase(child);
    }

    on_resized();
}

void SplitLayout::set_visible_child(Item* child, bool visible)
{
    std::optional<size_t> index = index_of(child);
    ASSERT(index.has_value(), "Child is not present in SplitLayout");

    child->set_visible(visible);

    if (object_count() > 1) {
        // also set their separator visible
        get_item(index.value() == 0 ? index.value() + 1 : index.value() - 1)->set_visible(visible);
    }

    on_resized();
}

void SplitLayout::set_separator_enable(size_t index, bool enable)
{
    m_separators.at(index).item()->set_enabled(enable);
}

void SplitLayout::invalidate()
{
    m_last_height = 0;
    m_last_width  = 0;
}

void SplitLayout::on_resized()
{
    for (Item* child : std::as_const(m_children_render_order)) {
        ASSERT(
            child->flex_grow() == 0,
            "SplitLayout children cannot have set flex explicitly, use SplitLayout::set_flex_child"
        );
    }

    for (Item* flex_child : std::as_const(m_flex_children)) {
        flex_child->set_flex_grow(1);
        if (orientation() == Orientation::Horizontal) {
            flex_child->set_width(0);
        } else {
            flex_child->set_height(0);
        }
    }

    auto min_item_size = [&](Item* item) -> float
    {
        return orientation() == Orientation::Horizontal ? item->min_width().value :
                                                          item->min_heigth().value;
    };
    auto item_size = [&](Item* item) -> float
    {
        if (m_flex_children.contains(item)) {
            return min_item_size(item);
        } else {
            return orientation() == Orientation::Horizontal ? item->width() : item->height();
        }
    };
    auto set_item_size = [this](Item* item, float size) -> void
    { orientation() == Orientation::Horizontal ? item->set_width(size) : item->set_height(size); };

    const float avail_size = orientation() == Orientation::Horizontal ?
        width() - padding().result_horizontal() :
        height() - padding().result_vertical();
    float set_size         = std::accumulate(
        m_children_render_order.cbegin(),
        m_children_render_order.cend(),
        0.f,
        [&](float sum_size, Item* item) -> float { return sum_size + item_size(item); }
    );

    if (set_size > avail_size) {
        // Our saved size does not fit anymore, we need to try to shrink non-flex children
        float diff = set_size - avail_size;
        for (Item* content_item : m_content_items | std::views::reverse) {
            // skip flex children
            if (m_flex_children.contains(content_item)) {
                continue;
            }
            const float current_size      = item_size(content_item);
            const float possible_decrease = current_size - min_item_size(content_item);
            const float actual_decrease   = std::min(possible_decrease, diff);
            const float new_size          = current_size - actual_decrease;
            set_item_size(content_item, new_size);

            diff -= actual_decrease;
            if (Domain::fuzzy_compare(0.f, diff) || diff <= 0.f) {
                // we have increased size enough, exit
                break;
            }
        }
    }

    YGNodeCalculateLayout(node(), width(), height(), direction());

    for (Item* flex_child : std::as_const(m_flex_children)) {
        if (orientation() == Orientation::Horizontal) {
            float width = flex_child->width();
            flex_child->set_flex_grow(0);
            flex_child->set_width(width);
        } else {
            float height = flex_child->height();
            flex_child->set_flex_grow(0);
            flex_child->set_height(height);
        }
    }

    YGNodeCalculateLayout(node(), width(), height(), direction());
}

Item* SplitLayout::SeparatorItem::item() const
{
    if (window) {
        return window;
    } else {
        return button;
    }
}

} // namespace Slic3r::App::Yoga
