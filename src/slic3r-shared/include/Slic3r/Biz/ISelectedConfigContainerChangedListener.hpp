#pragma once

#include "Slic3r/Domain/SelectionId.hpp"

namespace Slic3r::Biz {
class ISelectedConfigContainerChangedListener
{
public:
    virtual ~ISelectedConfigContainerChangedListener() = default;

    virtual void on_selected_config_container_changed(Domain::SelectionId project_id, Domain::SelectionId container_id) = 0;
};
}
