#pragma once

#include "Slic3r/App/Scene/IGizmo.hpp"
#include "Slic3r/App/Yoga/StackLayout.hpp"

#include <map>

namespace Slic3r::App {

class SidebarStackLayout : public Yoga::StackLayout
{
public:
    enum class ItemType
    {
        Bed,
        Object,
        Gizmo,
        GCode
    };

    void insert_item(ItemType type, Yoga::ItemPtr item);
    void insert_gizmo(Scene::ToolType tool_type, Yoga::ItemPtr item);

    void switch_to_item(ItemType type);
    void switch_to_gizmo(Scene::ToolType type);

    bool is_current_item(ItemType type);
    // Right now some of our gizmos do not have dialogs, this provides a checking method
    bool contains_gizmo(Scene::ToolType type) const;

private:
    // Intentionally hidden
    void prepend(Yoga::ObjectPtr child) override;
    void append(Yoga::ObjectPtr child) override;
    void insert(Yoga::ObjectPtr child, size_t index) override;

private:
    using GizmoMap = std::map<Scene::ToolType, Item*>;
    using ItemMap  = std::map<ItemType, Item*>;

    GizmoMap m_gizmo_map;
    ItemMap m_item_map;
};

} // namespace Slic3r::App
