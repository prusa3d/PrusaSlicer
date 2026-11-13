#include "Slic3r/App/ObjectListWindow.hpp"

#include "Slic3r/Biz/ProjectInteractor.hpp"
#include "Slic3r/Biz/I18N/I18N.hpp"

#include "Slic3r/App/Yoga/LayoutButton.hpp"

#include "Slic3r/App/ObjectList/BedSlicedInfo.hpp"
#include "Slic3r/App/ObjectListItem.hpp"

#include "Slic3r/App/Plater/BedThumbnailTexture.hpp"

using namespace Slic3r::Biz;

namespace Slic3r::App {

using namespace Yoga;

ObjectListWindow::ObjectListWindow(Biz::ProjectInteractor& project_interactor, bool for_plater) :
    CollapsibleWindow(_u8L("Object list"), "ObjectListWindow"),
    m_project_interactor(project_interactor)
{
    set_transparent(true);
    content()->set_padding(0.f);
    const ObjectListItem::Mode mode =
        for_plater ? ObjectListItem::Mode::Plater : ObjectListItem::Mode::Preview;
    if (mode == ObjectListItem::Mode::Plater) {
        m_add_container_button = content()->emplace_back<Yoga::LayoutButton>(
            _u8L("Add printer group"),
            Render::Icon::ConfigContainer
        );
        m_add_container_button->set_object_name("AddPrinterGroupButton");
        m_add_container_button->set_flex_shrink(0.f);
        m_add_container_button->set_padding({20.f, 5.f});
        m_add_container_button->callbacks().action = [this]()
        {
            m_project_interactor.add_config_container();
            if (on_config_container_added != nullptr) {
                on_config_container_added();
                m_project_interactor.undo_provider().take_snapshot(Biz::UndoSnapshotType::AddConfigContainer);
            }
        };
    }

    m_object_list = content()->emplace_back<ObjectListItem>(project_interactor, mode);

    if (mode == ObjectListItem::Mode::Preview) {
        m_sliced_info = content()->emplace_back<ObjectList::BedSlicedInfo>(project_interactor);
    }
}

void ObjectListWindow::update_sliced_info()
{
    m_sliced_info->update();
}

void ObjectListWindow::set_bed_instance_icons(const Plater::BedThumbnailTextures& icons)
{
    m_object_list->set_bed_instance_icons(icons);
}

void ObjectListWindow::set_gizmo_controller(Scene::IGizmoController* controller)
{
    // Not yet consumed by ObjectListItem; stored for when gizmo-triggering rows are ported.
    m_gizmo_controller = controller;
}

void ObjectListWindow::add_context_menu_listener(IShowContextMenuListener* listener)
{
    m_object_list->add_context_menu_listener(listener);
}

void ObjectListWindow::start_renaming_selected()
{
    set_collapsed(false);
    m_object_list->start_renaming_selected();
}

} // namespace Slic3r::App
