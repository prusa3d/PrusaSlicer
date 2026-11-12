#include "Slic3r/App/Yoga/SegmentedControl.hpp"
#include "Slic3r/App/Yoga/LayoutButton.hpp"

namespace Slic3r::App::Yoga {
SegmentedControl::SegmentedControl(
    std::initializer_list<Segment> segments,
    OnIndexSelected on_index_selected
)
{
    set_gap(10_fpx);

    bool initial_selection_picked{false};
    for (const Segment& segment : segments) {
        ASSERT(segment.icon != Render::Icon::None);

        LayoutButton* button{emplace_back<LayoutButton>("", segment.icon, segment.tooltip)};

        button->set_checkable(true);
        button->set_min_width(36_fpx);
        button->set_min_height(36_fpx);
        button->set_content_padding(9_fpx);
        m_group.insert_button(button);
        m_group.callbacks().checked_changed =
            [this, on_index_selected](AbstractButton* current_checked, AbstractButton* last_checked) {
                if (current_checked != last_checked) {
                    on_index_selected(*ASSERT_VAL(index_of(current_checked)));
                }
            };

        if (segment.initially_selected) {
            ASSERT(!initial_selection_picked);
            button->set_checked(true);
            initial_selection_picked = true;
        }
    }
    ASSERT(initial_selection_picked);
}

std::size_t SegmentedControl::selected_index() const
{
    std::optional<std::size_t> res{index_of(m_group.checked_button())};
    return *ASSERT_VAL(res);
}

void SegmentedControl::select_index(std::size_t index)
{
    ASSERT(index < items().size());
    const auto button{dynamic_cast<AbstractButton*>(items().at(index))};
    ASSERT_VAL(button)->set_checked(true);
}

} // namespace Slic3r::App::Yoga
