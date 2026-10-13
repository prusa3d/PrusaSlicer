#pragma once

#include "Slic3r/App/Yoga/CollapsibleWindow.hpp"
#include <functional>

namespace Slic3r::Biz {
class ProjectInteractor;
} // namespace Slic3r::Biz

namespace Slic3r::App::Scene {
class IGizmoController;
} // namespace Slic3r::App::Scene

namespace Slic3r::App::Plater {
struct BedThumbnailTexture;
using BedThumbnailTextures = std::vector<BedThumbnailTexture>;
} // namespace Slic3r::App::Plater

namespace Slic3r::App {

namespace Yoga {
class Text;
class Rectangle;
class LayoutButton;
class ScrollArea;
} // namespace Yoga

class ObjectListItem;
class IShowContextMenuListener;

class ObjectListWindow : public Yoga::CollapsibleWindow
{
public:
    explicit ObjectListWindow(Biz::ProjectInteractor& project_interactor, bool for_plater);
    void update_sliced_info();

    void set_bed_instance_icons(const Plater::BedThumbnailTextures& icons);
    void set_gizmo_controller(Scene::IGizmoController* controller);

    /**
     * @brief Adds a listener showing the context menus requested by right-clicks on the object
     * list rows.
     */
    void add_context_menu_listener(IShowContextMenuListener* listener);

    /**
     * @brief Shows the name editor on the row of the single selected object or volume. The window
     * and the collapsed parents of the row are expanded, so the editor is visible.
     */
    void start_renaming_selected();

    std::function<void()> on_config_container_added{nullptr};

private:
    Yoga::LayoutButton* m_add_container_button{nullptr};
    ObjectListItem* m_object_list{nullptr};

    Yoga::Rectangle* m_sliced_info{nullptr};

    Yoga::Text* m_used_material_total{nullptr};
    Yoga::Item* m_used_material_for_objects_row{nullptr};
    Yoga::Text* m_used_material_for_objects{nullptr};
    Yoga::Item* m_used_material_for_wipe_tower_row{nullptr};
    Yoga::Text* m_used_material_for_wipe_tower{nullptr};
    Yoga::Item* m_used_material_for_flush_row{nullptr};
    Yoga::Text* m_used_material_for_flush{nullptr};
    Yoga::Item* m_used_material_for_supports_row{nullptr};
    Yoga::Text* m_used_material_for_supports{nullptr};

    Yoga::Text* m_material_cost{nullptr};
    Yoga::Text* m_first_layer_time{nullptr};
    Yoga::Text* m_estimated_time{nullptr};

    Yoga::Item* m_material_cost_row{nullptr};
    Yoga::Item* m_first_layer_time_row{nullptr};

    Biz::ProjectInteractor& m_project_interactor;

    // Not yet consumed by ObjectListItem; stored for when gizmo-triggering rows are ported.
    Scene::IGizmoController* m_gizmo_controller{nullptr};
};

} // namespace Slic3r::App
