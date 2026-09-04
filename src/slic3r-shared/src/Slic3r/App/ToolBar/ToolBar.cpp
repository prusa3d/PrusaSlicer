#include "Slic3r/App/ToolBar/ToolBar.hpp"

#include "Slic3r/App/ToolBar/ToolBarButton.hpp"
#include "Slic3r/App/Yoga/Item.hpp"
#include "Slic3r/App/Yoga/ContextPopup.hpp"

#include <utility>
#include <imgui_internal.h>

using namespace Slic3r::App::Yoga;

namespace Slic3r::App {

ToolBar::ToolBar(const std::string& name) : Window(name)
{
    set_padding(4);
    // Button More is used for collapsible and should never be part of m_buttons
    m_button_more = emplace_back<ToolBarButton>(Render::Icon::Ellipsis, "Show more");
    m_button_more->set_object_name(name + "ShowMoreButton");
    m_button_more->set_has_arrow(true);
    m_button_more->set_visible(false);
}

ToolBar::Callbacks& ToolBar::toolbar_callbacks()
{
    return m_toolbar_callbacks;
}

std::unique_ptr<ToolBarButton> ToolBar::remove(ToolBarButton* button)
{
    if (contains(button)) {
        std::optional<size_t> index = index_of(button);
        ASSERT(index.has_value());
        m_buttons.erase(m_buttons.cbegin() + index.value());
        return unique_dynamic_cast<ToolBarButton>(Item::remove(button));
    } else {
        return nullptr;
    }
}

ToolBarButton* ToolBar::button_at(int index) const
{
    return m_buttons.at(index);
}

int ToolBar::button_count() const
{
    return m_buttons.size();
}

bool ToolBar::contains(ToolBarButton* button) const
{
    return index_of(button).has_value();
}

float ToolBar::button_width() const
{
    return m_button_width;
}

void ToolBar::set_button_width(const float button_width)
{
    if (!Domain::fuzzy_compare(m_button_width, button_width)) {
        m_button_width = button_width;
        for (ToolBarButton* button : std::as_const(m_buttons)) {
            if (orientation() == Orientation::Horizontal && !button->label().empty()) {
                continue;
            }
            button->set_width(button_width);
        }
        m_button_more->set_width(button_width);
    }
}

float ToolBar::button_height() const
{
    return m_button_height;
}

void ToolBar::set_button_height(float button_height)
{
    if (!Domain::fuzzy_compare(m_button_height, button_height)) {
        m_button_height = button_height;
        for (ToolBarButton* button : std::as_const(m_buttons)) {
            if (orientation() == Orientation::Vertical && !button->label().empty()) {
                continue;
            }
            button->set_height(button_height);
        }
        m_button_more->set_height(button_height);
    }
}

void ToolBar::append(ObjectPtr child)
{
    Item::append(std::move(child));
}

void ToolBar::insert(ObjectPtr child, size_t index)
{
    ToolBarButton* button = dynamic_cast<ToolBarButton*>(child.get());
    ASSERT(button);

    if (m_button_width > 0 && (orientation() != Orientation::Horizontal || button->label().empty()))
    {
        button->set_width(m_button_width);
    }
    if (m_button_height > 0 && (orientation() != Orientation::Vertical || button->label().empty()))
    {
        button->set_height(m_button_height);
    }

    m_buttons.push_back(button);
    Item::insert(std::move(child), object_count() ? index - 1 : index);
}

ObjectPtr ToolBar::remove(Object* child)
{
    return Item::remove(child);
}

bool ToolBar::hovered() const
{
    return m_hovered;
}

bool ToolBar::has_enough_space() const
{
    return m_has_enough_space;
}

bool ToolBar::collapsible() const
{
    return m_collapsible;
}

void ToolBar::set_collapsible(bool collapsible)
{
    if (m_collapsible != collapsible) {
        m_collapsible = collapsible;
        set_style_dirty();
    }
}

float ToolBar::available_size() const
{
    return m_available_size;
}

void ToolBar::set_available_size(float available_size)
{
    m_available_size = available_size;
}

void ToolBar::style_node()
{
    // I absolutely understand this is hidous, I already spent > 0 hours debugging this,
    // I will someday clean this up, but today is not the day
    bool has_enough_space = true;
    if (m_collapsible
        && parent_item()
        && m_button_width > 0
        && m_button_height > 0
        && is_visible()
        && width() > 0
        && height() > 0)
    {
        // Compute available size
        float available_size = orientation() == Orientation::Horizontal ? parent_item()->width() :
                                                                          parent_item()->height();
        for (Item* node : parent_item()->items()) {
            if (node != this) {
                available_size -=
                    orientation() == Orientation::Horizontal ? node->width() : node->height();
            }
            if (node != *parent_item()->items().rbegin()) {
                available_size -= parent_item()->gap().value;
            }
            available_size -= orientation() == Orientation::Horizontal ?
                node->margin().result_horizontal() :
                node->margin().result_vertical();
        }

        available_size -= orientation() == Orientation::Horizontal ? padding().result_horizontal() :
                                                                     padding().result_vertical();

        // Decide which buttons will be included

        // Assume button size
        const float button_size =
            orientation() == Orientation::Horizontal ? m_button_width : m_button_height;
        // Take away size from collapsed button
        available_size -= button_size;

        std::vector<ToolBarButton*> included_buttons;
        std::vector<ToolBarButton*> collapsed_buttons;
        for (ToolBarButton* button : std::as_const(m_buttons)) {
            if (button == m_button_more) {
                continue;
            }

            if (!button->is_self_visible()) {
                included_buttons.push_back(button);
                continue;
            }

            available_size -= button_size;
            if (available_size > 0) {
                included_buttons.push_back(button);
            } else {
                collapsed_buttons.push_back(button);
            }

            if (button != *m_buttons.rbegin()) {
                available_size -= gap().value;
            }
        }

        if (!collapsed_buttons.empty()) {
            // Put every collapsed button to ButtonMore subtoolbar
            m_button_more->set_orientation(orientation());
            ContextPopup* subtoolbar = m_button_more->get_or_create_subtoolbar();
            for (ToolBarButton* collapsed_button : std::as_const(collapsed_buttons)) {
                if (collapsed_button->parent() != subtoolbar->content_item()) {
                    subtoolbar->append(
                        unique_dynamic_cast<ToolBarButton>(Item::remove(collapsed_button))
                    );
                }
            }
        }

        m_button_more->set_visible(!collapsed_buttons.empty());

        Yoga::ContextPopup* subtoolbar = m_button_more->get_subtoolbar();
        if (subtoolbar) {
            // Go through every included button and append it if they are missing
            for (ToolBarButton* included_button : std::as_const(included_buttons)) {
                if (included_button->parent() == subtoolbar->content_item()) {
                    Item::insert(subtoolbar->remove(included_button), object_count() - 1);
                }
            }
        }

        has_enough_space = collapsed_buttons.empty();
        if (has_enough_space && !m_has_enough_space && m_toolbar_callbacks.expand_margin
            && available_size < m_toolbar_callbacks.expand_margin())
        {
            // The extra room may only exist because a sibling shrank in reaction to us
            // losing space (e.g. hid a label); don't report regained space unless
            // there's enough slack to also cover what re-showing it would cost.
            has_enough_space = false;
        }
    }

    if (m_has_enough_space != has_enough_space) {
        m_has_enough_space = has_enough_space;
        if (m_toolbar_callbacks.has_enough_space_changed) {
            m_toolbar_callbacks.has_enough_space_changed(has_enough_space);
        }
    }

    Item::style_node();
}

std::optional<size_t> ToolBar::index_of(ToolBarButton* button) const
{
    std::vector<ToolBarButton*>::const_iterator it = std::find_if(
        m_buttons.cbegin(),
        m_buttons.cend(),
        [button](ToolBarButton* child_button) { return button == child_button; }
    );

    return it != m_buttons.cend() ? std::distance(m_buttons.cbegin(), it) : std::optional<size_t>();
}

} // namespace Slic3r::App
