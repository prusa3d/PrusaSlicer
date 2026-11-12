#pragma once

#include "imgui/imgui.h"
#include <map>

namespace Slic3r::App {

struct MultiSelectionStorage : public ImGuiSelectionBasicStorage // !!! move into separate files with imgui includes
{
    // override ApplyRequests to check if selection was changed
    void ApplyRequests(ImGuiMultiSelectIO* ms_io);

    bool    is_changed{ false };

private:
    size_t  last_size{ 0 };
    size_t  last_single_selected_id{ 0 };
    bool    is_started{ true };
};

/**
    * @brief Help class to save several instances of MultiSelectionStorage for each set of volums or instances of the object
    * @param key is always Id of the object
    * @param value is a MultiSelectionStorage of the selected volums or instances of this object.
    */
class MultiSelections : public std::map<size_t, MultiSelectionStorage>
{
public:
    // T may be ModelInstancePtrs OR ModelVolumePtrs
    template <typename T>
    MultiSelectionStorage& get_ms(size_t object_id)
    {
        if (this->find(object_id) == this->end()) {
            this->emplace(object_id, MultiSelectionStorage());
            this->at(object_id).AdapterIndexToStorageId = [](ImGuiSelectionBasicStorage* self, int idx) {
                return (ImGuiID)((T*)self->UserData)->at(idx)->id().id;
                };
        }

        return this->at(object_id);
    }

    void clear_all();
    void clear_except(size_t id);
};
}