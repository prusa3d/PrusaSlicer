#pragma once

#include "Slic3r/App/Yoga/CollapsibleWindow.hpp"
#include "Slic3r/Domain/SelectionId.hpp"
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
class Menu;
class MenuItem;
class ScrollArea;
} // namespace Yoga

class ObjectList;
class Navigator;

class ObjectListWindow : public Yoga::CollapsibleWindow
{
public:
    ObjectListWindow(Biz::ProjectInteractor* project_interactor, bool for_plater);
    void update_sliced_info();

    void set_bed_instance_icons(const Plater::BedThumbnailTextures& icons);
    void set_gizmo_controller(Scene::IGizmoController* controller);

    std::function<void()> on_config_container_added{nullptr};

private:
    void init_cc_context_menu();

private:
    Yoga::LayoutButton* m_add_container_button{nullptr};
    Yoga::ScrollArea* m_scroll_area{nullptr};
    ObjectList* m_object_list{nullptr};

    Yoga::Rectangle* m_sliced_info{nullptr};

    Yoga::Text* m_used_material_label{nullptr};
    Yoga::Text* m_used_material{nullptr};
    Yoga::Text* m_material_cost{nullptr};
    Yoga::Text* m_first_layer_time{nullptr};
    Yoga::Text* m_estimated_time{nullptr};

    Yoga::Item* m_material_cost_row{nullptr};
    Yoga::Item* m_first_layer_time_row{nullptr};

    Biz::ProjectInteractor* m_project_interactor{nullptr};

    // context menu for config containers
    Yoga::Menu* m_cc_context_menu{nullptr};
    Yoga::MenuItem* m_delete_cc_menu_item{nullptr};
    Domain::SelectionId m_selected_config_container_id{Domain::INVALID_ID};
};

} // namespace Slic3r::App
