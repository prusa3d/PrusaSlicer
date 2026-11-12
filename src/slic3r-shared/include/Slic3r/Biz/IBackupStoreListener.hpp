#pragma once

#include "Slic3r/Domain/SelectionId.hpp"

#include <boost/filesystem/path.hpp>

#include <vector>

namespace Slic3r::Biz {

class IBackupStoreListener
{
public:
    virtual ~IBackupStoreListener() = default;

    virtual void on_crashed_projects_detected(
        const std::vector<boost::filesystem::path>& crashed_projects
    )
    {}

    virtual void on_project_restore_completed() {}

    virtual void on_project_invalidation_changed(Domain::SelectionId project_id) {}
};

} // namespace Slic3r::Biz
