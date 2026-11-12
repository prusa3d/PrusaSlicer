#pragma once

#include <Slic3r/App/Yoga/RootItem.hpp>

namespace Slic3r::App::Yoga {

class TestRootItem : public RootItem
{
public:
    void process_loop_events() { m_loop_events.process_events(); }
};

} // namespace Slic3r::App::Yoga
