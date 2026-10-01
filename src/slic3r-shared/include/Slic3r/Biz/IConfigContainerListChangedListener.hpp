#pragma once

#include "Slic3r/Domain/SelectionId.hpp"

namespace Slic3r::Biz {

class IConfigContainerListChangedListener
{
public:
    virtual ~IConfigContainerListChangedListener() = default;

    virtual void on_config_container_list_changed(Domain::SelectionId project_id) {}

    virtual void on_config_container_bed_list_changed(
        Domain::SelectionId project_id,
        Domain::SelectionId config_container_id
    )
    {}
};

} // namespace Slic3r::Biz
