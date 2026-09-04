#include "Slic3r/App/Yoga/StackLayout.hpp"

namespace Slic3r::App::Yoga {

StackLayout::StackLayout()
{
    set_object_name("StackLayout");
}

void StackLayout::insert(ObjectPtr child, size_t index)
{
    Item* item = dynamic_cast<Item*>(child.get());
    ASSERT(item, "StackLayout only supports Items");
    if (!m_children.empty()) {
        item->set_visible(false);
    }

    Item::insert(std::move(child), index);
}

ObjectPtr StackLayout::remove(Object* child)
{
    std::optional<size_t> index = index_of(child);
    ASSERT(index.has_value());

    ObjectPtr item = Item::remove(child);

    if (m_current_index >= index.value()) {
        m_current_index--;
    }
    m_current_index = std::clamp(m_current_index, size_t{0}, object_count() - 1);
    // It is valid that StackLayout has no children, therefore we have to check if any children exists
    if (object_count()) {
        get_item(m_current_index)->set_visible(true);
    }

    return item;
}

size_t StackLayout::current_index() const
{
    return m_current_index;
}

void StackLayout::set_current_index(size_t current_index)
{
    if (m_current_index == current_index) {
        return;
    }

    ASSERT(current_index < object_count());

    m_current_index = current_index;

    for (size_t index = 0; index < object_count(); ++index) {
        get_item(index)->set_visible(m_current_index == index);
    }
}

void StackLayout::set_current_item(Item* item)
{
    std::optional<size_t> index = index_of(item);
    ASSERT(index.has_value());

    set_current_index(index.value());
}

} // namespace Slic3r::App::Yoga
