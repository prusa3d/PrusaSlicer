#pragma once

#include "Slic3r/Domain/ElementRef.hpp"
#include "Slic3r/Domain/SelectionId.hpp"
namespace Slic3r::Biz {

class IArrangeEventsListener
{
public:
    virtual void
    on_elements_not_arranged(Domain::SelectionId project_id, const Domain::ElementRefs& elements)
    {}

    virtual void on_fatal_arrange_error(Domain::SelectionId project_id)
    {}
};
} // namespace Slic3r::Biz
