#pragma once

#include <cstddef>

namespace Slic3r::Biz {

class ISelectedProjectChangedListener
{
public:
    virtual ~ISelectedProjectChangedListener() = default;

    /**
     * @brief Called whenever a project is switched, as early as it is known to
     * the ProjectInteractor (i.e. in case of newly added project).
     * @param index Project ID
     * @note Note that the selected project at this point may not be completed (i.e. have no config
     * container or bed). Use @c on_selected_project_changed_final if you need to guarantee complete
     * valid project loaded.
     */
    virtual void on_selected_project_changed(size_t index) {}

    /**
     * @brief Called whenever a project is switched, at point the project is completely constructed
     * (when adding a new project).
     * @param index Project ID
     */
    virtual void on_selected_project_changed_final(size_t index) {}


};

}
