#include "Slic3r/App/Undo/BedSelectionState.hpp"

namespace Slic3r::App::Undo {

BedSelectionState::BedSelectionState(const Biz::Scene::BedSelection& selection) :
    selected_beds{selection.selected_beds()},
    last_selected_bed{selection.last_selected_bed()},
    camera_action_on_selection{selection.camera_action_on_selection()}
{}

} // namespace Slic3r::App::Undo
