#pragma once

#include "Slic3r/Domain/SelectionId.hpp"

#include <string>

namespace Slic3r::Biz {

class IProjectsChangedListener {
public:
    virtual ~IProjectsChangedListener() = default;

    virtual void on_project_added_uninitialized(Domain::SelectionId project_id) {}
    virtual void on_project_will_be_removed(Domain::SelectionId project_id) {}
    virtual void on_project_removed(Domain::SelectionId project_id) {}
    virtual void on_project_changed(Domain::SelectionId project_id) {}
    virtual void on_project_loaded(Domain::SelectionId project_id) {}
    virtual void on_project_saved(Domain::SelectionId project_id) {}
    virtual void on_project_load_failed(const std::string& error) {}
};

} // namespace Slic3r::Biz
