#include "Slic3r/App/MultiSelections.hpp"

namespace Slic3r::App {

void MultiSelectionStorage::ApplyRequests(ImGuiMultiSelectIO* ms_io)
{
    // process ApplyRequests() from paarent class
    ImGuiSelectionBasicStorage::ApplyRequests(ms_io);

    // ApplyRequests alwys is called twise (for Begin and End MultipleSelection)
    if (is_started) {
        // So, always apply last_size && last_single_selected_id values on the Begin 
        last_size = Size;
        last_single_selected_id = Size == 1 ? _Storage.Data.begin()->key : 0;
        is_changed = false;
    }
    else {
        // And chech is something was changed at the End
        is_changed = last_size != static_cast<size_t>(Size) || (Size == 1 && last_single_selected_id != _Storage.Data.begin()->key);
    }

    is_started = !is_started;
}

void MultiSelections::clear_all()
{
    for (auto& [obj_id, ms] : *this)
        ms.Clear();
}

void MultiSelections::clear_except(size_t id)
{
    for (auto& [obj_id, ms] : *this)
        if (obj_id != id)
            ms.Clear();
}

}//namespace Slic3r::App