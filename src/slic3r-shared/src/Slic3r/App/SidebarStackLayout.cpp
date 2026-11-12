#include "Slic3r/App/SidebarStackLayout.hpp"

using namespace Slic3r::App::Yoga;

namespace Slic3r::App {

void SidebarStackLayout::insert_item(ItemType type, Yoga::ItemPtr item)
{
    ASSERT(type != ItemType::Gizmo, "To insert Gizmo please use insert_gizmo");
    ASSERT(!m_item_map.contains(type), "Item is already inserted");

    m_item_map[type] = item.get();

    StackLayout::append(std::move(item));
}

void SidebarStackLayout::insert_gizmo(Scene::ToolType tool_type, Yoga::ItemPtr item)
{
    ASSERT(!m_gizmo_map.contains(tool_type), "Gizmo is already inserted");
    m_gizmo_map[tool_type] = item.get();

    StackLayout::append(std::move(item));
}

void SidebarStackLayout::switch_to_item(ItemType type) {
    ItemMap::const_iterator it = m_item_map.find(type);
    ASSERT(it != m_item_map.cend());

    set_current_item(it->second);
}

void SidebarStackLayout::switch_to_gizmo(Scene::ToolType type) {
    GizmoMap::const_iterator it = m_gizmo_map.find(type);
    ASSERT(it != m_gizmo_map.cend());

    set_current_item(it->second);
}

bool SidebarStackLayout::is_current_item(ItemType type)
{
    ItemMap::const_iterator it = m_item_map.find(type);
    ASSERT(it != m_item_map.cend());
    return current_index() == index_of(it->second);
}

bool SidebarStackLayout::contains_gizmo(Scene::ToolType type) const
{
    return m_gizmo_map.find(type) != m_gizmo_map.cend();
}

void SidebarStackLayout::prepend(Yoga::ObjectPtr child)
{
    StackLayout::prepend(std::move(child));
}

void SidebarStackLayout::append(Yoga::ObjectPtr child)
{
    StackLayout::append(std::move(child));
}

void SidebarStackLayout::insert(Yoga::ObjectPtr child, size_t index)
{
    StackLayout::insert(std::move(child), index);
}

} // namespace Slic3r::App
