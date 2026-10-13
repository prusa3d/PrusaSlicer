#include "Slic3r/App/ContextMenu.hpp"

#include "Slic3r/Biz/Scene/Selection.hpp"
#include "Slic3r/Domain/Project.hpp"
#include "Slic3r/Domain/ModelVolume.hpp"

namespace Slic3r::App {

void ContextMenuRequester::request_context_menu(ContextMenuType type, Domain::Vec2f mouse_position)
{
    invoke_listeners<IShowContextMenuListener>(
        [&](IShowContextMenuListener* l) { l->on_show_context_menu(type, mouse_position); }
    );
}

ContextMenuType
context_menu_type(const Biz::Scene::ObjectSelection& selection, const Domain::Project& project)
{
    if (selection.empty()) {
        return ContextMenuType::None;
    }

    if (selection.mode == Biz::Scene::SelectionMode::Volume) {
        if (selection.elements.size() == 1) {
            const Domain::ModelVolume* volume = project.find_volume_by_id(
                selection.elements.front().object_id,
                selection.elements.front().volume_id
            );
            if (volume && (volume->is_text() || volume->is_svg())) {
                return ContextMenuType::SvgOrText;
            }
        }
        return ContextMenuType::Volume;
    }

    return selection.only_single_object() ? ContextMenuType::Object : ContextMenuType::MultiObjects;
}

} // namespace Slic3r::App
