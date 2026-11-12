#pragma once

#include "Slic3r/App/Yoga/ButtonGroup.hpp"
#include "Slic3r/App/Yoga/Item.hpp"
#include "Slic3r/App/Render/ImguiTypes.hpp"

namespace Slic3r::App::Yoga {

class SegmentedControl : public Item
{
public:
    struct Segment
    {
        Render::Icon icon{Render::Icon::None};
        std::string tooltip;
        bool initially_selected{false};
    };

    using OnIndexSelected = std::function<void(std::size_t)>;

    SegmentedControl(
        std::initializer_list<Segment> segments,
        OnIndexSelected on_index_selected = [](std::size_t) {}
    );

    std::size_t selected_index() const;

    void select_index(std::size_t);

private:
    ButtonGroup m_group;
};
} // namespace Slic3r::App::Yoga
